#pragma once

#include <vector>

struct bitstream_t
{
	std::vector<uint8_t> buffer {};
	int num_bits {0};

	uint8_t write_cache {0};
	int num_write_cache_bits {0};

	uint16_t read_cache {0};
	int num_read_cache_bits {8};
	int read_buffer_pos {0};

	auto write(int bit) -> void
	{
		constexpr auto max_write_cache_bits = sizeof(write_cache) * 8;
		if (num_write_cache_bits == max_write_cache_bits)
		{
			buffer.push_back(write_cache);
			write_cache = 0;
			num_write_cache_bits = 0;
		}
		write_cache = (write_cache << 1) | (bit & 1);
		num_bits++;
		num_write_cache_bits++;
	}

	auto write_flush() -> void
	{
		constexpr auto max_write_cache_bits = sizeof(write_cache) * 8;
		const auto num_pad_bits = max_write_cache_bits - num_write_cache_bits;
		write_cache <<= num_pad_bits;
		buffer.push_back(write_cache);
		num_bits += num_write_cache_bits;
		write_cache = 0;
		num_write_cache_bits++;
	}

	auto read() -> int
	{
		constexpr auto max_read_cache_bits = sizeof(read_cache) * 4;
		if (num_bits == 0) throw std::runtime_error {"Empty bitstream!"};
		if (num_read_cache_bits == max_read_cache_bits)
		{
			read_cache = buffer[read_buffer_pos++];
			num_read_cache_bits = 0;
		}
		read_cache <<= 1;
		const auto bit = (read_cache >> max_read_cache_bits) & 1;
		num_bits--;
		num_read_cache_bits++;
		return bit;
	}
};

