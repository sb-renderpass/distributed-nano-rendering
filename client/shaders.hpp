#pragma once

namespace shaders
{

constexpr auto shader_vs = R"glsl(
#version 450 core

out VS_TO_FS
{
    vec2 texcoord;
} vs_to_fs;

// Hard-coded triangle-strip quad
vec2 positions[4] = vec2[](
    vec2(-1, -1),
    vec2(-1, +1),
    vec2(+1, -1),
    vec2(+1, +1)
);

void main()
{
    const vec2 position = positions[gl_VertexID];
    const vec2 texcoord = (position + 1) * 0.5F;

    gl_Position = vec4(position, 0, 1);
    vs_to_fs.texcoord = vec2(1 - texcoord.y, texcoord.x); // Transpose and flip texture

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
} vs_to_fs;

out vec4 out_color;

layout(binding = 0) uniform usampler2DArray in_texture;

vec3 unpack_rgb233(uint color)
{
    return vec3(
        (((color & 0xC0) >> 6) << 6) / 255.0,
        (((color & 0x38) >> 3) << 5) / 255.0,
        (((color & 0x07) >> 0) << 5) / 255.0);
}

void main()
{
    const vec3 color = unpack_rgb233(texture(in_texture, vec3(vs_to_fs.texcoord, 0)).r);
    out_color = vec4(color, 1);
}

)glsl";

} // namespace shaders

