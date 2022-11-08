#pragma once

#include <cstdint>

#include "glm/vec2.hpp"

struct server_t
{
	uint32_t ip   {};
	uint32_t port {};
};

struct pose_t
{
	uint64_t  timestamp {0};
	uint16_t  frame_num {0};
	glm::vec2 position  {0, 0};
	glm::vec2 direction {0, 0};
	glm::vec2 cam_plane {0, 0};
};

struct tile_t
{
	float x_scale  {+2};
	float x_offset {-1};
};

struct render_command_t
{
	pose_t pose;
	tile_t tile;
};

struct pkt_info_t
{
	uint8_t frame_end : 1 {0};
	uint8_t slice_end : 1 {0};
	uint8_t reserved  : 2 {0};
	uint8_t slice_id  : 4 {0}; // max 16 slices per frame
	uint8_t pkt_id        {0}; // max 256 packets per slice
};

struct slice_info_t
{
	uint8_t num_pkts {0};
};

struct frame_info_t
{
	uint32_t render_time_us {0};
	uint32_t stream_time_us {0};
	uint64_t timestamp      {0};
} __attribute__((__packed__));

struct stats_t
{
	uint64_t pose_rtt_ns    {0};
	uint32_t render_time_us {0};
	uint32_t stream_time_us {0};
	uint32_t slice_bitmask  {0};
	uint32_t num_enc_bytes  {0};
};
