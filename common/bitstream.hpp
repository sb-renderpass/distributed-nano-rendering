#pragma once

#include <vector>
#include <stdexcept>

class bitstream_t
{
public:

	auto reserve(int capacity) { buffer.reserve(capacity); }

	auto write(uint32_t value, int count) -> void
	{
		assert(count <= max_write_cache_bits);
		const auto num_free_bits = max_write_cache_bits - num_write_cache_bits;

		const auto n = std::min<int>(num_free_bits, count);
		const auto r = count - n;

		write_cache = (write_cache << n) | (value >> r);
		num_bits += n;
		num_write_cache_bits += n;

		if (num_write_cache_bits == max_write_cache_bits) flush_write_cache();

		if (r > 0)
		{
			write_cache = (write_cache << r) | ((value << n) >> n);
			num_bits += r;
			num_write_cache_bits += r;
		}
	}

	auto write(int bit) -> void
	{
		if (num_write_cache_bits == max_write_cache_bits) flush_write_cache();
		write_cache = (write_cache << 1) | (bit & 1);
		num_bits++;
		num_write_cache_bits++;
	}

	auto flush() -> void
	{
		const auto num_pad_bits = max_write_cache_bits - num_write_cache_bits;
		write_cache <<= num_pad_bits;
		flush_write_cache();
		num_bits += num_write_cache_bits;
		write_cache = 0;
		num_write_cache_bits++;
	}

	auto read() -> int
	{
#if defined(__cpp_exceptions)
		if (num_bits == 0) throw std::runtime_error {"Empty bitstream!"};
#endif
		if (num_read_cache_bits == max_read_cache_bits) fetch_read_cache();
		read_cache <<= 1;
		const auto bit = (read_cache >> max_read_cache_bits) & 1;
		num_bits--;
		num_read_cache_bits++;
		return bit;
	}

	auto clear() -> void
	{
		buffer.clear();
		num_bits = 0;

		write_cache = 0;
		num_write_cache_bits = 0;

		num_read_cache_bits = max_read_cache_bits;
		read_cache = 0;
		read_buffer_pos = 0;
	}

	auto size() const { return num_bits; }

	auto size_bytes() const { return buffer.size() - read_buffer_pos; }

private:

	auto flush_write_cache() -> void
	{
		//buffer.push_back(write_cache);
		buffer.push_back((write_cache >> 24) & 0xFF);
		buffer.push_back((write_cache >> 16) & 0xFF);
		buffer.push_back((write_cache >>  8) & 0xFF);
		buffer.push_back((write_cache >>  0) & 0xFF);
		write_cache = 0;
		num_write_cache_bits = 0;
	}

	auto fetch_read_cache() -> void
	{
		//read_cache = buffer[read_buffer_pos++];
		read_cache =
			(buffer[read_buffer_pos + 0] << 24) |
			(buffer[read_buffer_pos + 1] << 16) |
			(buffer[read_buffer_pos + 2] <<  8) |
			(buffer[read_buffer_pos + 3] <<  0);
		read_buffer_pos += 4;
		num_read_cache_bits = 0;
	}

	std::vector<uint8_t> buffer {};
	int num_bits {0};

	uint32_t write_cache {0};
	int num_write_cache_bits {0};

	uint64_t read_cache {0};
	int num_read_cache_bits {32};
	int read_buffer_pos {0};

	static constexpr auto max_write_cache_bits = 8 * sizeof(write_cache);
	static constexpr auto max_read_cache_bits  = 4 * sizeof(read_cache); // Half-size
};

