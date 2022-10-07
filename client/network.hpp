#pragma once

#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring> // memset
#include <iostream>
#include <optional>
#include <tuple>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "glm/vec2.hpp"
#include <fmt/core.h>
#include <fmt/color.h>

#include "config.hpp"

constexpr auto frame_buffer_width  = config::width;
constexpr auto frame_buffer_height = config::height;
constexpr auto frame_buffer_size   = frame_buffer_width * frame_buffer_height;
constexpr auto slice_buffer_size   = frame_buffer_size / config::num_slices;

constexpr auto num_pkts_per_slice  = static_cast<int>(std::ceil(static_cast<float>(slice_buffer_size) / config::pkt_buffer_size));
constexpr auto num_pkts_per_frame  = num_pkts_per_slice * config::num_slices;
constexpr auto last_seqnum         = num_pkts_per_frame - 1;
constexpr auto last_pkt_bitmask    = (1ULL << last_seqnum);
constexpr auto all_pkt_bitmask     = (1ULL << num_pkts_per_frame) - 1ULL;

constexpr auto target_frame_time   = 1.0F / config::target_fps;
constexpr auto timeout_us          = static_cast<int64_t>(target_frame_time * 1e6F);

auto get_timestamp_ns() -> uint64_t
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::high_resolution_clock::now().time_since_epoch()).count();
}

struct pose_t
{
	uint64_t  timestamp {0};
	glm::vec2 position  {0, 0};
	glm::vec2 direction {0, 0};
	glm::vec2 cam_plane {0, 0};
};

struct tile_t
{
	glm::ivec2 offset {0, 0};
	glm::ivec2 dims   {0, 0};
};

using pkt_header_t = uint32_t;

auto unpack_pkt_header(pkt_header_t header)
{
	const auto padding = (header >> 31) & 0x01;
	const auto seqnum  = (header >> 24) & 0x7F;
	const auto offset  = (header >>  0) & 0xFFFFFF;
	return std::make_tuple(padding, seqnum, offset);
}

auto get_pkt_header(const uint8_t* buffer) -> pkt_header_t
{
	return ntohl(*reinterpret_cast<const pkt_header_t*>(buffer));
}

struct pkt_footer_t
{
	uint32_t render_time_us {0};
	uint32_t stream_time_us {0};
	uint64_t timestamp      {0};
	uint16_t padding_len    {0};
} __attribute__((__packed__));

auto get_pkt_footer(const uint8_t* buffer, int offset) -> pkt_footer_t
{
	return *reinterpret_cast<const pkt_footer_t*>(buffer + offset - sizeof(pkt_footer_t));
}

class stream_t
{
public:
	struct stats_t
	{
		uint64_t pkt_bitmask    {0};
		uint64_t pose_rtt_ns    {0};
		uint32_t render_time_us {0};
		uint32_t stream_time_us {0};
	};

	auto initialize(const char* server_ip, int server_port) -> int;
	auto shutdown() -> void;

	auto render(const pose_t& pose, uint8_t* frame_buffer) -> std::optional<stats_t>;

private:
	int sock {0};
	sockaddr_in server_addr;
	sockaddr_in client_addr;

	std::array<uint8_t, config::pkt_buffer_size> pkt_buffer;

	auto send_pose(const pose_t& pose) const -> int;
	auto recv_pkt() -> int;
};

auto stream_t::initialize(const char* server_ip, int server_port) -> int
{
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
	{
		std::cerr << "Failed to create socket!\n";
		return -1;
	}

	const auto recv_timeout = timeval { .tv_usec = timeout_us, };
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));

	//const auto value = 1;
	//setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &value, sizeof(value));

	std::memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family      = AF_INET;
	server_addr.sin_addr.s_addr = inet_addr(server_ip);
	server_addr.sin_port        = htons(server_port);

	std::memset(&client_addr, 0, sizeof(client_addr));
	client_addr.sin_family      = AF_INET;
	client_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	client_addr.sin_port        = htons(0);

	if (bind(sock, reinterpret_cast<const sockaddr*>(&client_addr), sizeof(client_addr)) < 0)
	{
		std::cerr << "Failed to bind socket!\n";
		return -1;
	}

	return 0;
}

auto stream_t::shutdown() -> void
{
	close(sock);
	sock = 0;
}

auto stream_t::render(const pose_t& pose, uint8_t* frame_buffer) -> std::optional<stats_t>
{
	send_pose(pose);

	stats_t stats;

	while (true)
	{
		if (recv_pkt() < 0) break;

		const auto header = get_pkt_header(pkt_buffer.data());
		const auto [padding, seqnum, offset] = unpack_pkt_header(header);

		const auto footer = get_pkt_footer(pkt_buffer.data(), pkt_buffer.size());

		const auto payload_size = pkt_buffer.size() - sizeof(pkt_header_t) - padding * footer.padding_len;
		std::copy_n(pkt_buffer.data() + sizeof(pkt_header_t), payload_size, frame_buffer + offset);

		stats.pkt_bitmask |= (1ULL << seqnum);

		if (stats.pkt_bitmask == all_pkt_bitmask)
		{
			const auto pose_recv_timestamp = get_timestamp_ns();
			const auto pose_rtt_ns = pose_recv_timestamp - footer.timestamp;

			stats.pose_rtt_ns    = pose_rtt_ns;
			stats.render_time_us = footer.render_time_us;
			stats.stream_time_us = footer.stream_time_us;

			return stats;
		}
	}
	return std::nullopt;
}

auto stream_t::send_pose(const pose_t& pose) const -> int
{
	const auto nbytes = sendto(sock, &pose, sizeof(pose), 0,
		reinterpret_cast<const sockaddr*>(&server_addr), sizeof(server_addr));
	if (nbytes < 0)
	{
		std::cerr << "Failed to send pose!\n";
	}
	return nbytes;
}

auto stream_t::recv_pkt() -> int
{
	const auto nbytes = ::recv(sock, pkt_buffer.data(), pkt_buffer.size(), 0);
	if (nbytes < 0)
	{
		std::cerr << "Failed to recv frame packet!\n";
	}
	return nbytes;
}

