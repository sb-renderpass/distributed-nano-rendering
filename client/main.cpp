#include <array>
#include <algorithm>
#include <asm-generic/socket.h>
#include <cctype>
#include <cmath>
#include <chrono>
#include <cstring>
#include <iostream>
#include <numeric>
#include <deque>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <fmt/core.h>
#include <fmt/color.h>

#include "gl.hpp"
#include "config.hpp"
#include "shaders.hpp"
#include "network.hpp"

constexpr auto frame_buffer_width  = config::width;
constexpr auto frame_buffer_height = config::height;
constexpr auto frame_buffer_size   = frame_buffer_width * frame_buffer_height;
constexpr auto slice_buffer_size   = frame_buffer_size / config::num_slices;
constexpr auto num_pkts_per_slice  = static_cast<int>(std::ceil(static_cast<float>(slice_buffer_size) / config::pkt_buffer_size));
constexpr auto num_pkts_per_frame  = num_pkts_per_slice * config::num_slices;
constexpr auto last_seqnum         = num_pkts_per_frame - 1;

auto get_timestamp_ns() -> uint64_t
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::high_resolution_clock::now().time_since_epoch()).count();
}

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

	std::vector<uint8_t> frame_buffer (frame_buffer_size, 0b01001001);
	const auto frame_buffer_texture = gl::texture_builder_t(frame_buffer_height, frame_buffer_width)
		.add_data(frame_buffer.data())
		.build();

	GLuint dummy_vao {GL_NONE};
	glCreateVertexArrays(1, &dummy_vao);


	stream_t stream;
	constexpr auto target_frame_time = 1.0F / config::target_fps;
	constexpr auto timeout_us = static_cast<int64_t>(target_frame_time * 1e6F);
	stream.initialize(config::server_ip, config::server_port, timeout_us);

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

		stream.send(reinterpret_cast<uint8_t*>(&pose), sizeof(pose));

		auto prev_seqnum = -1;
		auto pose_rtt_ns = 0;

		while (prev_seqnum < last_seqnum)
		{
			if (stream.recv(pkt_buffer.data(), pkt_buffer.size()) < 0) break;

			const auto header = get_pkt_header(pkt_buffer.data());
			const auto [padding, seqnum, offset] = unpack_pkt_header(header);

			const auto footer = get_pkt_footer(pkt_buffer.data(), pkt_buffer.size());

			const auto payload_size = pkt_buffer.size() - sizeof(pkt_header_t) - padding * footer.padding_len;
			std::copy_n(pkt_buffer.data() + sizeof(pkt_header_t), payload_size, frame_buffer.data() + offset);

			if (seqnum == last_seqnum)
			{
				const auto pose_recv_timestamp = get_timestamp_ns();
				const auto pose_rtt_ns = pose_recv_timestamp - footer.timestamp;
				fmt::print(
					frame_time <= target_frame_time ? fmt::fg(fmt::color::white) : fmt::fg(fmt::color::red),
					"Frame {:4.1f} | RTT {:5.1f} | Render {:5.1f} | Stream {:5.1f}\n",
					frame_time * 1e3, pose_rtt_ns * 1e-6, footer.render_time_us * 1e-3, footer.stream_time_us * 1e-3);

				update_data(frame_buffer_texture, frame_buffer.data());
			}

			if (seqnum != (prev_seqnum + 1))
			{
				fmt::print(fmt::fg(fmt::color::yellow), "JUMP {} => {}\n", prev_seqnum, seqnum);
			}
			prev_seqnum = seqnum;
		}

		if (prev_seqnum < last_seqnum)
		{
			fmt::print(fmt::fg(fmt::color::red), "LOST {} => {}\n", prev_seqnum + 1, last_seqnum);
		}

		glUseProgram(program.handle);
		glBindTextureUnit(0, frame_buffer_texture.handle);
		glBindVertexArray(dummy_vao);
		glDrawArrays(GL_TRIANGLES, 0, 6);

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	stream.shutdown();

	glfwDestroyWindow(window);
	glfwTerminate();

	return 0;
}
