#pragma once

#include <cstdint>

struct pose_t
{
    uint64_t ts   {0};
    uint16_t num  {0};
    float pos_x   {0};
    float pos_y   {0};
    float dir_x   {0};
    float dir_y   {0};
    float plane_x {0};
    float plane_y {0};
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

struct pkt_header_t
{
	uint16_t frame_num    {0};
	uint32_t padding :  1 {0};
	uint32_t seqnum  :  7 {0};
	uint32_t offset  : 24 {0};
};

struct encoded_slice_t
{
    int width  {0};
    int height {0};
    int size   {0};
    uint8_t* buffer {nullptr};
};
