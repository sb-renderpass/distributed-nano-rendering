#pragma once

#include <algorithm>
#include <array>

#include "config.hpp"
#include "bitstream.hpp"

namespace codec
{

inline auto split_rgb233(uint8_t x) -> std::array<uint8_t, 3>
{
	return
	{
		static_cast<uint8_t>((x >> 6) & 0b011),
		static_cast<uint8_t>((x >> 3) & 0b111),
		static_cast<uint8_t>((x >> 0) & 0b111),
	};
}

inline auto join_rgb233(const std::array<uint8_t, 3>& x) -> uint8_t
{
	return
		((x[0] & 0b011) << 6) |
		((x[1] & 0b111) << 3) |
		((x[2] & 0b111) << 0);
}

inline auto zigzag_encode(int x)
{
	return (x >> 31) ^ (x << 1);
}

inline auto zigzag_decode(int x)
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

auto encode_slice(const uint8_t* slice_buffer, bitstream_t& bitstream) -> void
{
	constexpr auto W = config::height;
	constexpr auto H = config::width / config::num_slices;

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
				const auto pred = fixed_prediction(a_split[i], b_split[i], c_split[i]);
				const auto resd = x_split[i] - pred;
				const auto num_enc_bits = zigzag_encode(resd) + 1;
				bitstream.write(1, num_enc_bits);
			}
		}
	}
}

auto decode_slice(bitstream_t& bitstream, uint8_t* slice_buffer) -> void
{
	constexpr auto W = config::height;
	constexpr auto H = config::width / config::num_slices;

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
				const auto pred = fixed_prediction(a_split[i], b_split[i], c_split[i]);

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

} // namespace codec
