#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "ring_buffer.h"
#include "audio_proto.h"
#include "i2s_output.h"
#include "udp_tx.h"

static const char *TAG = "udp_tx";

static ring_buffer_t     *s_rb;
static int                s_sock = -1;
static struct sockaddr_in s_peer;          /* satellite address, learned from its hello */
static bool               s_have_peer = false;
static udp_tx_stats_t     s_stats;

static int open_socket(void)
{
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s < 0) {
        ESP_LOGE(TAG, "socket() failed: errno %d", errno);
        return -1;
    }
    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(AUDIO_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind() failed: errno %d", errno);
        close(s);
        return -1;
    }
    return s;
}

/* Non-blocking. If a hello arrived, its sender becomes the destination. */
static void poll_hello(void)
{
    hello_pkt_t h;
    struct sockaddr_in src;
    socklen_t slen = sizeof(src);

    int n = recvfrom(s_sock, &h, sizeof(h), MSG_DONTWAIT,
                     (struct sockaddr *)&src, &slen);
    if (n != sizeof(h) || h.magic != HELLO_MAGIC) {
        return;
    }
    if (!s_have_peer ||
        src.sin_addr.s_addr != s_peer.sin_addr.s_addr ||
        src.sin_port        != s_peer.sin_port) {
        ESP_LOGI(TAG, "satellite registered: %s:%u",
                 inet_ntoa(src.sin_addr), ntohs(src.sin_port));
    }
    s_peer      = src;
    s_have_peer = true;
}

/* ENOMEM = WiFi TX queue full. Wait a tick and retry, which pushes back on the
   ring buffer instead of dropping; give up after 5 tries. */
static void send_to_peer(const void *buf, size_t len)
{
    for (int attempt = 0; attempt < 5; attempt++) {
        int n = sendto(s_sock, buf, len, 0,
                       (struct sockaddr *)&s_peer, sizeof(s_peer));
        if (n >= 0) {
            s_stats.sent++;
            return;
        }
        if (errno != ENOMEM) {
            break;
        }
        s_stats.enomem++;
        vTaskDelay(1);
    }
    s_stats.gave_up++;
}

/* Reads one payload from the ring buffer, adds the header, sends it. */
static void udp_tx_task(void *arg)
{
    uint8_t pkt[AUDIO_PKT_MAX] __attribute__((aligned(4)));
    uint8_t *payload = pkt + sizeof(audio_pkt_hdr_t);   /* rb_read copies straight in here */
    uint32_t seq = 0;
    uint64_t frame_pos = 0;

    for (;;) {
        poll_hello();

        /* Wait for a full payload; yield only when there isn't one. */
        if (rb_available_read(s_rb) < AUDIO_PAYLOAD_BYTES) {
            vTaskDelay(1);
            continue;
        }

        rb_read(s_rb, payload, AUDIO_PAYLOAD_BYTES);

        /* frame_pos is media time: it advances even with nobody listening. */
        uint64_t chunk_pos = frame_pos;   /* first frame of this chunk */
        frame_pos += AUDIO_PAYLOAD_BYTES / AUDIO_BYTES_PER_FRAME;

        if (!s_have_peer) {
            continue;
        }

        audio_pkt_hdr_t hdr = {
            .magic       = AUDIO_MAGIC,
            .seq         = seq++,
            .frame_pos   = chunk_pos,
            .payload_len = AUDIO_PAYLOAD_BYTES,
            .volume      = i2s_output_get_volume(),
        };
        memcpy(pkt, &hdr, sizeof(hdr));   /* header goes in front of the payload */

        send_to_peer(pkt, sizeof(hdr) + AUDIO_PAYLOAD_BYTES);
    }
}

void udp_tx_get_stats(udp_tx_stats_t *out)
{
    *out = s_stats;
}

void udp_tx_start(ring_buffer_t *rb)
{
    s_rb   = rb;
    s_sock = open_socket();
    if (s_sock < 0) {
        ESP_LOGE(TAG, "not starting");
        return;
    }
    xTaskCreate(udp_tx_task, "udp_tx", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "listening for satellites on :%d", AUDIO_PORT);
}
