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

#include "bitstream.hpp"
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

static std::vector<uint8_t> prev_fb (frame_buffer_size,  0);

auto test_calculate_motion_vector(int slice_index,  uint8_t* fb) -> int
{
	constexpr auto W = config::height;
	constexpr auto H = config::width / config::num_slices;
	const auto slice_offset = slice_buffer_size * slice_index;

	auto mvec_sad = std::numeric_limits<int>::max();
	auto mvec = 0;

	constexpr auto K_W = 32;
	constexpr auto K_H = 16;

	const auto ref_i = H/2;
	const auto ref_j = W/2;

	const auto i_min = 0 + K_H/2;
	const auto i_max = H - K_H/2;

	for (auto i = i_min, j = ref_j; i < i_max; i++)
	{
		auto sad = 0;
		for (auto ii = -K_H/2; ii < K_H/2; ii++)
		{
			for (auto jj = -K_W/2; jj < K_W/2; jj++)
			{
				sad += std::abs(fb[slice_offset + (ref_j + jj) + (ref_i + ii) * W] - prev_fb[slice_offset + (j + jj) + (i + ii) * W]);
				//if (slice_index == 0) fb[slice_offset + (j + jj) + (i + ii) * W] = 0;
				//if (slice_index == 0) fb[slice_offset + (ref_j + jj) + (ref_i + ii) * W] = 0xFF;
			}
		}
		if (sad < mvec_sad)
		{
			mvec_sad = sad;
			mvec = i;
		}
	}
	mvec -= ref_i;
	return mvec;
}

auto get_ch(uint8_t c)
{
	return (c >> 3) & 0b111;
}

auto set_ch(uint8_t c)
{
	return (c & 0b111) << 3;
}

auto split_rgb233(uint8_t x) -> std::array<uint8_t, 3>
{
	return
	{
		static_cast<uint8_t>((x >> 6) & 0b011),
		static_cast<uint8_t>((x >> 3) & 0b111),
		static_cast<uint8_t>((x >> 0) & 0b111),
	};
}

auto join_rgb233(const std::array<uint8_t, 3>& x) -> uint8_t
{
	return
		((x[0] & 0b011) << 6) |
		((x[1] & 0b111) << 3) |
		((x[2] & 0b111) << 0);
}

auto zigzag_encode(int x)
{
	return (x >> 31) ^ (x << 1);
}

auto zigzag_decode(int x)
{
	return (x >> 1) ^ -(x & 1);
}

auto fixed_prediction(int a, int b, int c) -> int
{
	const auto [min_a_b, max_a_b] = std::minmax(a, b);
	auto pred = a + b - c;
	if (c >= max_a_b) return min_a_b;
	else if (c <= min_a_b) return max_a_b;
	return a + b - c;
}

auto test_slice_encode(int slice_index, uint8_t* fb) -> bitstream_t
{
	//const auto mvec = test_calculate_motion_vector(slice_index, fb);
	//std::clog << "V " << mvec << '\n';

	bitstream_t bitstream;

	constexpr auto W = config::height;
	constexpr auto H = config::width / config::num_slices;
	const auto slice_offset = slice_buffer_size * slice_index;

	for (auto i = 0; i < H; i++)
	{
		for (auto j = 0; j < W; j++)
		{
			//if (slice_index == 0) fb[slice_offset + j + i * W] = 0xFF;

			// I-frame encoding
			const auto c = (i >  0 && j >  0) ? fb[slice_offset + (j - 1) + (i - 1) * W] : 0;
			const auto b = (i >  0 && j > -1) ? fb[slice_offset + (j + 0) + (i - 1) * W] : 0;
			const auto a = (i > -1 && j >  0) ? fb[slice_offset + (j - 1) + (i + 0) * W] : 0;
			const auto x = fb[slice_offset + j + i * W];

			/*
			// P-frame encoding
			// TODO: Fix indices (i, j)
			const int prev_c = (j >  0 && i+mvec >   0) ? prev_fb[slice_offset + (j - 1) + (i+mvec - 1) * W] : 0;
			const int prev_b = (j >  0 && i+mvec >  -1) ? prev_fb[slice_offset + (j - 1) + (i+mvec + 0) * W] : 0;
			const int prev_d = (j >  0 && i+mvec < H-1) ? prev_fb[slice_offset + (j - 1) + (i+mvec + 1) * W] : 0;
			const int prev_a = (j > -1 && i+mvec >   0) ? prev_fb[slice_offset + (j + 0) + (i+mvec - 1) * W] : 0;
			const int prev_x = (i+mvec > -1) ? prev_fb[slice_offset + j + (i+mvec) * W] : 0;

			const int curr_c = (j >  0 && i >   0) ? fb[slice_offset + (j - 1) + (i - 1) * W] : 0;
			const int curr_b = (j >  0 && i >  -1) ? fb[slice_offset + (j - 1) + (i + 0) * W] : 0;
			const int curr_d = (j >  0 && i < H-1) ? fb[slice_offset + (j - 1) + (i + 1) * W] : 0;
			const int curr_a = (j > -1 && i >   0) ? fb[slice_offset + (j + 0) + (i - 1) * W] : 0;
			const int curr_x = fb[slice_offset + j + i * W];

			const int c = curr_c - prev_c;
			const int b = curr_b - prev_b;
			const int d = curr_d - prev_d;
			const int a = curr_a - prev_a;
			const int x = curr_x - prev_x;
			*/

			const auto c_split = split_rgb233(c);
			const auto b_split = split_rgb233(b);
			const auto a_split = split_rgb233(a);
			const auto x_split = split_rgb233(x);

			for (auto i = 0; i < 3; i++)
			{
				const auto pred = fixed_prediction(a_split[i], b_split[i], c_split[i]);
				const auto resd = x_split[i] - pred;
				const auto num_enc_bits = zigzag_encode(resd) + 1;

				//if (i == 0 && j <= 8) std::clog << "(" << i << ',' << j << ") " << ' ' << num_enc_bits << ' ' << resd << ' ' << x << '\n';

				for (auto k = 0; k < num_enc_bits; k++)
				{
					constexpr auto encoded = 1;
					bitstream.write(encoded >> (num_enc_bits - k - 1));
				}
			}
		}
	}

	return bitstream;
}

auto test_slice_decode(bitstream_t& bitstream) -> std::vector<uint8_t>
{
	std::vector<uint8_t> result (slice_buffer_size, 0);

	constexpr auto W = config::height;
	constexpr auto H = config::width / config::num_slices;

	for (auto i = 0; i < H; i++)
	{
		for (auto j = 0; j < W; j++)
		{
			const auto c = (i >  0 && j >  0) ? result[(j - 1) + (i - 1) * W] : 0;
			const auto b = (i >  0 && j > -1) ? result[(j + 0) + (i - 1) * W] : 0;
			const auto a = (i > -1 && j >  0) ? result[(j - 1) + (i + 0) * W] : 0;

			const auto c_split = split_rgb233(c);
			const auto b_split = split_rgb233(b);
			const auto a_split = split_rgb233(a);

			std::array<uint8_t, 3> channels {};

			for (auto i = 0; i < 3; i++)
			{
				const auto pred = fixed_prediction(a_split[i], b_split[i], c_split[i]);

				auto num_zero_bits = 0;
				while (bitstream.read() == 0)
				{
					num_zero_bits++;
				}
				const auto resd = zigzag_decode(num_zero_bits);

				const auto x = pred + resd;
				channels[i] = x;

				//std::clog << "(" << i << ',' << j << ") " << ' ' << (num_zero_bits + 1) << ' ' << resd << ' ' << x << '\n';
				//if (j == 8) std::exit(0);
			}
			result[j + i * W] = join_rgb233(channels);
		}
	}

	return result;
}

auto test_same(int slice_index, const uint8_t* fb, const std::vector<uint8_t>& decoded) -> void
{
	const auto slice_offset = slice_buffer_size * slice_index;
	for (auto i = 0; i < slice_buffer_size; i++)
	{
		const auto x1 = fb[slice_offset + i];
		const auto x2 = decoded[i];
		if (x1 != x2)
		{
			std::clog << "!!! " << i << ' ' << (int)x1 << ' ' << (int)x2 << '\n';
			throw;
		}
	}
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

			// CODEC
			auto num_total_bytes = 0;
			for (auto i = 0; i < config::num_slices; i++)
			{
				auto bitstream = test_slice_encode(i, (*frame_buffers)[server_id].data());
				bitstream.write_flush();
				const auto cr = (float)bitstream.buffer.size() / slice_buffer_size;
				std::clog << i << ' ' << cr << '\n';

				num_total_bytes += bitstream.buffer.size();

				//std::clog << "~~~~~\n";
				const auto decoded = test_slice_decode(bitstream);
				test_same(i, (*frame_buffers)[server_id].data(), decoded);
			}
			const auto cr = (float)num_total_bytes / frame_buffer_size;
			std::clog << "= " << cr << '\n';
			std::clog << "----------\n";

            std::memcpy(prev_fb.data(), (*frame_buffers)[server_id].data(), frame_buffer_size);
		}

		// Early exit when all frames are received
		if (complete_stream_bitmask == all_stream_bitmask) ready_promise.set_value();
	}
}
