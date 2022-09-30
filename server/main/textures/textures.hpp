#pragma once

#include <cstdint>

#include "eagle.hpp"
#include "redbrick.hpp"
#include "purplestone.hpp"
#include "greystone.hpp"
#include "bluestone.hpp"
#include "mossy.hpp"
#include "wood.hpp"
#include "colorstone.hpp"

constexpr auto num_textures   =  8;
constexpr auto texture_width  = 64;
constexpr auto texture_height = 64;

constexpr const uint8_t* texture_map[num_textures] = {
    textures::eagle,
    textures::redbrick,
    textures::purplestone,
    textures::greystone,
    textures::bluestone,
    textures::mossy,
    textures::wood,
    textures::colorstone,
};

