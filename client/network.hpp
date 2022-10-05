#pragma once

#include <cstring> // memset
#include <iostream>
#include <tuple>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "glm/vec2.hpp"

struct pose_t
{
	uint64_t  timestamp {0};
	glm::vec2 position  {0, 0};
	glm::vec2 direction {0, 0};
	glm::vec2 cam_plane {0, 0};
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
	auto initialize(
		const char* server_ip, int server_port, int64_t timeout_us) -> int;
	auto shutdown() -> void;

	auto send(const uint8_t* data, int size) const -> int;
	auto recv(      uint8_t* data, int size) const -> int;

//private:
	int sock {0};
	sockaddr_in server_addr;
	sockaddr_in client_addr;
};

auto stream_t::initialize(
	const char* server_ip, int server_port, int64_t timeout_us) -> int
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

auto stream_t::send(const uint8_t* data, int size) const -> int
{
	const auto nbytes = sendto(sock, data, size, 0,
		reinterpret_cast<const sockaddr*>(&server_addr),
		sizeof(server_addr));
	if (nbytes < 0)
	{
		std::cerr << "Failed to send data!\n";
	}
	return nbytes;
}

auto stream_t::recv(uint8_t* data, int size) const -> int
{
	const auto nbytes = ::recv(sock, data, size, 0);
	if (nbytes < 0)
	{
		std::cerr << "Failed to recv frame!\n";
	}
	return nbytes;
}

