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
vec2 texcoords[4] = vec2[](
    vec2(0, 0),
    vec2(0, 1),
    vec2(1, 0),
    vec2(1, 1)
);

struct instance_t
{
    mat4 transform;
    int  texture_id;
	int  padding[3];
};

layout(binding=0, std430) readonly buffer instance_data
{
    instance_t instance[];
};

void main()
{
    const vec2 texcoord = texcoords[gl_VertexID];
    gl_Position = instance[gl_InstanceID].transform * vec4(texcoord, 0, 1) * 2 - 1;
    vs_to_fs.texcoord = vec2(1 - texcoord.y, texcoord.x); // Transpose and flip texture
    vs_to_fs.texture_id = instance[gl_InstanceID].texture_id;

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
    const vec3 color = unpack_rgb233(texture(in_texture, vec3(vs_to_fs.texcoord, vs_to_fs.texture_id)).r);
    out_color = vec4(color, 1);
}

)glsl";

} // namespace shaders

