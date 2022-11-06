#pragma once

#include <cassert>
#include <vector>
#include <stdexcept>

class bitstream_t
{
public:

	~bitstream_t() = default;

	bitstream_t(uint8_t* buffer, int capacity)
	: buffer {buffer}, capacity {capacity}
	{
	}

	auto write(uint32_t value, int count) -> void
	{
		// TODO: throw bitstream overflow if it attempts to exceed capacity
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
		// TODO: throw bitstream overflow if it attempts to exceed capacity
		write_cache = (write_cache << 1) | (bit & 1);
		num_bits++;
		num_write_cache_bits++;
		if (num_write_cache_bits == max_write_cache_bits) flush_write_cache();
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
		if (num_bits == 0) throw std::runtime_error {"bitstream underflow"};
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
		num_bits = 0;

		write_cache = 0;
		write_buffer_pos = 0;
		num_write_cache_bits = 0;

		read_cache = 0;
		read_buffer_pos = 0;
		num_read_cache_bits = max_read_cache_bits;
	}

	auto size() const { return num_bits; }

	auto size_bytes() const { return write_buffer_pos - read_buffer_pos; }

private:

	auto flush_write_cache() -> void
	{
		//buffer.push_back(write_cache);
		buffer[write_buffer_pos + 0] = (write_cache >> 24) & 0xFF;
		buffer[write_buffer_pos + 1] = (write_cache >> 16) & 0xFF;
		buffer[write_buffer_pos + 2] = (write_cache >>  8) & 0xFF;
		buffer[write_buffer_pos + 3] = (write_cache >>  0) & 0xFF;
		write_cache = 0;
		write_buffer_pos += 4;
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

	uint8_t* buffer {nullptr};
	int capacity {0};
	int num_bits {0};

	uint32_t write_cache {0};
	int write_buffer_pos {0};
	int num_write_cache_bits {0};

	uint64_t read_cache {0};
	int read_buffer_pos {0};
	int num_read_cache_bits {max_read_cache_bits};

	static constexpr auto max_write_cache_bits = 8 * sizeof(write_cache);
	static constexpr auto max_read_cache_bits  = 4 * sizeof(read_cache); // Half-size
};

