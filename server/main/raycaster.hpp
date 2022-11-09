#pragma once

#include <cmath>

#include "types.hpp"
#include "common/codec.hpp"
#include "textures/textures.hpp"

template <int size_ = 64, int stride_ = size_>
struct texture_cache_t
{
    static constexpr auto size   = size_;
    static constexpr auto stride = stride_;

    int tex_id {-1};
    int row_id {-1};
    uint8_t data[size] = {0};

    auto update(int tex_id_, int row_id_) -> void
    {
        if (tex_id_ != tex_id || row_id_ != row_id)
        {
            std::memcpy(data, texture_map[tex_id_] + row_id_ * stride, size);
            tex_id = tex_id_;
            row_id = row_id_;
        }
    }
};

constexpr auto map_size_x = 24;
constexpr auto map_size_y = 24;

constexpr uint8_t world_map[map_size_x][map_size_y] = {
    {8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4, 4, 6, 4, 4, 6, 4, 6, 4, 4, 4, 6, 4},
    {8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4},
    {8, 0, 3, 3, 0, 0, 0, 0, 0, 8, 8, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6},
    {8, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6},
    {8, 0, 3, 3, 0, 0, 0, 0, 0, 8, 8, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4},
    {8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 4, 0, 0, 0, 0, 0, 6, 6, 6, 0, 6, 4, 6},
    {8, 8, 8, 8, 0, 8, 8, 8, 8, 8, 8, 4, 4, 4, 4, 4, 4, 6, 0, 0, 0, 0, 0, 6},
    {7, 7, 7, 7, 0, 7, 7, 7, 7, 0, 8, 0, 8, 0, 8, 0, 8, 4, 0, 4, 0, 6, 0, 6},
    {7, 7, 0, 0, 0, 0, 0, 0, 7, 8, 0, 8, 0, 8, 0, 8, 8, 6, 0, 0, 0, 0, 0, 6},
    {7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 6, 0, 0, 0, 0, 0, 4},
    {7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 6, 0, 6, 0, 6, 0, 6},
    {7, 7, 0, 0, 0, 0, 0, 0, 7, 8, 0, 8, 0, 8, 0, 8, 8, 6, 4, 6, 0, 6, 6, 6},
    {7, 7, 7, 7, 0, 7, 7, 7, 7, 8, 8, 4, 0, 6, 8, 4, 8, 3, 3, 3, 0, 3, 3, 3},
    {2, 2, 2, 2, 0, 2, 2, 2, 2, 4, 6, 4, 0, 0, 6, 0, 6, 3, 0, 0, 0, 0, 0, 3},
    {2, 2, 0, 0, 0, 0, 0, 2, 2, 4, 0, 0, 0, 0, 0, 0, 4, 3, 0, 0, 0, 0, 0, 3},
    {2, 0, 0, 0, 0, 0, 0, 0, 2, 4, 0, 0, 0, 0, 0, 0, 4, 3, 0, 0, 0, 0, 0, 3},
    {1, 0, 0, 0, 0, 0, 0, 0, 1, 4, 4, 4, 4, 4, 6, 0, 6, 3, 3, 0, 0, 0, 3, 3},
    {2, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 1, 2, 2, 2, 6, 6, 0, 0, 5, 0, 5, 0, 5},
    {2, 2, 0, 0, 0, 0, 0, 2, 2, 2, 0, 0, 0, 2, 2, 0, 5, 0, 5, 0, 0, 0, 5, 5},
    {2, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 2, 5, 0, 5, 0, 5, 0, 5, 0, 5},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5},
    {2, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 2, 5, 0, 5, 0, 5, 0, 5, 0, 5},
    {2, 2, 0, 0, 0, 0, 0, 2, 2, 2, 0, 0, 0, 2, 2, 0, 5, 0, 5, 0, 0, 0, 5, 5},
    {2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 5, 5, 5, 5, 5, 5, 5, 5, 5},
};

uint8_t floor_colors[256];

float zbuffer[320]; // TODO: Paramterize

[[maybe_unused]]
inline constexpr auto gray_to_rgb332(uint8_t x)
{
    const auto msb3 = (x & 0b11100000) >> 5;
    const auto msb2 = (x & 0x11000000) >> 6;
    return (msb3 << 5) | (msb3 << 2) | msb2;
}

[[maybe_unused]]
inline constexpr auto gray_to_rgb233(uint8_t x)
{
    const auto msb3 = (x & 0b11100000) >> 5;
    const auto msb2 = (x & 0x11000000) >> 6;
    return (msb2 << 6) | (msb3 << 3) | msb3;
}

auto init_renderer(int frame_buffer_width, int frame_buffer_height) -> void
{
    constexpr auto base_floor_color = 0x30;
    const auto floor_dist_scale = 1.0F / frame_buffer_height;
    for (auto i = 0; i < frame_buffer_height; i++)
    {
        floor_colors[i] = gray_to_rgb233(base_floor_color - static_cast<uint8_t>(i * floor_dist_scale * base_floor_color));
    }

    for (auto i = 0; i < frame_buffer_width; i++)
    {
        zbuffer[i] = 1e9F;
    }
}

auto render_encode_slice(
    const render_command_t& cmd,
    int slice_start,
    int slice_stop,
    encoded_slice_t& frame) -> void
{
    texture_cache_t<texture_height> tex_cache;

	auto dst_ptr = frame.buffer;

    const auto x_scale = cmd.tile.x_scale / frame.width;
    for (auto x = slice_start, i = 0; x < slice_stop; x++, i++)
    {
        //const float cam_x = static_cast<float>(2 * x) / frame.width - 1.0F;
        const float cam_x = x * x_scale + cmd.tile.x_offset;
        const float ray_dir_x = cmd.pose.dir_x + cmd.pose.plane_x * cam_x;
        const float ray_dir_y = cmd.pose.dir_y + cmd.pose.plane_y * cam_x;

        auto map_x = static_cast<int>(cmd.pose.pos_x);
        auto map_y = static_cast<int>(cmd.pose.pos_y);

        const auto delta_dist_x = (ray_dir_x == 0) ? 1e30F : std::abs(1.0F / ray_dir_x);
        const auto delta_dist_y = (ray_dir_y == 0) ? 1e30F : std::abs(1.0F / ray_dir_y);

        auto step_x = 0;
        auto step_y = 0;

        auto side_dist_x = 0.0F;
        auto side_dist_y = 0.0F;

        if (ray_dir_x < 0)
        {
            step_x = -1;
            side_dist_x = (cmd.pose.pos_x - map_x) * delta_dist_x;
        }
        else
        {
            step_x = 1;
            side_dist_x = (map_x + 1.0F - cmd.pose.pos_x) * delta_dist_x;
        }
        if (ray_dir_y < 0)
        {
            step_y = -1;
            side_dist_y = (cmd.pose.pos_y - map_y) * delta_dist_y;
        }
        else
        {
            step_y = 1;
            side_dist_y = (map_y + 1.0F - cmd.pose.pos_y) * delta_dist_y;
        }

        auto is_front_side = true;
        auto hit = 0;
        while (
			(0 <= map_x) && (map_x < map_size_x) &&
			(0 <= map_y) && (map_y < map_size_y))
        {
            hit = world_map[map_x][map_y];
            if (hit > 0) break;

            if (side_dist_x < side_dist_y)
            {
                side_dist_x += delta_dist_x;
                map_x += step_x;
                is_front_side = false;
            }
            else
            {
                side_dist_y += delta_dist_y;
                map_y += step_y;
                is_front_side = true;
            }
        }

		const auto hit_dist   = std::max(is_front_side ? (side_dist_y - delta_dist_y) : (side_dist_x - delta_dist_x), 0.1F);
		const auto wall_len   = static_cast<int>(frame.height / hit_dist);
		const auto wall_start = std::max((frame.height - wall_len) / 2, 0);
		const auto wall_stop  = std::min((frame.height + wall_len) / 2, frame.height);

		auto tex_u = is_front_side ?
			cmd.pose.pos_x + hit_dist * ray_dir_x :
			cmd.pose.pos_y + hit_dist * ray_dir_y;
		tex_u -= int(tex_u);

		auto tex_x = int(tex_u * texture_width);
		if ((!is_front_side && ray_dir_x > 0) && (is_front_side && ray_dir_y < 0)) tex_x = texture_width - 1 - tex_x;

		const auto tex_id = hit - 1;
		tex_cache.update(tex_id, tex_x);

		const auto tex_v_step = static_cast<float>(texture_height) / wall_len;
		auto tex_v = (wall_start - (frame.height - wall_len) / 2) * tex_v_step;

		auto run_val = 0;
		auto run_len = 0;

        for (auto j = 0; j < frame.height; j++)
		{
			auto color = 0;
			if (j < wall_start)
			{
                constexpr auto sky_color_rgb233 = 0b00010011;
				color = sky_color_rgb233;
			}
			else if (j > wall_stop)
			{
                constexpr auto gnd_color_rgb233 = 0b00010000;
				color = gnd_color_rgb233;
			}
			else
			{
                const auto tex_y = static_cast<int>(tex_v) & (texture_height - 1);
                tex_v += tex_v_step;
                color = tex_cache.data[tex_y];
				//color = texture_map[hit - 1][tex_y + tex_x * texture_width];
			}

			// Run-length encode rendered color
			if (run_len == 0)
			{
				run_val = color;
				run_len = 1;
			}
			else if (color == run_val)
			{
				run_len++;
			}
			else
			{
				*dst_ptr++ = run_val;
				*dst_ptr++ = run_len;
				run_val = color;
				run_len = 1;
			}
		} //for(j)

		// Add last run
		*dst_ptr++ = run_val;
		*dst_ptr++ = run_len;
	} // for(i)

	// Terminate stream with special symbol
	*dst_ptr++ = codec::stream_end_symbol;
	*dst_ptr++ = codec::stream_end_symbol;

	frame.size = dst_ptr - frame.buffer;
}

#if 0
        if (hit > 0)
        {
            const auto perp_wall_dist = std::max(is_front_side ? (side_dist_y - delta_dist_y) : (side_dist_x - delta_dist_x), 0.1F);

            const auto row_id_height = static_cast<int>(frame.height / perp_wall_dist);

            const auto row_id_start = std::max((frame.height - row_id_height) / 2, 0);
            const auto row_id_stop  = std::min((frame.height + row_id_height) / 2, frame.height);

            for (auto j = 0; j <= row_id_start; j++)
            {
                constexpr auto base_sky_color_rgb233 = 0b00010011;
                constexpr auto base_gnd_color_rgb233 = 0b00010000;
                frame.buffer[j + i * frame.height] = base_sky_color_rgb233;
                frame.buffer[(frame.height - 1 - j) + i * frame.height] = base_gnd_color_rgb233;
                //frame.buffer[j + i * frame.height] = floor_colors[j];
                //frame.buffer[(frame.height - 1 - j) + i * frame.height] = floor_colors[j];
            }

            auto tex_u = is_front_side ?
                cmd.pose.pos_x + perp_wall_dist * ray_dir_x :
                cmd.pose.pos_y + perp_wall_dist * ray_dir_y;
            tex_u -= int(tex_u);

            auto tex_x = int(tex_u * texture_width);
            if ((!is_front_side && ray_dir_x > 0) && (is_front_side && ray_dir_y < 0)) tex_x = texture_width - 1 - tex_x;

            const auto tex_id = hit - 1;
            tex_cache.update(tex_id, tex_x);

            const auto tex_v_step = static_cast<float>(texture_height) / row_id_height;
            auto tex_v = (row_id_start - (frame.height - row_id_height) / 2) * tex_v_step;

            for (auto j = row_id_start; j < row_id_stop; j++)
            {
                const auto tex_y = int(tex_v) & (texture_height - 1);
                tex_v += tex_v_step;

                /*
                const auto dist_x = std::abs((int)light.pos_x - map_x);
                const auto dist_y = std::abs((int)light.pos_y - map_y);
                const auto dist = std::min(std::max(dist_x, dist_y), 5);
                */

                //const auto wall_color = 192 * (tex_x % 16 && tex_y % 16);
                //const auto wall_color = tex_x * 128 / texture_width + tex_y * 128 / texture_height;
                //const auto wall_color = (tex_x * 256 / texture_width) ^ (tex_y * 256 / texture_height);

                // Nearest-neighbor sampling
                //const auto wall_color = texture_map[hit - 1][tex_y + tex_x * texture_height];
                const auto wall_color = tex_cache.data[tex_y];
                frame.buffer[j + i * frame.height] = wall_color;
                //frame.buffer[j + i * frame.height] = wall_color >> (!is_front_side);
                //frame.buffer[j + i * frame.height] = wall_color >> dist;

                /*
                // Birow_idar interpolation
                const auto q11 = texture_map[hit - 1][std::min(tex_y + 0, 63) + std::min(tex_x + 0, 63) * texture_height];
                const auto q21 = texture_map[hit - 1][std::min(tex_y + 1, 63) + std::min(tex_x + 0, 63) * texture_height];
                const auto q12 = texture_map[hit - 1][std::max(tex_y + 0, 63) + std::max(tex_x + 1, 63) * texture_height];
                const auto q22 = texture_map[hit - 1][std::max(tex_y + 1, 63) + std::max(tex_x + 1, 63) * texture_height];
                const auto r1  = (q11 >> 1) + (q21 >> 1);
                const auto r2  = (q12 >> 1) + (q22 >> 1);
                const auto p   = (r1  >> 1) + (r2  >> 1);
                frame.buffer[j + i * frame.height] = p >> (!is_front_side);
                */

                //zbuffer[x] = perp_wall_dist;
            }
        } // hit block
        /*
        else
        {
            zbuffer[x] = 1e9;
        }
        */
    } // main loop

    const auto sprite_x = light.pos_x - cmd.pose.pos_x;
    const auto sprite_y = light.pos_y - cmd.pose.pos_y;

    const auto inv_det = 1.0F / (cmd.pose.plane_x * cmd.pose.dir_y - cmd.pose.plane_y * cmd.pose.dir_x);

    const auto tfm_x = inv_det * (sprite_x * cmd.pose.dir_y   - sprite_y * cmd.pose.dir_x);
    const auto tfm_y = inv_det * (sprite_y * cmd.pose.plane_x - sprite_x * cmd.pose.plane_y);

    const auto sprite_screen_x = int((frame.width / 2) * (1.0F + tfm_x / tfm_y));

    const auto sprite_height  = std::abs(int(frame.height / tfm_y)) >> 4;
    const auto sprite_start_y = std::max((frame.height - sprite_height) / 2, 0);
    const auto sprite_stop_y  = std::min((frame.height + sprite_height) / 2, frame.height);

    const auto sprite_width   = std::abs(int(frame.width / tfm_y)) >> 4;
    const auto sprite_start_x = std::max(sprite_screen_x - sprite_width / 2, 0);
    const auto sprite_stop_x  = std::min(sprite_screen_x + sprite_width / 2, frame.width);

    const auto slice_width = slice_stop - slice_start;
    const auto i_start = (sprite_start_x - slice_start);
    //for (auto x = sprite_start_x; x < std::min(sprite_stop_x, slice_stop); x++)
    for (auto i = i_start; i < i_stop; i++)
    {
        //if (tfm_y > 0 && slice_start <= x && x < slice_stop && tfm_y < zbuffer[x])
        if (tfm_y > 0 && tfm_y < zbuffer[x])
        {
            //for (auto y = sprite_start_y; y < sprite_stop_y; y++)
            for (auto j = sprite_start_y; j < sprite_stop_y; j++)
            {
                frame.buffer[j + i * frame.height] = 0xFF;
            }
        }
    }
}
#endif

