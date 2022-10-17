#pragma once

namespace shaders
{

constexpr auto shader_vs = R"glsl(
#version 450 core

out VS_TO_FS
{
    vec2 texcoord;
    flat int texture_id;
} vs_to_fs;

// Hard-coded triangle-strip quad
vec2 quad_coords[4] = vec2[](
    vec2(0, 0),
    vec2(0, 1),
    vec2(1, 0),
    vec2(1, 1)
);

layout(location = 0) uniform vec4 u_slice_render_data;
layout(location = 1) uniform vec4 u_slice_texture_data;
layout(location = 2) uniform  int u_num_slices;

layout(binding = 0, std430) readonly buffer stream_id_buffer
{
    int stream_ids[];
};

mat4 create_T(vec4 v, int i)
{
    return mat4(
        v.x, 0, 0, 0,
        0, v.y, 0, 0,
        0, 0, 0, 0,
        v.z * i, v.w * i, 0, 1);
}

void main()
{
    const int frame_id = gl_InstanceID / u_num_slices;
    const int slice_id = gl_InstanceID % u_num_slices;
    const vec2 quad_coord = quad_coords[gl_VertexID];

    const mat4 slice_render_T  = create_T(u_slice_render_data, gl_InstanceID);
    const mat4 slice_texture_T = create_T(u_slice_texture_data, slice_id);

    const vec2 texcoord = (slice_texture_T * vec4(quad_coord, 0, 1)).st;

    gl_Position = slice_render_T * vec4(quad_coord, 0, 1) * 2 - 1;
    vs_to_fs.texcoord = vec2(1 - texcoord.y, texcoord.x); // Transpose and flip texture
    vs_to_fs.texture_id = stream_ids[frame_id];

    /*
    // Screen-space triangle
    // Render without index buffer
    // Use glDrawElements(GL_TRIANGLES, 0, 6)

    gl_Position = vec4(
        (gl_VertexID / 2) * 4 - 1,
        (gl_VertexID % 2) * 4 - 1,
        0, 1);

    // Transpose and flip texture
    vs_to_fs.texcoord = vec2(
        1 - (gl_VertexID % 2) * 2,
        (gl_VertexID / 2) * 2);
    */
}

)glsl";

constexpr auto shader_fs = R"glsl(
#version 450 core

in VS_TO_FS
{
    vec2 texcoord;
    flat int texture_id;
} vs_to_fs;

vec3 overlay_color[2] = vec3[](
    vec3(1, 0, 1), // Magenta
    vec3(0, 1, 1)  // Cyan
);

out vec4 out_color;

layout(location = 3) uniform float u_overlay_alpha;

layout(binding = 0) uniform usampler2DArray in_texture;

vec3 unpack_rgb233(uint color)
{
    return vec3(
        (((color & 0xC0U) >> 6) << 6) / 255.0,
        (((color & 0x38U) >> 3) << 5) / 255.0,
        (((color & 0x07U) >> 0) << 5) / 255.0);
}

void main()
{
    const vec3 color = unpack_rgb233(texture(in_texture, vec3(vs_to_fs.texcoord, vs_to_fs.texture_id)).r);
    const vec3 final_color = mix(color, overlay_color[vs_to_fs.texture_id], u_overlay_alpha);
    out_color = vec4(final_color, 1);
}

)glsl";

} // namespace shaders

