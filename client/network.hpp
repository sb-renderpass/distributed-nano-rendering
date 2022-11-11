#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstring> // memset
#include <future>
#include <functional>
#include <iostream>
#include <pthread.h>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <fmt/core.h>
#include <fmt/color.h>

#include "config.hpp"
#include "types.hpp"
#include "common/codec.hpp"
#include "common/protocol.hpp"

constexpr auto frame_buffer_width  = config::width;
constexpr auto frame_buffer_height = config::height;
constexpr auto frame_buffer_size   = frame_buffer_width * frame_buffer_height;
constexpr auto slice_buffer_size   = frame_buffer_size / config::num_slices;
constexpr auto all_stream_bitmask  = (1U << config::num_streams) - 1U;
constexpr auto all_slice_bitmask   = (1U << config::num_slices ) - 1U;
constexpr auto target_frame_time   = 1.0F / config::target_fps;
constexpr auto timeout_us          = static_cast<int64_t>(target_frame_time * 1e6F);

auto get_timestamp_ns() -> uint64_t
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::high_resolution_clock::now().time_since_epoch()).count();
}

class stream_t
{
public:
	struct result_t
	{
		uint32_t stream_bitmask {};
		std::array<stats_t, config::num_streams> stats {};
	};

	~stream_t();

	stream_t(const std::vector<server_t>& server_addr, uint8_t* screen_buffer);

	auto start(const std::vector<render_command_t>& cmds) -> std::future<void>;
	auto stop() -> result_t;
 
private:
	uint8_t* screen_buffer {nullptr};
	std::array<std::array<uint32_t, config::num_slices>, config::num_streams> pkt_bitmasks;

	int sock {};
	std::vector<sockaddr_in> server_addrs;
	sockaddr_in client_addr;
	std::thread recv_thread;
	std::atomic_flag drop_incoming_pkts;
	std::atomic_flag is_running; // TODO: Use std::stop_token
	std::array<uint8_t, config::pkt_buffer_size> pkt_buffer;
	std::unordered_map<uint32_t, int> server_id_map;
	std::vector<uint8_t> enc_buffer;
	result_t result {};
	uint32_t active_stream_bitmask {};
	std::promise<void> ready_promise;

	auto send_render_command(const render_command_t& cmd, int server_id) -> int;
	auto recv_pkt() -> int;
	auto recv_thread_task() -> void;
};

stream_t::~stream_t()
{
	is_running.clear();
	recv_thread.join();
	close(sock);
}

stream_t::stream_t(const std::vector<server_t>& servers, uint8_t* screen_buffer)
	: screen_buffer {screen_buffer}
{
	enc_buffer.resize(frame_buffer_size * config::num_streams);

	for (auto&& x : pkt_bitmasks) std::fill(std::begin(x), std::end(x), 0);

	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) throw std::runtime_error {"Failed to create stream socket!"};

	const auto recv_timeout = timeval { .tv_sec = 1, };
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));

	constexpr auto priority = 6 << 7;
	setsockopt(sock, IPPROTO_IP, IP_TOS, &priority, sizeof(priority));

	constexpr auto flag = 1;
	setsockopt(sock, SOL_SOCKET, SO_DONTROUTE, &flag, sizeof(flag));

	//constexpr auto rcvbuf_size = config::pkt_buffer_size;
	//setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf_size, sizeof(rcvbuf_size));

	//const auto value = 1;
	//setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &value, sizeof(value));

	for (auto i = 0; i < servers.size(); i++)
	{
		sockaddr_in server_addr;
		std::memset(&server_addr, 0, sizeof(server_addr));
		server_addr.sin_family      = AF_INET;
		server_addr.sin_addr.s_addr = htonl(servers[i].ip);
		server_addr.sin_port        = htons(servers[i].port);
		server_addrs.push_back(server_addr);
		server_id_map.insert({servers[i].ip, i});
	}

	std::memset(&client_addr, 0, sizeof(client_addr));
	client_addr.sin_family      = AF_INET;
	client_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	client_addr.sin_port        = htons(0);

	if (bind(sock, reinterpret_cast<const sockaddr*>(&client_addr), sizeof(client_addr)) < 0)
	{
		throw std::runtime_error {"Failed to bind to stream socket!"};
	}

	drop_incoming_pkts.test_and_set();
	is_running.test_and_set();

	cpu_set_t cpu_set;
	CPU_ZERO(&cpu_set);
	CPU_SET(1, &cpu_set);
	recv_thread = std::thread {[this](){ recv_thread_task(); }};
	if (pthread_setaffinity_np(recv_thread.native_handle(), sizeof(cpu_set), &cpu_set) != 0)
	{
		std::cerr << "Failed to set recv-thread affinity!\n";
	}
}

auto stream_t::start(const std::vector<render_command_t>& cmds) -> std::future<void>
{
	// Reset system state
	active_stream_bitmask = 0;
	result.stream_bitmask = all_stream_bitmask;
	for (auto&& x : result.stats) x.slice_bitmask = 0;
	for (auto&& x : pkt_bitmasks) std::fill(std::begin(x), std::end(x), 0);

	// Re-enable packet reception and send pose to servers to start frame render
	drop_incoming_pkts.clear();
	for (auto i = 0; i < cmds.size(); i++) send_render_command(cmds[i], i);

	ready_promise = {};
	return ready_promise.get_future();
}

auto stream_t::stop() -> result_t
{
	drop_incoming_pkts.test_and_set();

	// Mark missing streams
	for (auto i = 0; i < config::num_streams; i++)
	{
		if (result.stats[i].slice_bitmask == 0) result.stream_bitmask &= ~(1U << i);
	}

	return std::exchange(result, {});
}

auto stream_t::send_render_command(const render_command_t& cmd, int server_id) -> int
{
	const auto nbytes = sendto(sock, &cmd, sizeof(cmd), 0,
		reinterpret_cast<const sockaddr*>(&server_addrs[server_id]), sizeof(server_addrs[server_id]));
	if (nbytes < 0) std::cerr << "Failed to send pose!\n";
	return nbytes;
}

auto stream_t::recv_pkt() -> int
{
	struct sockaddr_in server_addr;
	socklen_t server_addr_size = sizeof(server_addr);
	std::memset(&server_addr, 0, sizeof(server_addr));

	const auto nbytes = recvfrom(
		sock, pkt_buffer.data(), pkt_buffer.size(), 0,
		reinterpret_cast<struct sockaddr*>(&server_addr), &server_addr_size);

	if (nbytes < static_cast<int>(pkt_buffer.size()))
	{
		std::cerr << "Failed to recv frame packet!\n";
		return -1;
	}

	const auto server_ip = ntohl(server_addr.sin_addr.s_addr);
	return server_id_map.at(server_ip);
}

auto stream_t::recv_thread_task() -> void
{
	while (is_running.test())
	{
		const auto stream_id = recv_pkt();
		if (drop_incoming_pkts.test() || stream_id < 0) continue;

		auto pkt_ptr = pkt_buffer.data();

		protocol::pkt_info_t pkt_info;
		pkt_ptr = protocol::read(pkt_ptr, pkt_info);
		//std::clog << pkt_info << '\n';

		// Determine precise location in buffer to store packet
		// Ignore packets with no encoded data
		constexpr auto max_pkt_payload_size = config::pkt_buffer_size - sizeof(pkt_info);
		const auto stream_offset = stream_id * frame_buffer_size;
		const auto slice_offset  = pkt_info.slice_id * slice_buffer_size;
		const auto pkt_offset    = pkt_info.pkt_id   * max_pkt_payload_size;
		const auto enc_ptr       = enc_buffer.data() + stream_offset + slice_offset + pkt_offset;
		if (pkt_info.has_data) protocol::read_payload(pkt_ptr, max_pkt_payload_size, enc_ptr);

		// Mark packet received for a slice
		pkt_bitmasks[stream_id][pkt_info.slice_id] |= (1U << pkt_info.pkt_id);

		// Mark slice complete if all of its packets have been received
		const auto all_slice_pkts_bitmask = (1U << (pkt_info.pkt_id + 1)) - 1U;
		const auto all_slice_pkts_recvd   = pkt_bitmasks[stream_id][pkt_info.slice_id] == all_slice_pkts_bitmask;
		if (pkt_info.slice_end && all_slice_pkts_recvd)
		{
			result.stats[stream_id].slice_bitmask |= (1U << pkt_info.slice_id);

			// Decode slice once all packets have been received
			auto enc_ptr = enc_buffer.data() + stream_offset + slice_offset;
			auto out_ptr = screen_buffer     + stream_offset + slice_offset;
			result.stats[stream_id].num_enc_bytes += codec::decode_slice(enc_ptr, out_ptr);
		}

		// Unpack frame stats from the last packet of the frame
		const auto frame_end = pkt_info.slice_id == config::num_slices - 1;
		const auto is_frame_end_pkt = frame_end && pkt_info.slice_end;
		if (is_frame_end_pkt)
		{
			protocol::frame_info_t frame_info;
			protocol::read(pkt_buffer.data() + config::pkt_buffer_size - sizeof(frame_info), frame_info);

			const auto pose_recv_timestamp = get_timestamp_ns();
			const auto pose_rtt_ns = pose_recv_timestamp - frame_info.timestamp;

			result.stats[stream_id].pose_rtt_ns    = pose_rtt_ns;
			result.stats[stream_id].render_time_us = frame_info.render_time_us;
			result.stats[stream_id].stream_time_us = frame_info.stream_time_us;

			active_stream_bitmask |= (1U << stream_id);

			// Encode-Decode Test
			/*
			constexpr auto W = config::height;
			constexpr auto H = config::width / config::num_slices;
			auto num_total_bytes = 0;
			for (auto i = 0; i < config::num_slices; i++)
			{
				const auto slice_offset = i * slice_buffer_size;
				auto slice_buffer = (*out_buffers)[server_id].data() + slice_offset;

				const auto num_enc_bytes = codec::encode_slice(slice_buffer, enc_buffer, W, H);
				const auto cr = (float)num_enc_bytes / slice_buffer_size;
				std::clog << i << ' ' << cr << '\n';

				codec::decode_slice(enc_buffer, slice_buffer);

				num_total_bytes += num_enc_bytes;
			}
			const auto cr = (float)num_total_bytes / frame_buffer_size;
			std::clog << "= " << cr << '\n';
			std::clog << "----------\n";
			*/
		}

		// Early exit when all frames are received
		//if (active_stream_bitmask == all_stream_bitmask) ready_promise.set_value();
	}
}
