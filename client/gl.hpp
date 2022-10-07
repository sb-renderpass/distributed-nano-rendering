#pragma once

#include <iostream>
#include <span>
#include <vector>

#include <glad/glad.h>

namespace gl
{

struct shader_t
{
	GLuint handle {GL_NONE};
};

struct program_t
{
	GLuint handle {GL_NONE};
};

auto create_shader(GLenum type, const char* source) -> shader_t
{
	const auto handle = glCreateShader(type);
	glShaderSource(handle, 1, &source, nullptr);
	glCompileShader(handle);

	auto status = 0;
	glGetShaderiv(handle, GL_COMPILE_STATUS, &status);
	if (!status)
	{
		std::array<char, 1024> log;
		glGetShaderInfoLog(handle, log.size(), nullptr, log.data());
		std::cerr << "Failed to build shader!\n" << log.data() << '\n';
		return {};
	}

	return {handle};
}

auto create_program(std::span<shader_t> shaders) -> program_t
{
	const auto handle = glCreateProgram();
	for (const auto s : shaders) glAttachShader(handle, s.handle);
	glLinkProgram(handle);

	auto status = 0;
	glGetProgramiv(handle, GL_LINK_STATUS, &status);
	if (!status)
	{
		std::array<char, 1024> log;
		glGetProgramInfoLog(handle, log.size(), nullptr, log.data());
		std::cerr << "Failed to build program!\n" << log.data() << '\n';
		return {};
	}

	return {handle};
}

class program_builder_t
{
public:
	auto add_shader(GLenum type, const char* source) -> program_builder_t&
	{
		shaders_.push_back(create_shader(type, source));
		return *this;
	}

	auto build() -> program_t
	{
		return create_program(shaders_);
	}

private:
	std::vector<shader_t> shaders_;
};

struct texel_t
{
	GLenum format {GL_RED};
	GLenum type   {GL_UNSIGNED_BYTE};
};

struct texture_t
{
	GLuint handle {GL_NONE};
	int width  {0};
	int height {0};
	int depth  {0};
	GLenum type {GL_TEXTURE_2D};
	texel_t texel;
};

auto update_data(const texture_t& texture, const uint8_t* data, int layer=0) -> void
{
	if (data)
	{
		switch (texture.type)
		{
			default: [[fallthrough]];
			case GL_TEXTURE_2D:
				glTextureSubImage2D(
					texture.handle,
					0,
					0, 0,
					texture.width, texture.height,
					texture.texel.format,
					texture.texel.type,
					data);
				break;
			case GL_TEXTURE_2D_ARRAY:
				glTextureSubImage3D(
					texture.handle,
					0,
					0, 0, layer,
					texture.width, texture.height, 1,
					texture.texel.format,
					texture.texel.type,
					data);
				break;
		}
	}
}

class texture_builder_t
{
public:
	texture_builder_t(int width, int height, int depth=0)
		: width {width}, height {height}, depth {depth} {}

	auto set_data(const uint8_t* data) -> texture_builder_t&
	{
		this->data = data;
		return *this;
	}

	auto set_type(GLenum type)
	{
		this->type = type;
		return *this;
	}

	auto build() const -> texture_t
	{
		texture_t texture {GL_NONE, width, height, depth, type};
		glCreateTextures(type, 1, &texture.handle);
		glTextureParameteri(texture.handle, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTextureParameteri(texture.handle, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTextureParameteri(texture.handle, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
		glTextureParameteri(texture.handle, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);

		switch (type) // TODO: Support other texture types
		{
			default: [[fallthrough]];
			case GL_TEXTURE_2D:
				glTextureStorage2D(texture.handle, 1, GL_R8UI, width, height);
				break;
			case GL_TEXTURE_2D_ARRAY:
				glTextureStorage3D(texture.handle, 1, GL_R8UI, width, height, depth);
				break;
		}

		if (data)
		{
			for (auto i = 0; i < depth; i++) update_data(texture, data, i); // Set same data for all layers
		}

		texture.texel = {GL_RED_INTEGER, GL_UNSIGNED_BYTE};
		return texture;
	}

private:
	int width  {0};
	int height {0};
	int depth  {0};
	const uint8_t* data {nullptr};
	GLenum type {GL_TEXTURE_2D};
};

auto debug_message_callback(
	GLenum source,
	GLenum type,
	unsigned int id,
	GLenum severity,
	GLsizei length,
	const char* msg,
	const void*) -> void
{
	if (type == GL_DEBUG_TYPE_ERROR)
		std::cerr << "GL: " << msg << '\n';
}

} // namespace gl

