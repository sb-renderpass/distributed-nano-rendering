#pragma once

#include <cstdint>

namespace codec
{

constexpr auto stream_end_symbol = 0xFF;

auto encode_slice(const uint8_t* in_buffer, uint8_t* enc_buffer, int width, int height) -> int
{
	auto dst_ptr = enc_buffer;
	for (auto i = 0; i < height; i++)
	{
		// Reset RLE for every row
		auto run_val = 0;
		auto run_len = 0;

		for (auto j = 0; j < width; j++)
		{
			const auto value = *in_buffer++;
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

auto decode_slice(const uint8_t* enc_buffer, uint8_t* out_buffer) -> int
{
	auto src_ptr = enc_buffer;
	auto dst_ptr = out_buffer;
	for (;;)
	{
		const auto run_val = *src_ptr++;
		const auto run_len = *src_ptr++;
		if (run_val == stream_end_symbol && run_len == stream_end_symbol) break;
		for (auto i = 0; i < run_len; i++) *dst_ptr++ = run_val;
	}
	//const auto t = dst_ptr - out_buffer; if (t != 19200) std::clog << t << '\n';
	return src_ptr - enc_buffer;
}

} // namespace codec
