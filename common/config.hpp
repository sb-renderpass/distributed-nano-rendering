#pragma once

namespace config::common
{

constexpr auto screen_width		= 320;
constexpr auto screen_height	= 240;
constexpr auto num_slices		= 4;
constexpr auto pkt_buffer_size	= 1440;

constexpr auto screen_buffer_size = screen_width * screen_height;
constexpr auto slice_buffer_size  = screen_buffer_size / num_slices;

} // namespace config::common

