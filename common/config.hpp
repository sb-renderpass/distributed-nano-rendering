#pragma once

#include <cstdint>

constexpr auto make_addr(int a, int b, int c, int d) -> uint32_t
{
	return (a << 24) | (b << 16) | (c << 8) | d;
}

struct server_info_t
{
	uint32_t addr {};
	uint32_t port {};
};

namespace config::common
{

constexpr auto screen_width		= 320;
constexpr auto screen_height	= 240;
constexpr auto num_slices		= 4;
constexpr auto pkt_buffer_size	= 1440;

constexpr auto screen_buffer_size = screen_width * screen_height;
constexpr auto slice_buffer_size  = screen_buffer_size / num_slices;
constexpr auto all_slice_bitmask  = (1U << num_slices ) - 1U;

} // namespace config::common

namespace config::client
{

constexpr auto num_streams	= 2;
constexpr auto stream_port	= 3333;

constexpr server_info_t server_infos[] = {
	{make_addr(192, 168, 12, 180), stream_port},
	{make_addr(192, 168, 12,  82), stream_port},
};

constexpr auto name			= "ESP32 Remote Render";
constexpr auto fov			= 60;
constexpr auto target_fps	= 30;
constexpr auto scale_width	= 2;
constexpr auto scale_height	= 2;

constexpr auto sprint_speed	= 0.1F;
constexpr auto strafe_speed	= 0.1F;
constexpr auto rotate_speed	= 0.05F;

constexpr auto all_stream_bitmask = (1U << num_streams) - 1U;

} // namespace config::client

