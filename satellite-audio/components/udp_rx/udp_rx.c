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
#include "udp_rx.h"

static const char *TAG = "udp_rx";

static ring_buffer_t     *s_rb;
static int                s_sock = -1;
static struct sockaddr_in s_hello_dst;   /* subnet broadcast address */
static udp_rx_stats_t     s_stats;

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
    /* recvfrom times out after 1 s of silence, so we can resend the hello
       (recovers if central reboots and forgets us). */
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* We don't know central's IP, so hellos are broadcast. */
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));

    s_hello_dst.sin_family = AF_INET;
    s_hello_dst.sin_port   = htons(AUDIO_PORT);
    inet_pton(AF_INET, HELLO_BCAST_IP, &s_hello_dst.sin_addr);
    return s;
}

static void send_hello(void)
{
    hello_pkt_t h = { .magic = HELLO_MAGIC };
    sendto(s_sock, &h, sizeof(h), 0,
           (struct sockaddr *)&s_hello_dst, sizeof(s_hello_dst));
}

/* Receive -> validate -> loss accounting -> ring buffer. */
static void udp_rx_task(void *arg)
{
    uint8_t pkt[AUDIO_PKT_MAX] __attribute__((aligned(4)));
    bool     have_last = false;
    uint32_t last_seq  = 0;

    send_hello();
    for (;;) {
        int n = recvfrom(s_sock, pkt, sizeof(pkt), 0, NULL, NULL);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                send_hello();
            } else {
                ESP_LOGW(TAG, "recvfrom errno %d", errno);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            continue;
        }

        if (n < (int)sizeof(audio_pkt_hdr_t)) {
            s_stats.bad++;
            continue;
        }

        /* Copy the header out instead of casting: the struct is packed. */
        audio_pkt_hdr_t hdr;
        memcpy(&hdr, pkt, sizeof(hdr));
        size_t payload_len = (size_t)n - sizeof(hdr);

        if (hdr.magic != AUDIO_MAGIC || hdr.payload_len != payload_len) {
            s_stats.bad++;
            continue;
        }

        if (have_last) {
            /* Unsigned subtraction survives wraparound; the signed cast tells
               ahead (>0) from duplicate/late (<=0). */
            int32_t delta = (int32_t)(hdr.seq - last_seq);
            if (delta <= 0) {   /* late or duplicate: worthless for a stream */
                s_stats.bad++;
                continue;
            }
            s_stats.lost += (uint32_t)(delta - 1);
        }
        last_seq  = hdr.seq;
        have_last = true;

        i2s_output_set_volume(hdr.volume);

        /* Latency clamp: after a stall central sends a catch-up burst. Drop
           above 70% fill so latency doesn't jump and stay high. */
        if (rb_available_read(s_rb) > s_rb->capacity * 7 / 10) {
            s_stats.trimmed++;
            s_stats.packets++;
            continue;
        }

        /* Whole packet or nothing, so samples never misalign. */
        if (rb_available_write(s_rb) < payload_len) {
            s_stats.overflow_bytes += payload_len;
            s_stats.packets++;
            continue;
        }
        rb_write(s_rb, pkt + sizeof(hdr), payload_len);

        s_stats.packets++;
    }
}

void udp_rx_start(ring_buffer_t *rb)
{
    s_rb   = rb;
    s_sock = open_socket();
    if (s_sock < 0) {
        ESP_LOGE(TAG, "not starting");
        return;
    }
    /* Priority 6, above the feed task (5), so incoming packets are drained promptly. */
    xTaskCreate(udp_rx_task, "udp_rx", 4096, NULL, 6, NULL);
}

void udp_rx_get_stats(udp_rx_stats_t *out)
{
    *out = s_stats;
}
