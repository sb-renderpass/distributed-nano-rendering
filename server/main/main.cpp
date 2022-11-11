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

#include "common/config.hpp"
#include "common/protocol.hpp"
#include "raycaster.hpp"
#include "types.hpp"

#define PORT CONFIG_EXAMPLE_PORT

static const char *TAG = "server";

TaskHandle_t render_task_handle {nullptr};
TaskHandle_t stream_task_handle {nullptr};

render_command_t cmd;
protocol::frame_info_t frame_info;

// Double-buffered slice for simultaneous render and stream
encoded_slice_t slice[2];

auto render_task(void* params) -> void
{
    constexpr auto slice_width = config::common::screen_width / config::common::num_slices;

    for (;;)
    {
		// Wait for network thread to start a new frame
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        auto index = 0U;
        auto render_elapsed = 0U;

        for (auto slice_id = 0; slice_id < config::common::num_slices; slice_id++)
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

        frame_info.render_time_us = render_elapsed;
    }

    vTaskDelete(nullptr);
}

auto stream_task(void* params) -> void
{
    int addr_family = (int)params;
    int ip_protocol = 0;
    struct sockaddr_in6 client_addr;
    socklen_t socklen = sizeof(client_addr);

    uint8_t pkt_buffer[config::common::pkt_buffer_size];

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

        const auto frame_info_ptr = pkt_buffer + config::common::pkt_buffer_size - sizeof(frame_info);

        for (;;)
        {
            const int recv_nbytes = recvfrom(
				sock,
				&cmd, sizeof(cmd), 0,
				reinterpret_cast<struct sockaddr*>(&client_addr), &socklen);

            frame_info.timestamp = cmd.pose.ts;

            if (recv_nbytes == sizeof(cmd))
            {
				// Notify render thread to start a new frame when a new pose is received
				xTaskNotifyGive(render_task_handle);

                auto stream_elapsed = 0U;

                for (auto slice_id = 0; slice_id < config::common::num_slices; slice_id++)
                {
					// Wait for render thread to signal that the rendered slice is ready for streaming
					const auto index = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

                    stream_elapsed -= esp_timer_get_time();

                    auto slice_ptr = slice[index].buffer;
                    const auto slice_end = slice[index].buffer + slice[index].size;

                    auto pkt_id = 0;
                    auto is_slice_end = false;
                    while (slice_ptr < slice_end)
                    {
                        // Split encoded slice buffer into as many full packets as possible
                        constexpr auto max_pkt_payload_size = config::common::pkt_buffer_size - sizeof(protocol::pkt_info_t);
                        constexpr auto min_pkt_payload_size = max_pkt_payload_size - sizeof(protocol::frame_info_t);
                        const auto rem_size = slice_end - slice_ptr;
                        const auto payload_size = std::min<int>(rem_size, max_pkt_payload_size);
                        is_slice_end = rem_size <= min_pkt_payload_size;

						auto pkt_ptr = pkt_buffer;
                        pkt_ptr = protocol::write_pkt_info(is_slice_end, 1, slice_id, pkt_id, pkt_ptr);
						pkt_ptr = protocol::write_payload(slice_ptr, payload_size, pkt_ptr);

                        // Add frame info to last packet if there is space
                        const auto frame_end = (slice_id == (config::common::num_slices - 1));
                        if (frame_end && is_slice_end) protocol::write(frame_info, frame_info_ptr);

                        sendto(
							sock,
							pkt_buffer, config::common::pkt_buffer_size, 0,
							reinterpret_cast<sockaddr *>(&client_addr), sizeof(client_addr));

                        pkt_id++;
                        slice_ptr += payload_size;
                    }

                    // Send frame info as an additional packet if ther was no space in the last packet
                    if (!is_slice_end)
                    {
                        protocol::write_pkt_info(1, 0, config::common::num_slices - 1, pkt_id, pkt_buffer);
                        protocol::write(frame_info, frame_info_ptr);
                        sendto(
							sock,
							pkt_buffer, config::common::pkt_buffer_size, 0,
							reinterpret_cast<sockaddr *>(&client_addr), sizeof(client_addr));
                    }

                    stream_elapsed += esp_timer_get_time();
                } // for(slice_id)

                frame_info.stream_time_us = stream_elapsed;
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

    slice[0].width  = config::common::screen_width;
    slice[0].height = config::common::screen_height;
    slice[0].buffer = reinterpret_cast<uint8_t*>(malloc(config::common::slice_buffer_size));

    slice[1].width  = config::common::screen_width;
    slice[1].height = config::common::screen_height;
    slice[1].buffer = reinterpret_cast<uint8_t*>(malloc(config::common::slice_buffer_size));

    init_renderer(config::common::screen_width, config::common::screen_height);

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

