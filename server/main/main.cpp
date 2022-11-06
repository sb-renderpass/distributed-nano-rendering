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
#include "common/codec.hpp"

#define PORT CONFIG_EXAMPLE_PORT

static const char *TAG = "server";

namespace
{

constexpr auto frame_buffer_width  = 320 / 2;
constexpr auto frame_buffer_height = 240;
constexpr auto pkt_buffer_size     = 1400;
constexpr auto num_slices          = 4;

constexpr auto frame_buffer_size   = frame_buffer_width * frame_buffer_height;
constexpr auto slice_buffer_size   = frame_buffer_size / num_slices;

} // anonymous namespace

TaskHandle_t render_task_handle  {nullptr};
TaskHandle_t network_task_handle {nullptr};

render_command_t cmd;

frame_t slice[2];

struct pipeline_stats_t
{
    uint32_t render_elapsed  {0};
    uint32_t network_elapsed {0};
};
pipeline_stats_t pipeline_stats;

//light_t light {22.0F, 11.5F, -1.0F, 0.0F};

uint8_t bitstream_buffer[slice_buffer_size];
bitstream_t bitstream {bitstream_buffer, slice_buffer_size};

auto render_task(void* params) -> void
{
    constexpr auto slice_width = frame_buffer_width / num_slices;

    for (;;)
    {
        auto render_elapsed = 0U;

        for (auto slice_id = 0; slice_id < num_slices; slice_id++)
        {
            const auto slice_index = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            render_elapsed -= esp_timer_get_time();
            render_slice(cmd, slice_id * slice_width, (slice_id + 1) * slice_width, slice[slice_index]);

			/*
            // CODEC
			constexpr auto W = frame_buffer_height;
			constexpr auto H = frame_buffer_width / num_slices;
			codec::encode_slice(slice[slice_index].buffer, bitstream, W, 4);
			bitstream.flush();
			bitstream.clear();
			*/

            render_elapsed += esp_timer_get_time();
            if (slice_id == 0) xTaskNotifyGive(network_task_handle);
        }

        pipeline_stats.render_elapsed = render_elapsed;
    }

    vTaskDelete(nullptr);
}

inline auto write_pkt_header(uint16_t frame_num, bool padding, uint8_t seqnum, uint32_t offset, uint8_t* buffer) -> void
{
    buffer[0] = frame_num >> 8;
    buffer[1] = frame_num >> 0;
    buffer[2] = (padding << 7) | (seqnum & 0x7F);
    buffer[3] = offset >> 16;
    buffer[4] = offset >>  8;
    buffer[5] = offset;
}

auto network_task(void* params) -> void
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

        constexpr auto num_pkts_per_slice = static_cast<int>(std::ceil(static_cast<float>(slice_buffer_size) / pkt_buffer_size));
        constexpr auto num_pkts_per_frame = num_pkts_per_slice * num_slices;
        constexpr auto pkt_payload_size   = pkt_buffer_size - sizeof(pkt_header_t);
        constexpr auto rem_size_per_slice = slice_buffer_size % pkt_payload_size;
        ESP_LOGI(TAG, "num_pkts_per_frame=%d num_pkts_per_slice=%d rem_size_per_slice=%d",
                num_pkts_per_frame, num_pkts_per_slice, rem_size_per_slice);

        for (;;)
        {
            const int recv_nbytes = recvfrom(sock, &cmd, sizeof(cmd), 0, reinterpret_cast<struct sockaddr*>(&client_addr), &socklen);
            if (recv_nbytes == sizeof(cmd))
            {
                auto slice_index = 0U;
                xTaskNotify(render_task_handle, slice_index, eSetValueWithOverwrite);
                ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

                auto seqnum = 0U;
                auto offset = 0U;

                auto network_elapsed = 0U;

                for (auto slice_id = 0; slice_id < num_slices; slice_id++)
                {
                    if (slice_id < num_slices - 1) xTaskNotify(render_task_handle, !slice_index, eSetValueWithOverwrite);

                    network_elapsed -= esp_timer_get_time();

					/*
					// CODEC
					constexpr auto W = frame_buffer_height;
					constexpr auto H = frame_buffer_width / num_slices;
					codec::encode_slice(slice[slice_index].buffer, bitstream, W, H);
					bitstream.flush();
					bitstream.clear();
					*/

                    for (auto i = 0; i < num_pkts_per_slice; i++)
                    {
                        const auto is_last_pkt = i == num_pkts_per_slice - 1;
                        write_pkt_header(cmd.pose.num, is_last_pkt, seqnum, offset, pkt_buffer);

                        const auto payload_size = is_last_pkt ? rem_size_per_slice : pkt_payload_size;
                        std::memcpy(pkt_buffer + sizeof(pkt_header_t), slice[slice_index].buffer + i * pkt_payload_size, payload_size);

                        using padding_t = uint16_t;

                        if (is_last_pkt)
                        {
                            constexpr auto padding = static_cast<padding_t>(pkt_payload_size - rem_size_per_slice);
                            std::memcpy(pkt_buffer + pkt_buffer_size - sizeof(padding_t), &padding, sizeof(padding_t));
                        }

                        if (seqnum == num_pkts_per_frame - 1)
                        {
                            memcpy(pkt_buffer + pkt_buffer_size - sizeof(padding_t) - sizeof(cmd.pose.ts) - sizeof(pipeline_stats),
                                    &pipeline_stats, sizeof(pipeline_stats));
                            memcpy(pkt_buffer + pkt_buffer_size - sizeof(padding_t) - sizeof(cmd.pose.ts), &cmd.pose.ts, sizeof(cmd.pose.ts));
                        }

                        sendto(sock, pkt_buffer, pkt_buffer_size, 0, reinterpret_cast<sockaddr *>(&client_addr), sizeof(client_addr));

                        seqnum++;
                        offset += payload_size;
                    }
                    slice_index = !slice_index;

                    network_elapsed += esp_timer_get_time();
                }

                pipeline_stats.network_elapsed = network_elapsed;
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
    xTaskCreatePinnedToCore(network_task, "network_task", 4096, reinterpret_cast<void*>(AF_INET), 5, &network_task_handle, 0);
#endif
#ifdef CONFIG_EXAMPLE_IPV6
    xTaskCreate(network_task, "network_task", 4096, reinterpret_cast<void*>(AF_INET6), 5, nullptr);
#endif

}

#ifdef __cplusplus
}
#endif

