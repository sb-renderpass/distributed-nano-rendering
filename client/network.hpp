#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstring> // memset
#include <future>
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

constexpr auto frame_buffer_width  = config::width;
constexpr auto frame_buffer_height = config::height;
constexpr auto frame_buffer_size   = frame_buffer_width * frame_buffer_height;
constexpr auto slice_buffer_size   = frame_buffer_size / config::num_slices;

constexpr auto num_pkts_per_slice  = static_cast<int>(std::ceil(static_cast<float>(slice_buffer_size) / config::pkt_buffer_size));
constexpr auto num_pkts_per_frame  = num_pkts_per_slice * config::num_slices;
constexpr auto last_seqnum         = num_pkts_per_frame - 1;
constexpr auto last_pkt_bitmask    = (1ULL << last_seqnum);
constexpr auto all_pkt_bitmask     = (1ULL << num_pkts_per_frame) - 1ULL;
constexpr auto all_stream_bitmask  = (1U   << config::num_streams) - 1U;
constexpr auto all_slice_bitmask   = (1U   << config::num_slices ) - 1U;

constexpr auto target_frame_time   = 1.0F / config::target_fps;
constexpr auto timeout_us          = static_cast<int64_t>(target_frame_time * 1e6F);

auto get_timestamp_ns() -> uint64_t
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::high_resolution_clock::now().time_since_epoch()).count();
}

auto unpack_pkt_header(const uint8_t* buffer) -> pkt_header_t
{
	const uint16_t frame_num = (buffer[0] << 8) | buffer[1];
	const uint32_t padding   = (buffer[2] >> 7) & 0x01;
	const uint32_t seqnum    = (buffer[2] >> 0) & 0x7F;
	const uint32_t offset    = (buffer[3] << 16) | (buffer[4] << 8) | buffer[5];
	return {frame_num, padding, seqnum, offset};
}

auto get_pkt_footer(const uint8_t* buffer, int offset) -> pkt_footer_t
{
	return *reinterpret_cast<const pkt_footer_t*>(buffer + offset - sizeof(pkt_footer_t));
}

auto calculate_slice_bitmask(uint64_t pkt_bitmask) -> uint32_t
{
	constexpr auto pkts_in_slice_mask = (1U << num_pkts_per_slice) - 1U;
	auto slice_bitmask = 0U;
	for (auto i = 0; i < config::num_slices; i++)
	{
		const auto match = (pkt_bitmask & pkts_in_slice_mask) == pkts_in_slice_mask;
		slice_bitmask |= (match << i);
		pkt_bitmask >>= num_pkts_per_slice;
	}
	return slice_bitmask;
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

	stream_t(
		const std::vector<server_t>& server_addr,
		std::vector<std::vector<uint8_t>>* frame_buffers);

	auto start(const std::vector<render_command_t>& cmds) -> std::future<void>;
	auto stop() -> result_t;
 
private:
	std::vector<std::vector<uint8_t>>* frame_buffers; 

	int sock {};
	std::vector<sockaddr_in> server_addrs;
	sockaddr_in client_addr;
	std::jthread recv_thread;
	std::atomic_flag drop_incoming_pkts;
	std::atomic_flag stats_not_ready;
	std::array<uint8_t, config::pkt_buffer_size> pkt_buffer;
	std::unordered_map<uint32_t, int> server_id_map;
	result_t result {};
	uint32_t complete_stream_bitmask {};
	std::promise<void> ready_promise;

	auto send_render_command(const render_command_t& cmd, int server_id) -> int;
	auto recv_pkt() -> int;
	auto recv_thread_task() -> void;
};

stream_t::~stream_t()
{
	close(sock);
}

stream_t::stream_t(
	const std::vector<server_t>& servers,
	std::vector<std::vector<uint8_t>>* frame_buffers)
	: frame_buffers {frame_buffers}
{
	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) throw std::runtime_error {"Failed to create stream socket!"};

	//const auto recv_timeout = timeval { .tv_usec = timeout_us, };
	//setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));

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

	this->frame_buffers = frame_buffers;
	drop_incoming_pkts.test_and_set();
	stats_not_ready.test_and_set();

	cpu_set_t cpu_set;
	CPU_ZERO(&cpu_set);
	CPU_SET(1, &cpu_set);
	recv_thread = std::jthread {[this](){ recv_thread_task(); }};
	if (pthread_setaffinity_np(recv_thread.native_handle(), sizeof(cpu_set), &cpu_set) != 0)
	{
		std::cerr << "Failed to set recv-thread affinity!\n";
	}
}

auto stream_t::start(const std::vector<render_command_t>& cmds) -> std::future<void>
{
	complete_stream_bitmask = 0U; // Reset
	result.stream_bitmask = all_stream_bitmask;
	for (auto i = 0; i < cmds.size(); i++) send_render_command(cmds[i], i);
	drop_incoming_pkts.clear();
	return ready_promise.get_future();
}

auto stream_t::stop() -> result_t
{
	drop_incoming_pkts.test_and_set();
	ready_promise = {};

	// Mark missing streams
	for (auto i = 0; i < config::num_streams; i++)
	{
		if (result.stats[i].pkt_bitmask == 0) result.stream_bitmask &= ~(1U << i);
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
	const auto server_ip = ntohl(server_addr.sin_addr.s_addr);

	if (nbytes < pkt_buffer.size())
	{
		std::cerr << "Failed to recv frame packet!\n";
		return -1;
	}

	return server_id_map.at(server_ip);
}

auto stream_t::recv_thread_task() -> void
{
	// TODO: Handle graceful exit
	for (;;)
	{
		const auto server_id = recv_pkt();
		if (drop_incoming_pkts.test() || server_id < 0) continue;

		const auto header = unpack_pkt_header(pkt_buffer.data());
		const auto footer = get_pkt_footer(pkt_buffer.data(), pkt_buffer.size());

		const auto payload_size = pkt_buffer.size() - sizeof(pkt_header_t) - header.padding * footer.padding_len;
		std::copy_n(pkt_buffer.data() + sizeof(pkt_header_t), payload_size, (*frame_buffers)[server_id].data() + header.offset);

		result.stats[server_id].pkt_bitmask |= (1ULL << header.seqnum);
		if (header.seqnum == last_seqnum && result.stats[server_id].pkt_bitmask == all_pkt_bitmask)
		{
			const auto pose_recv_timestamp = get_timestamp_ns();
			const auto pose_rtt_ns = pose_recv_timestamp - footer.timestamp;

			result.stats[server_id].pose_rtt_ns    = pose_rtt_ns;
			result.stats[server_id].render_time_us = footer.render_time_us;
			result.stats[server_id].stream_time_us = footer.stream_time_us;

			complete_stream_bitmask |= (1U << server_id);
		}

		// Early exit when all frames are received
		if (complete_stream_bitmask == all_stream_bitmask) ready_promise.set_value();
	}
}

