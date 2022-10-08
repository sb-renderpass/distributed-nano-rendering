#pragma once

#include <cstdint>

struct pose_t
{
    uint64_t ts   {0};
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

struct frame_t
{
    int width  {0};
    int height {0};
    uint8_t* buffer {nullptr};
};

