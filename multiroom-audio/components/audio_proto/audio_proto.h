#pragma once
/* Wire protocol and network constants shared by central and satellites. */

#include <stdint.h>

/* The satellite hosts this network; central joins it as a station. */
#define NET_SSID              "Multiroom-Audio"
#define NET_CHANNEL           6               /* channel 1 showed TX stalls */

#define AUDIO_PORT            5005
#define HELLO_BCAST_IP        "192.168.4.255" /* central's IP is DHCP-assigned, so hellos are broadcast */

#define AUDIO_MAGIC           0x4F445541u     /* "AUDO" */
#define HELLO_MAGIC           0x4F4C4548u     /* "HELO" */

#define AUDIO_BYTES_PER_FRAME 4               /* 16-bit stereo */
#define AUDIO_PAYLOAD_BYTES   1408            /* 352 frames, ~8 ms at 44.1 kHz */

/* Audio packet header, sent before every PCM payload (central -> satellite). */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t seq;          /* +1 per packet: loss / reorder detection */
    uint64_t frame_pos;    /* index of first frame: presentation timestamp for sync */
    uint16_t payload_len;  /* PCM bytes after the header */
    uint8_t  volume;       /* AVRCP volume 0..127, applied on the satellite */
    uint8_t  reserved;
} audio_pkt_hdr_t;

/* Satellite -> central: "send audio here". Repeated every 1 s of silence. */
typedef struct __attribute__((packed)) {
    uint32_t magic;
} hello_pkt_t;

#define AUDIO_PKT_MAX (sizeof(audio_pkt_hdr_t) + AUDIO_PAYLOAD_BYTES)

/* 1472 = 1500-byte WiFi MTU minus IP and UDP headers. */
_Static_assert(sizeof(audio_pkt_hdr_t) == 20, "header layout changed");
_Static_assert(AUDIO_PKT_MAX <= 1472, "packet would fragment");
_Static_assert(AUDIO_PAYLOAD_BYTES % AUDIO_BYTES_PER_FRAME == 0, "payload not whole frames");
