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

struct pkt_header_t
{
	uint16_t frame_num    {0};
	uint32_t padding :  1 {0};
	uint32_t seqnum  :  7 {0};
	uint32_t offset  : 24 {0};
};

struct pkt_footer_t
{
	uint32_t render_time_us {0};
	uint32_t stream_time_us {0};
	uint64_t timestamp      {0};
	uint16_t padding_len    {0};
} __attribute__((__packed__));

struct stats_t
{
	uint64_t pkt_bitmask    {0};
	uint64_t pose_rtt_ns    {0};
	uint32_t render_time_us {0};
	uint32_t stream_time_us {0};
};

