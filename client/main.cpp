#include <array>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <future>
#include <numeric>
#include <deque>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <fmt/core.h>
#include <fmt/color.h>

#include "gl.hpp"
#include "config.hpp"
#include "shaders.hpp"
#include "network.hpp"

auto key_callback(GLFWwindow* window, int key, int scancode, int action, int mode)
{
	if (action == GLFW_PRESS)
	{
		switch (key)
		{
			case GLFW_KEY_ESCAPE:
				glfwSetWindowShouldClose(window, GLFW_TRUE);
				break;
		}
	}
}

constexpr auto create_2d_rotation_matrix(float angle) -> glm::mat2
{
	return
	{
		+std::cos(angle), std::sin(angle),
		-std::sin(angle), std::cos(angle),
	};
}

constexpr auto create_cam_plane(const glm::vec2& direction) -> glm::vec2
{
	constexpr auto fov_scale = std::tan(glm::radians(config::fov * 0.5));
	return {-direction.y * fov_scale, direction.x};
}

auto update_pose(GLFWwindow* window, pose_t& pose) -> void
{
	constexpr auto rotate_left  = create_2d_rotation_matrix(-config::rotate_speed);
	constexpr auto rotate_right = create_2d_rotation_matrix(+config::rotate_speed);

	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) pose.position += pose.direction * config::sprint_speed;
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) pose.position -= pose.direction * config::sprint_speed;
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) pose.position -= pose.cam_plane * config::strafe_speed;
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) pose.position += pose.cam_plane * config::strafe_speed;
	if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
	{
		pose.direction = rotate_left * pose.direction;
		pose.cam_plane = create_cam_plane(pose.direction);
	}
	if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
	{
		pose.direction = rotate_right * pose.direction;
		pose.cam_plane = create_cam_plane(pose.direction);
	}
	pose.timestamp = get_timestamp_ns();
}

auto main() -> int
{
	std::clog << config::name << '\n';

	if (!glfwInit())
	{
		std::cerr << "Failed to initialize GLFW!\n";
		return -1;
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,	4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,	6);
	glfwWindowHint(GLFW_RESIZABLE,				false);
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT,	true);
	glfwWindowHint(GLFW_OPENGL_PROFILE,			GLFW_OPENGL_CORE_PROFILE);
	const auto window = glfwCreateWindow(
		config::width  * config::render_scale,
		config::height * config::render_scale,
		config::name, nullptr, nullptr);
	if (!window)
	{
		std::cerr << "Failed to create window!\n";
		return -1;
	}
	glfwSetKeyCallback(window, key_callback);

	glfwMakeContextCurrent(window);

	if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
	{
		std::cerr << "Failed to load OpenGL!\n";
		return -1;
	}

	glEnable(GL_DEBUG_OUTPUT);
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
	glDebugMessageCallback(gl::debug_message_callback, nullptr);

	const auto program = gl::program_builder_t()
		.add_shader(GL_VERTEX_SHADER,   shaders::shader_vs)
		.add_shader(GL_FRAGMENT_SHADER, shaders::shader_fs)
		.build();

	std::vector<std::vector<uint8_t>> frame_buffers (config::num_streams);
	for (auto&& fb : frame_buffers) fb.resize(frame_buffer_size, 0b01001001);
	const auto frame_buffer_texture = gl::texture_builder_t(frame_buffer_height, frame_buffer_width, config::num_streams)
		.set_type(GL_TEXTURE_2D_ARRAY)
		.build();

	std::array<uint32_t, 4> indices {{0, 1, 2, 3}};
	GLuint ibo {GL_NONE};
	glCreateBuffers(1, &ibo);
	glNamedBufferStorage(ibo, indices.size() * sizeof(indices[0]), indices.data(), GL_DYNAMIC_STORAGE_BIT);
	GLuint vao {GL_NONE};
	glCreateVertexArrays(1, &vao);
	glVertexArrayElementBuffer(vao, ibo);

	std::vector<stream_t> streams (config::num_streams);
	for (auto i = 0; i < streams.size(); i++)
	{
		streams[i].initialize(config::server_addr[i].ip, config::server_addr[i].port);
	}

	std::array<uint8_t, config::pkt_buffer_size> pkt_buffer;

	std::deque<double> frame_time_deque (10, 0);

	pose_t pose {0, {22.0F, 11.05F}, {-1, 0}, {0, -1}};

	auto ts_prev = glfwGetTime();

	while(!glfwWindowShouldClose(window))
	{
		const auto ts_now = glfwGetTime();
		const auto frame_time = ts_now - ts_prev;
		frame_time_deque.pop_front();
		frame_time_deque.push_back(frame_time);
		const auto avg_frame_rate = frame_time_deque.size() / std::reduce(std::cbegin(frame_time_deque), std::cend(frame_time_deque));
		ts_prev = ts_now;

		const auto title = fmt::format("{} | {:.1f} fps", config::name, avg_frame_rate);
		glfwSetWindowTitle(window, title.data());

		update_pose(window, pose);

		std::vector<std::future<std::optional<stream_t::stats_t>>> futures;
		for (auto i = 0; i < streams.size(); i++)
		{
			futures.push_back(
				std::async(std::launch::async,
					[&, i]() { return streams[i].render(pose, frame_buffers[i].data()); }));
		}

		std::vector<std::optional<stream_t::stats_t>> stats;
		for (auto i = 0; i < futures.size(); i++)
		{
			const auto s = futures[i].get();
			if (s) update_data(frame_buffer_texture, frame_buffers[i].data(), i);
			stats.push_back(s);
		}

		//if (const auto stats = stream.render(pose, frame_buffer.data()); stats)
		if (stats[0])
		{
			constexpr auto target_frame_time = 1.0F / config::target_fps;
			fmt::print(
				frame_time <= target_frame_time ? fmt::fg(fmt::color::white) : fmt::fg(fmt::color::red),
				"Frame {:4.1f} | RTT {:5.1f} | Render {:5.1f} | Stream {:5.1f}\n",
				frame_time * 1e3, stats[0]->pose_rtt_ns * 1e-6, stats[0]->render_time_us * 1e-3, stats[0]->stream_time_us * 1e-3);
		}

		glUseProgram(program.handle);
		glBindTextureUnit(0, frame_buffer_texture.handle);
		glBindVertexArray(vao);
		glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_INT, 0);

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	for (auto&& s : streams) s.shutdown();

	glfwDestroyWindow(window);
	glfwTerminate();

	return 0;
}
