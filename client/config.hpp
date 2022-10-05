#pragma once

#include <array>

namespace config
{

constexpr auto name				= "ESP32 Remote Render";
constexpr auto width			= 320;
constexpr auto height			= 240;
constexpr auto fov				= 60;
constexpr auto target_fps		= 30;
constexpr auto render_scale		= 2;

constexpr auto pkt_buffer_size	= 1400;
constexpr auto num_slices		= 8;
constexpr auto num_streams		= 1;

constexpr auto sprint_speed		= 0.1F;
constexpr auto strafe_speed		= 0.1F;
constexpr auto rotate_speed		= 0.05F;

struct server_addr_t
{
	char ip[16] = "255.255.255.255";
	int port {3333};
};

constexpr std::array<server_addr_t, 2> server_addr
{{
	{"192.168.12.180",	3333},
	{"127.0.0.1",		3333},
}};

} // namespace config

