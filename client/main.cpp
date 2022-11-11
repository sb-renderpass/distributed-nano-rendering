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
#include <glm/gtc/type_ptr.hpp>
#include <fmt/core.h>
#include <fmt/color.h>

#include "gl.hpp"
#include "config.hpp"
#include "shaders.hpp"
#include "types.hpp"
#include "network.hpp"

auto g_stream_overlay_alpha = 0.0F;
auto g_slice_overlay_alpha  = 0.0F;

auto key_callback(GLFWwindow* window, int key, int, int action, int) -> void
{
	if (action == GLFW_PRESS)
	{
		switch (key)
		{
			case GLFW_KEY_ESCAPE:
				glfwSetWindowShouldClose(window, GLFW_TRUE);
				break;
			case GLFW_KEY_1:
				g_stream_overlay_alpha = g_stream_overlay_alpha > 0.0F ? 0.0F : 0.5F;
				break;
			case GLFW_KEY_2:
				g_slice_overlay_alpha = g_slice_overlay_alpha > 0.0F ? 0.0F : 1.0F;
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

struct stream_render_t
{
	uint32_t id {};
	uint32_t slice_bitmask {};
};

auto create_stream_render_data(
	uint32_t stream_bitmask,
	const std::array<uint32_t, config::num_streams>& slice_bitmasks
) -> std::vector<stream_render_t>
{
	std::vector<stream_render_t> stream_render_data;
	while (stream_bitmask > 0)
	{
		const auto stream_id = static_cast<uint32_t>(std::countr_zero(stream_bitmask));
		stream_render_data.push_back({stream_id, slice_bitmasks[stream_id]});
		stream_bitmask &= ~(1U << stream_id);
	}
	return stream_render_data;
}

auto create_slice_render_data(uint32_t stream_bitmask) -> glm::vec4
{
	const auto num_active_streams = std::popcount(stream_bitmask);
	const auto step = 1.0F / (num_active_streams * config::num_slices);
	return {step, 1.0F, step, 0.0F};
}

constexpr auto create_slice_texture_data() -> glm::vec4
{
	constexpr auto step = 1.0F / config::num_slices;
	return {step, 1.0F, step, 0.0F};
}

auto log_result(float frame_time, const stream_t::result_t& r) -> void
{
	if (r.stream_bitmask > 0)
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
					"{:1d}) RTT {:4.1f} | Render {:4.1f} | Stream {:4.1f} | CR {:4.2f}\n",
					i,
					r.stats[i].pose_rtt_ns * 1e-6,
					r.stats[i].render_time_us * 1e-3,
					r.stats[i].stream_time_us * 1e-3,
					r.stats[i].num_enc_bytes / static_cast<float>(frame_buffer_size)
				);
			}
		}

		fmt::print("\n");
	}
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

	std::vector<uint8_t> screen_buffer (frame_buffer_size * config::num_streams, 0b01010010);
	const auto screen_texture = gl::texture_builder_t(frame_buffer_height, frame_buffer_width, config::num_streams)
		.set_data(screen_buffer.data())
		.set_type(GL_TEXTURE_2D_ARRAY)
		.build();

	std::array<uint32_t, 4> indices {{0, 1, 2, 3}};
	const auto index_buffer = gl::create_buffer<uint32_t>(indices.size(), indices.data());
	GLuint vao {GL_NONE};
	glCreateVertexArrays(1, &vao);
	glVertexArrayElementBuffer(vao, index_buffer.handle);

	const auto stream_render_buffer = gl::create_buffer<stream_render_t>(config::num_streams);
	gl::bind_buffer(stream_render_buffer, 0);

	stream_t stream {config::servers, screen_buffer.data()};

	std::deque<double> frame_time_deque (10, 0);

	uint16_t frame_num = 0;
	auto pose = pose_t {0, frame_num, {22.0F, 11.05F}, {-1, 0}, {0, -1}};

	uint32_t prev_stream_bitmask {all_stream_bitmask};
	std::array<uint32_t, config::num_streams> prev_slice_bitmasks {};
	std::fill(std::begin(prev_slice_bitmasks), std::end(prev_slice_bitmasks), all_slice_bitmask);

	constexpr auto slice_texture_data = create_slice_texture_data();

	const auto preferred_frame_time = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::duration<float>(1.0F / config::target_fps));

	auto ts_prev = glfwGetTime();

	while(!glfwWindowShouldClose(window))
	{
		const auto timeout = std::chrono::high_resolution_clock::now() + preferred_frame_time;

		const auto ts_now = glfwGetTime();
		const auto frame_time = ts_now - ts_prev;
		frame_time_deque.pop_front();
		frame_time_deque.push_back(frame_time);
		const auto avg_frame_rate = frame_time_deque.size() / std::reduce(std::cbegin(frame_time_deque), std::cend(frame_time_deque));
		ts_prev = ts_now;

		update_pose(window, pose);
		pose.frame_num = frame_num++;
		const auto cmds = create_render_commands(pose, prev_stream_bitmask);

		auto ready_future = stream.start(cmds);

		const auto num_active_streams = std::popcount(prev_stream_bitmask);
		const auto stream_render_data = create_stream_render_data(prev_stream_bitmask, prev_slice_bitmasks);
		gl::update_data(
			stream_render_buffer, stream_render_data.data(), stream_render_data.size());
		const auto slice_render_data = create_slice_render_data(prev_stream_bitmask);

		const auto title = fmt::format("{} | {:.1f} fps | {:d} server(s)", config::name, avg_frame_rate, num_active_streams);
		glfwSetWindowTitle(window, title.data());

		//std::this_thread::sleep_until(timeout);
		ready_future.wait_until(timeout);
		const auto result = stream.stop();

		for (auto i = 0; i < config::num_streams; i++)
		{
			const auto stream_offset = i * frame_buffer_size;
			gl::update_data(screen_texture, screen_buffer.data() + stream_offset, i);
			/*
			if (const auto bm = result.stats[i].slice_bitmask; bm != all_slice_bitmask)
				fmt::print("{:02b} {:016b}\n", result.stream_bitmask, bm);
			*/
		}
		log_result(frame_time, result);

		if (result.stream_bitmask > 0)
		{
			prev_stream_bitmask = result.stream_bitmask;
			std::transform(
				std::cbegin(result.stats), std::cend(result.stats),
				std::begin(prev_slice_bitmasks),
				[](const auto& s) { return s.slice_bitmask; });
		}

		glUseProgram(program.handle);
		glBindTextureUnit(0, screen_texture.handle);
		glBindVertexArray(vao);
		glUniform4fv(0, 1, glm::value_ptr(slice_render_data));
		glUniform4fv(1, 1, glm::value_ptr(slice_texture_data));
		glUniform1i(2, config::num_slices);
		glUniform1f(3, g_slice_overlay_alpha);
		glUniform1f(4, g_stream_overlay_alpha);
		glDrawElementsInstanced(
			GL_TRIANGLE_STRIP,
			indices.size(),
			GL_UNSIGNED_INT,
			nullptr,
			num_active_streams * config::num_slices);

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	gl::delete_buffer(stream_render_buffer);
	gl::delete_buffer(index_buffer);
	gl::delete_texture(screen_texture);
	gl::delete_program(program);

	glfwDestroyWindow(window);
	glfwTerminate();

	return 0;
}
