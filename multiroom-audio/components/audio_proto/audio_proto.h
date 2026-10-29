#pragma once

#include <stdint.h>
#define NET_SSID              "Multiroom-Audio"
#define NET_CHANNEL           6

#define AUDIO_PORT            5005
#define HELLO_BCAST_IP        "192.168.4.255"

#define AUDIO_MAGIC           0x4F445541u
#define HELLO_MAGIC           0x4F4C4548u

#define AUDIO_BYTES_PER_FRAME 4
#define AUDIO_PAYLOAD_BYTES   1408

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t seq;
    uint64_t frame_pos;
    uint16_t payload_len;
    uint8_t  volume;
    uint8_t  reserved;
} audio_pkt_hdr_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
} hello_pkt_t;

#define AUDIO_PKT_MAX (sizeof(audio_pkt_hdr_t) + AUDIO_PAYLOAD_BYTES)

_Static_assert(sizeof(audio_pkt_hdr_t) == 20, "header layout changed");
_Static_assert(AUDIO_PKT_MAX <= 1472, "packet would fragment");
_Static_assert(AUDIO_PAYLOAD_BYTES % AUDIO_BYTES_PER_FRAME == 0, "payload not whole frames");
