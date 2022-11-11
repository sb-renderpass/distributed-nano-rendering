#pragma once

#include <cstdint>
#include <cstring>
#include <ostream>

namespace protocol
{

struct pkt_info_t
{
	uint8_t slice_end : 1;
	uint8_t has_data  : 1;
	uint8_t reserved  : 2;
	uint8_t slice_id  : 4;	// max 16 slices per frame
	uint8_t pkt_id {0};		// max 256 packets per slice
};

auto read(const uint8_t* buffer, pkt_info_t& obj) -> uint8_t*
{
	obj.slice_end = (buffer[0] >> 7) & 1;
	obj.has_data  = (buffer[0] >> 6) & 1;
	obj.slice_id  = (buffer[0] & 0x0F);
	obj.pkt_id    = (buffer[1] & 0xFF);
	return const_cast<uint8_t*>(buffer) + sizeof(obj);
}

auto write(const pkt_info_t& obj, uint8_t* buffer) -> uint8_t*
{
	*buffer++ = ((obj.slice_end & 1) << 7) | ((obj.has_data & 1) << 6) | (obj.slice_id & 0x0F);
	*buffer++ = obj.pkt_id & 0xFF;
	return buffer;
}

auto operator<< (std::ostream& os, const pkt_info_t& obj) -> std::ostream&
{
	os
		<< static_cast<int>(obj.slice_end)	<< ' '
		<< static_cast<int>(obj.has_data)	<< ' '
		<< static_cast<int>(obj.slice_id)	<< ' '
		<< static_cast<int>(obj.pkt_id);
	return os;
}

auto write_pkt_info(
	int slice_end,
	int has_data,
	int slice_id,
	int pkt_id,
	uint8_t* buffer) -> uint8_t*
{
	*buffer++ = ((slice_end & 1) << 7) | ((has_data & 1) << 6) | (slice_id & 0x0F);
	*buffer++ = pkt_id & 0xFF;
	return buffer;
}

struct frame_info_t
{
	uint64_t timestamp      {0};
	uint32_t render_time_us {0};
	uint32_t stream_time_us {0};
};

auto read(const uint8_t* buffer, frame_info_t& obj) -> uint8_t*
{
	std::memcpy(&obj, buffer, sizeof(obj));
	return const_cast<uint8_t*>(buffer) + sizeof(obj);
}

auto write(const frame_info_t& obj, uint8_t* buffer) -> uint8_t*
{
	std::memcpy(buffer, &obj, sizeof(obj));
	return buffer + sizeof(obj);
}

struct payload_t
{
	// TODO: Use C++17 std::span
	uint8_t* data {nullptr};
	int size {0};
};

auto write(const payload_t& obj, uint8_t* buffer) -> uint8_t*
{
	std::memcpy(buffer, obj.data, obj.size);
	return buffer + obj.size;
}

auto read_payload(const uint8_t* buffer, int size, uint8_t* data) -> uint8_t*
{
	std::memcpy(data, buffer, size);
	return const_cast<uint8_t*>(buffer) + size;
}

auto write_payload(const uint8_t* data, int size, uint8_t* buffer) -> uint8_t*
{
	std::memcpy(buffer, data, size);
	return buffer + size;
}

} // namespace protocol

