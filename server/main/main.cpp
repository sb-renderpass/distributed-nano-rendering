/* BSD Socket API Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <cstring>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "raycaster.hpp"
#include "types.hpp"

#define PORT CONFIG_EXAMPLE_PORT

static const char *TAG = "server";

namespace
{

constexpr auto frame_buffer_width  = 320;
constexpr auto frame_buffer_height = 240;
constexpr auto pkt_buffer_size     = 1440;
constexpr auto num_slices          = 4;

constexpr auto frame_buffer_size   = frame_buffer_width * frame_buffer_height;
constexpr auto slice_buffer_size   = frame_buffer_size / num_slices;

} // anonymous namespace

TaskHandle_t render_task_handle {nullptr};
TaskHandle_t stream_task_handle {nullptr};

render_command_t cmd;

// Double-buffered slice for simultaneous render and stream
encoded_slice_t slice[2];

struct pipeline_stats_t
{
    uint32_t render_elapsed {0};
    uint32_t stream_elapsed {0};
};
pipeline_stats_t pipeline_stats;

//light_t light {22.0F, 11.5F, -1.0F, 0.0F};

auto render_task(void* params) -> void
{
    constexpr auto slice_width = frame_buffer_width / num_slices;

    for (;;)
    {
		// Wait for network thread to start a new frame
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        auto index = 0U;
        auto render_elapsed = 0U;

        for (auto slice_id = 0; slice_id < num_slices; slice_id++)
        {
            render_elapsed -= esp_timer_get_time();
            render_encode_slice(cmd, slice_id * slice_width, (slice_id + 1) * slice_width, slice[index]);
            render_elapsed += esp_timer_get_time();

			// FIXME: Hack to ensure render thread is always slower than network thread
			vTaskDelay(pdMS_TO_TICKS(2));

			// Notify network thread to stream rendered slice and swap slice buffers
			xTaskNotify(stream_task_handle, index, eSetValueWithOverwrite);
			index = !index;
        }

        pipeline_stats.render_elapsed = render_elapsed;
    }

    vTaskDelete(nullptr);
}

inline auto write_pkt_info(
	int frame_end,
	int slice_end,
	int slice_id,
	int pkt_id,
	uint8_t* buffer) -> uint8_t*
{
	*buffer++ = ((frame_end & 1) << 7) | ((slice_end & 1) << 6) | (slice_id & 0x0F);
	*buffer++ = (pkt_id & 0xFF);
	return buffer;
}

inline auto write_pkt_payload(const uint8_t* payload, int size, uint8_t* buffer) -> uint8_t*
{
	std::memcpy(buffer, payload, size);
	return buffer + size;
}

inline auto write_slice_info(uint8_t num_pkts_in_slice, uint8_t* buffer) -> uint8_t*
{
	*buffer++ = num_pkts_in_slice;
	return buffer;
}

auto stream_task(void* params) -> void
{
    int addr_family = (int)params;
    int ip_protocol = 0;
    struct sockaddr_in6 client_addr;
    socklen_t socklen = sizeof(client_addr);

    uint8_t pkt_buffer[pkt_buffer_size];

    for (;;)
    {
        if (addr_family == AF_INET)
        {
            struct sockaddr_in *client_addr_ip4 = (struct sockaddr_in *)&client_addr;
            client_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
            client_addr_ip4->sin_family = AF_INET;
            client_addr_ip4->sin_port = htons(PORT);
            ip_protocol = IPPROTO_IP;
        }
        else if (addr_family == AF_INET6)
        {
            bzero(&client_addr.sin6_addr.un, sizeof(client_addr.sin6_addr.un));
            client_addr.sin6_family = AF_INET6;
            client_addr.sin6_port = htons(PORT);
            ip_protocol = IPPROTO_IPV6;
        }

        const auto sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
        if (sock < 0)
        {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket created");

#if defined(CONFIG_LWIP_NETBUF_RECVINFO) && !defined(CONFIG_EXAMPLE_IPV6)
        int enable = 1;
        lwip_setsockopt(sock, IPPROTO_IP, IP_PKTINFO, &enable, sizeof(enable));
#endif

#if defined(CONFIG_EXAMPLE_IPV4) && defined(CONFIG_EXAMPLE_IPV6)
        if (addr_family == AF_INET6) {
            // Note that by default IPV6 binds to both protocols, it is must be disabled
            // if both protocols used at the same time (used in CI)
            int opt = 1;
            setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
            setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
        }
#endif
        // Set timeout
        struct timeval timeout;
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);

        constexpr auto priority = 6 << 7;
        setsockopt(sock, IPPROTO_IP, IP_TOS, &priority, sizeof(priority));

        constexpr auto flag = 1;
        setsockopt(sock, SOL_SOCKET, SO_DONTROUTE, &flag, sizeof(flag));

        //constexpr auto rcvbuf_size = sizeof(render_command_t);
        //setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf_size, sizeof(rcvbuf_size));

        const auto err = bind(sock, reinterpret_cast<struct sockaddr*>(&client_addr), sizeof(client_addr));
        if (err < 0)
        {
            ESP_LOGE(TAG, "Failed to bind socket! errno=%d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket bound to port %d", PORT);

        for (;;)
        {
            const int recv_nbytes = recvfrom(
				sock,
				&cmd, sizeof(cmd), 0,
				reinterpret_cast<struct sockaddr*>(&client_addr), &socklen);

            if (recv_nbytes == sizeof(cmd))
            {
				// Notify render thread to start a new frame when a new pose is received
				xTaskNotifyGive(render_task_handle);

                auto stream_elapsed = 0U;

                for (auto slice_id = 0; slice_id < num_slices; slice_id++)
                {
					// Wait for render thread to signal that the rendered slice is ready for streaming
					const auto index = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

                    stream_elapsed -= esp_timer_get_time();

					const auto encoded_slice_size = slice[index].size;
					const auto pkt_payload_size   = pkt_buffer_size - sizeof(pkt_info_t);
					const auto num_pkts_in_slice = static_cast<int>(std::ceil(static_cast<float>(encoded_slice_size) / pkt_payload_size));

                    for (auto pkt_id = 0; pkt_id < num_pkts_in_slice; pkt_id++)
                    {
						auto ptr = pkt_buffer;

                        const auto is_frame_end = slice_id == num_slices - 1;
                        const auto is_slice_end = pkt_id == num_pkts_in_slice - 1;
                        ptr = write_pkt_info(is_frame_end, is_slice_end, slice_id, pkt_id, ptr);
						ptr = write_pkt_payload(slice[index].buffer + pkt_id * pkt_payload_size, pkt_payload_size, ptr);

                        if (is_slice_end)
						{
							write_slice_info(num_pkts_in_slice, pkt_buffer + pkt_buffer_size - sizeof(slice_info_t));
						}

						const auto is_frame_end_pkt = is_frame_end && is_slice_end;
						if (is_frame_end_pkt)
                        {
                            std::memcpy(
								pkt_buffer + pkt_buffer_size - sizeof(slice_info_t) - sizeof(cmd.pose.ts) - sizeof(pipeline_stats),
								&pipeline_stats,
								sizeof(pipeline_stats));
                            std::memcpy(
								pkt_buffer + pkt_buffer_size - sizeof(slice_info_t) - sizeof(cmd.pose.ts),
								&cmd.pose.ts,
								sizeof(cmd.pose.ts));
                        }

                        sendto(
							sock,
							pkt_buffer, pkt_buffer_size, 0,
							reinterpret_cast<sockaddr *>(&client_addr), sizeof(client_addr));
                    } // for(pkt_id)

                    stream_elapsed += esp_timer_get_time();
                } // for(slice_id)

                pipeline_stats.stream_elapsed = stream_elapsed;
            }
            else
            {
                ESP_LOGE(TAG, "Failed to receive upstream pose! errno=%d", errno);
                break;
            }
        } // inner loop

        if (sock != -1)
        {
            ESP_LOGE(TAG, "Shutting down socket and restarting...");
            shutdown(sock, 0);
            close(sock);
        }
    } // outer loop

    vTaskDelete(nullptr);
}

#ifdef __cplusplus
extern "C" {
#endif

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    slice[0].width  = frame_buffer_width;
    slice[0].height = frame_buffer_height;
    slice[0].buffer = reinterpret_cast<uint8_t*>(malloc(slice_buffer_size));

    slice[1].width  = frame_buffer_width;
    slice[1].height = frame_buffer_height;
    slice[1].buffer = reinterpret_cast<uint8_t*>(malloc(slice_buffer_size));

    init_renderer(frame_buffer_width, frame_buffer_height);

    xTaskCreatePinnedToCore(render_task, "render_task", 4096, nullptr, 5, &render_task_handle, 1);

#ifdef CONFIG_EXAMPLE_IPV4
    xTaskCreatePinnedToCore(stream_task, "stream_task", 4096, reinterpret_cast<void*>(AF_INET), 5, &stream_task_handle, 0);
#endif
#ifdef CONFIG_EXAMPLE_IPV6
    xTaskCreate(network_task, "network_task", 4096, reinterpret_cast<void*>(AF_INET6), 5, nullptr);
#endif

}

#ifdef __cplusplus
}
#endif

