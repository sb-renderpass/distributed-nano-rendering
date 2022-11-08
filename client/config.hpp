#pragma once

#include <vector>

#include "types.hpp"

inline constexpr auto create_ip(int a, int b, int c, int d) -> uint32_t
{
	return (a << 24) | (b << 16) | (c << 8) | d;
}

namespace config
{

constexpr auto name				= "ESP32 Remote Render";
constexpr auto width			= 320;
constexpr auto height			= 240;
constexpr auto fov				= 60;
constexpr auto target_fps		= 30;
constexpr auto render_scale_w	= 2;
constexpr auto render_scale_h	= 2;

constexpr auto pkt_buffer_size	= 1440;
constexpr auto num_slices		= 4;
constexpr auto num_streams		= 2;

constexpr auto sprint_speed		= 0.1F;
constexpr auto strafe_speed		= 0.1F;
constexpr auto rotate_speed		= 0.05F;

const std::vector<server_t> servers
{{
	{create_ip(192, 168, 12, 180), 3333},
	{create_ip(192, 168, 12,  82), 3333},
}};

} // namespace config

