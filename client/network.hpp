#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring> // memset
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

#include "common/codec.hpp"
#include "common/config.hpp"
#include "common/protocol.hpp"
#include "types.hpp"

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
		std::array<stats_t, config::client::num_streams> stats {};
	};

	~stream_t();

	stream_t(const server_info_t* server_infos, uint8_t* screen_buffer);

	auto send(const std::vector<render_command_t>& cmds) -> void;
	auto recv() -> result_t;

	template <typename T>
	auto recv(const std::chrono::time_point<T>& timeout) -> result_t;

private:
	result_t result {};
	uint8_t* screen_buffer {nullptr};
	std::vector<uint8_t> enc_buffer;
	std::thread pkt_recv_worker;

	// Networking data
	int sock {};
	sockaddr_in client_addr;
	std::vector<sockaddr_in> server_addrs;
	std::unordered_map<uint32_t, int> server_id_map;

	// System state
	uint32_t active_stream_bitmask {};
	std::atomic_flag drop_incoming_pkts;
	std::atomic_flag is_running; // TODO: Use std::recv_token
	std::array<uint8_t, config::common::pkt_buffer_size> pkt_buffer;
	std::array<std::array<uint32_t, config::common::num_slices>, config::client::num_streams> pkt_bitmasks;

	// For signal early exit
	bool all_stream_ready {false};
	std::mutex all_stream_ready_mutex;
	std::condition_variable all_stream_ready_cv;

	auto send_render_command(const render_command_t& cmd, int server_id) -> int;
	auto recv_packet() -> int;
	auto pkt_recv_worker_task() -> void;
};

stream_t::~stream_t()
{
	is_running.clear();
	pkt_recv_worker.join();
	close(sock);
}

stream_t::stream_t(const server_info_t* server_infos, uint8_t* screen_buffer)
	: screen_buffer {screen_buffer}
{
	enc_buffer.resize(config::common::screen_buffer_size * config::client::num_streams);

	for (auto&& x : pkt_bitmasks) std::fill(std::begin(x), std::end(x), 0);

	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) throw std::runtime_error {"Failed to create stream socket!"};

	const auto recv_timeout = timeval { .tv_sec = 1, };
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));

	constexpr auto priority = 6 << 7;
	setsockopt(sock, IPPROTO_IP, IP_TOS, &priority, sizeof(priority));

	constexpr auto flag = 1;
	setsockopt(sock, SOL_SOCKET, SO_DONTROUTE, &flag, sizeof(flag));

	//constexpr auto rcvbuf_size = config::common::pkt_buffer_size;
	//setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf_size, sizeof(rcvbuf_size));

	//const auto value = 1;
	//setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &value, sizeof(value));

	for (auto i = 0; i < config::client::num_streams; i++)
	{
		sockaddr_in server_addr;
		std::memset(&server_addr, 0, sizeof(server_addr));
		server_addr.sin_family      = AF_INET;
		server_addr.sin_addr.s_addr = htonl(server_infos[i].addr);
		server_addr.sin_port        = htons(server_infos[i].port);
		server_addrs.push_back(server_addr);
		server_id_map.insert({server_infos[i].addr, i});
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
	pkt_recv_worker = std::thread {[this](){ pkt_recv_worker_task(); }};
	if (pthread_setaffinity_np(pkt_recv_worker.native_handle(), sizeof(cpu_set), &cpu_set) != 0)
	{
		std::cerr << "Failed to set recv-thread affinity!\n";
	}
}

auto stream_t::send(const std::vector<render_command_t>& cmds) -> void
{
	// Reset system state
	active_stream_bitmask = 0;
	result.stream_bitmask = config::client::all_stream_bitmask;
	for (auto&& x : result.stats) x.slice_bitmask = 0;
	for (auto&& x : pkt_bitmasks) std::fill(std::begin(x), std::end(x), 0);
	{
		std::lock_guard lock {all_stream_ready_mutex};
		all_stream_ready = false;
	}

	// Re-enable packet reception and send pose to servers to start render
	drop_incoming_pkts.clear();
	for (auto i = 0; i < cmds.size(); i++) send_render_command(cmds[i], i);
}

auto stream_t::recv() -> result_t
{
	// Halt processing incoming packets
	drop_incoming_pkts.test_and_set();

	// Mark missing streams
	for (auto i = 0; i < config::client::num_streams; i++)
	{
		if (result.stats[i].slice_bitmask == 0) result.stream_bitmask &= ~(1U << i);
	}

	return std::exchange(result, {});
}

template <typename T>
auto stream_t::recv(const std::chrono::time_point<T>& timeout) -> result_t
{
	std::unique_lock lock {all_stream_ready_mutex};
	all_stream_ready_cv.wait_until(lock, timeout, [this](){ return all_stream_ready; });
	return recv();
}

auto stream_t::send_render_command(const render_command_t& cmd, int server_id) -> int
{
	const auto nbytes = sendto(sock, &cmd, sizeof(cmd), 0,
		reinterpret_cast<const sockaddr*>(&server_addrs[server_id]), sizeof(server_addrs[server_id]));
	if (nbytes < 0) std::cerr << "Failed to send pose!\n";
	return nbytes;
}

auto stream_t::recv_packet() -> int
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

auto stream_t::pkt_recv_worker_task() -> void
{
	while (is_running.test())
	{
		const auto stream_id = recv_packet();
		if (drop_incoming_pkts.test() || stream_id < 0) continue;

		auto pkt_ptr = pkt_buffer.data();

		protocol::pkt_info_t pkt_info;
		pkt_ptr = protocol::read(pkt_ptr, pkt_info);
		//std::clog << pkt_info << '\n';

		// Determine precise location in buffer to store packet
		// Ignore packets with no encoded data
		constexpr auto max_pkt_payload_size = config::common::pkt_buffer_size - sizeof(pkt_info);
		const auto stream_offset = stream_id * config::common::screen_buffer_size;
		const auto slice_offset  = pkt_info.slice_id * config::common::slice_buffer_size;
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
		const auto frame_end = pkt_info.slice_id == (config::common::num_slices - 1);
		const auto is_frame_end_pkt = frame_end && pkt_info.slice_end;
		if (is_frame_end_pkt)
		{
			protocol::frame_info_t frame_info;
			protocol::read(pkt_buffer.data() + config::common::pkt_buffer_size - sizeof(frame_info), frame_info);

			const auto pose_recv_timestamp = get_timestamp_ns();
			const auto pose_rtt_ns = pose_recv_timestamp - frame_info.timestamp;

			result.stats[stream_id].pose_rtt_ns    = pose_rtt_ns;
			result.stats[stream_id].render_time_us = frame_info.render_time_us;
			result.stats[stream_id].stream_time_us = frame_info.stream_time_us;

			active_stream_bitmask |= (1U << stream_id);
		}

		// Early exit when all streams are received
		if (active_stream_bitmask == config::client::all_stream_bitmask)
		{
			{
				std::lock_guard lock {all_stream_ready_mutex};
				all_stream_ready = true;
			}
			all_stream_ready_cv.notify_one();
		}
	}
}
