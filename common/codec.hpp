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

auto decode_slice(const uint8_t* enc_buffer, uint8_t* slice_buffer, int max_size) -> int
{
	auto src_ptr = enc_buffer;
	for (;;)
	{
		const auto run_val = *src_ptr++;
		const auto run_len = *src_ptr++;
		if (run_val == stream_end_symbol && run_len == stream_end_symbol) break;
		for (auto i = 0; i < run_len; i++)
		{
			if (max_size-- == 0) return src_ptr - enc_buffer; // FIXME: Hack to avoid buffer overflow
			*slice_buffer++ = run_val;
		}
	}
	return src_ptr - enc_buffer;
}

} // namespace codec
