#include <array>
#include <bit>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <pthread.h>
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
#include "types.hpp"
#include "network.hpp"

auto g_overlay_alpha = 0.0F;

auto key_callback(GLFWwindow* window, int key, int scancode, int action, int mode)
{
	if (action == GLFW_PRESS)
	{
		switch (key)
		{
			case GLFW_KEY_ESCAPE:
				glfwSetWindowShouldClose(window, GLFW_TRUE);
				break;
			case GLFW_KEY_O:
				g_overlay_alpha = g_overlay_alpha > 0.0F ? 0.0F : 0.5F;
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

auto create_render_commands(const pose_t& pose, uint32_t stream_bitmask) -> std::vector<render_command_t>
{
	const auto num_active_streams = std::popcount(stream_bitmask);
	std::vector<render_command_t> cmds (config::num_streams);
	const auto delta_active = 2.0F / num_active_streams;
	const auto delta_ideal  = 2.0F / config::num_streams;
	for (auto i = 0, count = 0; i < cmds.size(); i++)
	{
		if (stream_bitmask & (1U << i))
		{
			cmds[i] = {
				.pose = pose,
				.tile = tile_t {
					.x_scale  = delta_active,
					.x_offset = delta_active * count - 1,
				},
			};
			count++;
		}
		else
		{
			cmds[i] = {
				.pose = pose,
				.tile = tile_t {
					.x_scale  = delta_ideal,
					.x_offset = delta_ideal * i - 1,
				},
			};
		}
	}
	return cmds;
}

struct slice_render_data_t
{
	glm::mat4 transform {1};
};

auto create_slice_render_data(uint32_t stream_bitmask) -> std::vector<slice_render_data_t>
{
	const auto num_active_streams = std::popcount(stream_bitmask);
	std::vector<slice_render_data_t> result (config::num_streams * config::num_slices);
	const auto step = 1.0F / (num_active_streams * config::num_slices);
	for (auto i = 0; i < result.size(); i++)
	{
		const auto scale_x  = step;
		const auto scale_y  = 1.0F;
		const auto offset_x = step * i;
		const auto offset_y = 0.0F;
		result[i].transform = {
			scale_x, 0, 0, 0,
			0, scale_y, 0, 0,
			0, 0, 0, 0,
			offset_x, offset_y, 0, 1,
		};
	}
	return result;
}

struct slice_texture_data_t
{
	glm::mat4 transform {1};
};

auto create_slice_texture_data(uint32_t stream_bitmask) -> std::vector<slice_texture_data_t>
{
	const auto num_active_streams = std::popcount(stream_bitmask);
	std::vector<slice_texture_data_t> result (config::num_slices);
	const auto step = 1.0F / (config::num_slices);
	for (auto i = 0; i < result.size(); i++)
	{
		const auto scale_x  = step;
		const auto scale_y  = 1.0F;
		const auto offset_x = step * i;
		const auto offset_y = 0.0F;
		result[i].transform = {
			scale_x, 0, 0, 0,
			0, scale_y, 0, 0,
			0, 0, 0, 0,
			offset_x, offset_y, 0, 1,
		};
	}
	return result;
}

auto log_result(float frame_time, const stream_t::result_t& r) -> void
{
	constexpr auto target_frame_time = 1.0F / config::target_fps;
	const auto is_latency_high = (frame_time - target_frame_time) > 0.1F;

	fmt::print(
		is_latency_high ? fmt::fg(fmt::color::red) : fmt::fg(fmt::color::white),
		" Frame {:4.1f} | Mask {:02b}\n",
		frame_time * 1e3, r.stream_bitmask);

	for (auto i = 0; i < config::num_streams; i++)
	{
		if (r.stream_bitmask & (1 << i)) // Only log data for completed streams
		{
			fmt::print(
				"{:1d}) RTT {:4.1f} | Render {:4.1f} | Stream {:4.1f}\n",
				i, r.stats[i].pose_rtt_ns * 1e-6, r.stats[i].render_time_us * 1e-3, r.stats[i].stream_time_us * 1e-3);
		}
	}

	fmt::print("\n");
}

auto main() -> int
{
	cpu_set_t cpu_set;
	CPU_ZERO(&cpu_set);
	CPU_SET(0, &cpu_set);
	if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set), &cpu_set) != 0)
	{
		std::cerr << "Failed to set recv-thread affinity!\n";
	}

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
		config::width  * config::render_scale_w,
		config::height * config::render_scale_h,
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
		.set_data(frame_buffers[0].data())
		.set_type(GL_TEXTURE_2D_ARRAY)
		.build();

	std::array<uint32_t, 4> indices {{0, 1, 2, 3}};
	GLuint ibo {GL_NONE};
	glCreateBuffers(1, &ibo);
	glNamedBufferStorage(ibo, indices.size() * sizeof(indices[0]), indices.data(), GL_DYNAMIC_STORAGE_BIT);
	GLuint vao {GL_NONE};
	glCreateVertexArrays(1, &vao);
	glVertexArrayElementBuffer(vao, ibo);

	GLuint slice_render_buffer {GL_NONE};
	glCreateBuffers(1, &slice_render_buffer);
	glNamedBufferStorage(
		slice_render_buffer,
		config::num_streams * config::num_slices * sizeof(slice_render_data_t),
		nullptr,
		GL_DYNAMIC_STORAGE_BIT);

	GLuint slice_texture_buffer {GL_NONE};
	glCreateBuffers(1, &slice_texture_buffer);
	glNamedBufferStorage(
		slice_texture_buffer,
		config::num_slices * sizeof(slice_texture_data_t),
		nullptr,
		GL_DYNAMIC_STORAGE_BIT);

	stream_t stream {config::servers, &frame_buffers};

	std::deque<double> frame_time_deque (10, 0);

	uint16_t frame_num = 0;
	auto pose = pose_t {0, frame_num, {22.0F, 11.05F}, {-1, 0}, {0, -1}};

	auto ts_prev = glfwGetTime();

	std::deque<uint32_t> stream_bitmask_history (3, all_stream_bitmask);
	auto filtered_stream_bitmask = stream_bitmask_history[1];
	auto last_used_stream_bitmask = 0U;

	const auto preferred_frame_time = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::duration<float>(1.0F / config::target_fps));

	while(!glfwWindowShouldClose(window))
	{
		const auto timeout = std::chrono::high_resolution_clock::now() + preferred_frame_time;

		const auto ts_now = glfwGetTime();
		const auto frame_time = ts_now - ts_prev;
		frame_time_deque.pop_front();
		frame_time_deque.push_back(frame_time);
		const auto avg_frame_rate = frame_time_deque.size() / std::reduce(std::cbegin(frame_time_deque), std::cend(frame_time_deque));
		ts_prev = ts_now;

		if (
			stream_bitmask_history[0] != stream_bitmask_history[1] &&
			stream_bitmask_history[1] == stream_bitmask_history[2])
		{
			filtered_stream_bitmask = stream_bitmask_history[1];
		}
		const auto num_active_streams = std::popcount(filtered_stream_bitmask);

		update_pose(window, pose);
		pose.frame_num = frame_num++;
		const auto cmds = create_render_commands(pose, filtered_stream_bitmask);

		auto ready_future = stream.start(cmds);

		// Only generate render data when necessary
		//if (last_used_stream_bitmask != filtered_stream_bitmask)
		{
			const auto slice_render_data = create_slice_render_data(filtered_stream_bitmask);
			glNamedBufferSubData(
				slice_render_buffer,
				0, slice_render_data.size() * sizeof(slice_render_data_t),
				slice_render_data.data());

			const auto slice_texture_data = create_slice_texture_data(filtered_stream_bitmask);
			glNamedBufferSubData(
				slice_texture_buffer,
				0, slice_texture_data.size() * sizeof(slice_texture_data_t),
				slice_texture_data.data());

			last_used_stream_bitmask = filtered_stream_bitmask;
		}

		const auto title = fmt::format("{} | {:.1f} fps | {:d} server(s)", config::name, avg_frame_rate, num_active_streams);
		glfwSetWindowTitle(window, title.data());

		//std::this_thread::sleep_until(timeout);
		ready_future.wait_until(timeout);
		const auto result = stream.stop();

		for (auto i = 0; i < config::num_streams; i++)
		{
			const auto pkt_bitmask = result.stats[i].pkt_bitmask;
			if (result.stream_bitmask & (1U << i))
			{
				update_data(frame_buffer_texture, frame_buffers[i].data(), i);
			}
			else
			{
				const auto slice_bitmask = calculate_slice_bitmask(pkt_bitmask);
				//fmt::print("{:028b} {:04b}\n", pkt_bitmask, slice_bitmask);
			}
		}
		log_result(frame_time, result);

		if (result.stream_bitmask > 0)
		{
			stream_bitmask_history.pop_front();
			stream_bitmask_history.push_back(result.stream_bitmask);
		}

		glUseProgram(program.handle);
		glBindTextureUnit(0, frame_buffer_texture.handle);
		glBindVertexArray(vao);
		glUniform1f(0, g_overlay_alpha);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, slice_render_buffer);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, slice_texture_buffer);
		glDrawElementsInstanced(GL_TRIANGLE_STRIP, indices.size(), GL_UNSIGNED_INT, 0, 2 * 4);

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	glfwDestroyWindow(window);
	glfwTerminate();

	return 0;
}
