#pragma once

#include <algorithm>
#include <iostream>
#include <array>

#include "bitstream.hpp"

namespace codec
{

constexpr auto stream_end_symbol = 0xFF;

using pixel_t = std::array<uint8_t, 3>;

__attribute__((always_inline))
inline auto split_rgb233(uint8_t x) -> pixel_t
{
	return
	{
		static_cast<uint8_t>((x >> 6) & 0b011),
		static_cast<uint8_t>((x >> 3) & 0b111),
		static_cast<uint8_t>((x >> 0) & 0b111),
	};
}

__attribute__((always_inline))
inline auto join_rgb233(const pixel_t& x) -> uint8_t
{
	return
		((x[0] & 0b011) << 6) |
		((x[1] & 0b111) << 3) |
		((x[2] & 0b111) << 0);
}

__attribute__((always_inline))
inline auto zigzag_encode(int x)
{
	return (x >> 31) ^ (x << 1);
}

__attribute__((always_inline))
inline auto zigzag_decode(int x)
{
	return (x >> 1) ^ -(x & 1);
}

__attribute__((always_inline))
inline auto predict(int a, int b, int c) -> int
{
	const auto [min_a_b, max_a_b] = std::minmax(a, b);
	if (c >= max_a_b) return min_a_b;
	else if (c <= min_a_b) return max_a_b;
	return a + b - c;
}

__attribute__((always_inline))
inline auto encode(uint8_t r, uint8_t g, uint8_t b, uint8_t x) -> int
{
	return zigzag_encode(x - predict(r, g, b)) + 1;
}

auto encode_slice(const uint8_t* slice_buffer, uint8_t* enc_buffer, int W, int H) -> int
{
	auto dst_ptr = enc_buffer;
	for (auto i = 0; i < H; i++)
	{
		auto run_val = 0;
		auto run_len = 0;
		for (auto j = 0; j < W; j++)
		{
			const auto value = *slice_buffer++;
			if (run_len == 0)
			{
				run_val = value;
				run_len = 1;
			}
			else if (value == run_val)
			{
				run_len++;
			}
			else
			{
				*dst_ptr++ = run_val;
				*dst_ptr++ = run_len;
				run_val = value;
				run_len = 1;
			}
		}
		// Add last run
		*dst_ptr++ = run_val;
		*dst_ptr++ = run_len;
	}
	// Terminate stream with special symbol
	*dst_ptr++ = stream_end_symbol;
	*dst_ptr++ = stream_end_symbol;
	return dst_ptr - enc_buffer;
}

auto decode_slice(const uint8_t* enc_buffer, uint8_t* slice_buffer) -> int
{
	auto src_ptr = enc_buffer;
	for (;;)
	{
		const auto run_val = *src_ptr++;
		const auto run_len = *src_ptr++;
		if (run_val == stream_end_symbol && run_len == stream_end_symbol) break;
		for (auto i = 0; i < run_len; i++) *slice_buffer++ = run_val;
	}
	return src_ptr - enc_buffer;
}

#if 0
auto encode_slice(const uint8_t* slice_buffer, bitstream_t& bitstream, int W, int H) -> void
{
	for (auto i = 0; i < H; i++)
	{
		for (auto j = 0; j < W; j++)
		{
			const auto c = (i >  0 && j >  0) ? slice_buffer[(j - 1) + (i - 1) * W] : 0;
			const auto b = (i >  0 && j > -1) ? slice_buffer[(j + 0) + (i - 1) * W] : 0;
			const auto a = (i > -1 && j >  0) ? slice_buffer[(j - 1) + (i + 0) * W] : 0;
			const auto x = slice_buffer[j + i * W];

			const auto c_split = split_rgb233(c);
			const auto b_split = split_rgb233(b);
			const auto a_split = split_rgb233(a);
			const auto x_split = split_rgb233(x);

			for (auto i = 0; i < 3; i++)
			{
				const auto pred = predict(a_split[i], b_split[i], c_split[i]);
				const auto resd = x_split[i] - pred;
				const auto num_enc_bits = zigzag_encode(resd) + 1;
				bitstream.write(1, num_enc_bits);
			}
		}
	}
}
#endif

#if 0
auto encode_slice(const uint8_t* slice_buffer, bitstream_t& bitstream, int W, int H) -> void
{
	auto b_split = pixel_t {0, 0, 0};
	for (auto i = 0; i < H; i++)
	{
		auto a_split = pixel_t {0, 0, 0};
		auto c_split = pixel_t {0, 0, 0};

		for (auto j = 0; j < W; j++)
		{
			const auto x_split = split_rgb233(slice_buffer[j + i * W]);

			auto num_enc_bits = 0;
			auto symbol = 0U;
			for (auto i = 0; i < 3; i++)
			{
				//const auto pred = predict(a_split[i], b_split[i], c_split[i]);
				//const auto resd = x_split[i] - pred;
				//const auto num_enc_bits = zigzag_encode(resd) + 1;
				//bitstream.write(1, num_enc_bits);
				//const auto num_sym_bits = zigzag_encode(resd) + 1;
				const auto num_sym_bits = encode(a_split[i], b_split[i], c_split[i], x_split[i]);
				symbol = (symbol << num_sym_bits) | 1;
				num_enc_bits += num_sym_bits;
			}
			bitstream.write(symbol, num_enc_bits);

			c_split = b_split;
			a_split = x_split;
			b_split = (i > 0 && j < W-1) ? split_rgb233(slice_buffer[(j + 1) + (i - 1) * W]) : pixel_t {0, 0, 0};
		}
		b_split = split_rgb233(slice_buffer[i * W]);
	}
}
#endif

#if 0
auto decode_slice(bitstream_t& bitstream, uint8_t* slice_buffer, int W, int H) -> void
{
	for (auto i = 0; i < H; i++)
	{
		for (auto j = 0; j < W; j++)
		{
			const auto c = (i >  0 && j >  0) ? slice_buffer[(j - 1) + (i - 1) * W] : 0;
			const auto b = (i >  0 && j > -1) ? slice_buffer[(j + 0) + (i - 1) * W] : 0;
			const auto a = (i > -1 && j >  0) ? slice_buffer[(j - 1) + (i + 0) * W] : 0;

			const auto c_split = split_rgb233(c);
			const auto b_split = split_rgb233(b);
			const auto a_split = split_rgb233(a);

			std::array<uint8_t, 3> channels {};
			for (auto i = 0; i < 3; i++)
			{
				const auto pred = predict(a_split[i], b_split[i], c_split[i]);

				auto num_zero_bits = 0;
				while (bitstream.read() == 0)
				{
					num_zero_bits++;
				}
				const auto resd = zigzag_decode(num_zero_bits);

				const auto x = pred + resd;
				channels[i] = x;
			}
			slice_buffer[j + i * W] = join_rgb233(channels);
		}
	}
}
#endif

#if 0
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
#endif

} // namespace codec
