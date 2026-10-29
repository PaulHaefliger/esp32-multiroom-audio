# esp32-multiroom-audio

Bluetooth-to-WiFi multi-room audio on ESP32, written from scratch in C on ESP-IDF v6 / FreeRTOS.

A phone streams music over Bluetooth A2DP to a **central** node, which forwards it as timestamped UDP packets over WiFi to **satellite** nodes that drive I2S class-D amplifiers. The goal is synchronized playback across rooms with under 5 ms of skew between speakers.

**Status:** end-to-end streaming works (phone → central → WiFi → satellite → speaker). Clock synchronization is the next phase and is not implemented yet.

---

## Architecture

```
  Phone                    Central (ESP32)                         Satellite (ESP32)
 ┌──────┐  A2DP   ┌──────────────────────────────┐  UDP   ┌─────────────────────────────────┐
 │      │ ──────► │ bt_sink ─► ring buffer ─► udp_tx │ ────► │ udp_rx ─► ring buffer ─► i2s_output │ ─► MAX98357A ─► speaker
 └──────┘  (SBC)  │          (32 KB, ~185 ms)        │ WiFi  │          (64 KB, ~370 ms)           │
                  │ WiFi station                     │       │ WiFi SoftAP (hosts the network)     │
                  └──────────────────────────────────┘       └─────────────────────────────────────┘
```

- **Central** is a pure distribution hub: it never plays audio itself, so it can be the single clock master for the sync layer.
- **Satellites** buffer deeply and play out over I2S. The MAX98357A needs no MCLK and runs off the ESP32's I2S clock, which is what makes APLL-based drift correction possible later.
- Every stage is decoupled by a **lock-free single-producer/single-consumer ring buffer**, so a stall in one execution context (Bluetooth stack, WiFi driver, I2S DMA) never blocks another.
- The Bluetooth data callback runs in the BT stack's own task and **never blocks**: it writes a whole chunk or drops it, then returns.

### Wire protocol

UDP, unicast, port 5005. Each packet is a 20-byte header plus 1408 bytes of 16-bit stereo PCM (352 frames, ~8 ms at 44.1 kHz), which comes to 125 packets/s and ~176 kB/s. Packets stay under the 1472-byte UDP limit, so they never fragment.

| Field | Size | Purpose |
|---|---|---|
| `magic` | 4 B | reject stray traffic |
| `seq` | 4 B | loss and reorder detection (wrap-safe signed delta) |
| `frame_pos` | 8 B | index of the first frame: the presentation timestamp the sync layer will schedule on |
| `payload_len` | 2 B | PCM bytes that follow |
| `volume` | 1 B | AVRCP absolute volume (0–127), applied on the satellite |
| `reserved` | 1 B | |

On startup the satellite broadcasts a small *hello* every second until audio arrives. Central learns the satellite's address from the hello's source and unicasts to it. The same request/response channel will carry the clock-sync round trips.

UDP rather than TCP for the same reason A2DP's media channel isn't retransmitted: a late audio packet is worthless. Loss is absorbed by buffer depth instead of stalling the stream on retransmits.

---

## Engineering notes

The bugs that shaped the design.

### 1. Bluetooth/WiFi coexistence collapse → inverted the network topology

The ESP32 has one 2.4 GHz radio, shared between Bluetooth and WiFi by time-division.

- **Original design:** central hosted the WiFi access point. A coexistence test with ping traffic passed.
- **What broke:** with real audio and Bluetooth streaming at the same time, central sent **~15 of the required 125 packets/s**. The WiFi TX queue returned `ENOMEM` on every 10 ms tick, and the ring buffer overflowed upstream.
- **Isolating the cause:** I swapped Bluetooth for a locally generated 440 Hz tone at the exact audio rate. The same link then carried the full 125 packets/s with almost no loss. So the link was fine, and coexistence was the problem.
- **Root cause:** Espressif's coexistence table rates *SoftAP + Classic Bluetooth* as "supported, but unstable" in every state, and *station + Classic Bluetooth* as stable.
- **Fix:** the satellite, which has no Bluetooth, now hosts the access point, and central joins it as a station. Throughput went back to 125 packets/s, with 3 `ENOMEM` events in 80 s of streaming.
- **Trade-off:** with several satellites, the others have to join the satellite that hosts the access point, so their traffic is relayed. That's fine for a few rooms; a dedicated access point is the long-term answer.

### 2. White noise instead of music → sample misalignment from partial writes

When the ring buffer was nearly full, `rb_write` wrote as many bytes as fit. Usable space is `capacity − fill − 1`, which is often odd. One odd-length write shifts every later 16-bit sample by one byte, and the stream turns into full-scale noise until reboot.

**Fix:** every producer (Bluetooth callback, UDP receiver, test tone) writes a whole chunk or nothing, so the stream always stays a whole number of frames.

### 3. Reboot after ~80 s → heap exhaustion inside the Bluetooth stack

I had raised the WiFi TX buffer limit to 64 to ride out radio stalls. Each queued frame is a heap allocation, so during a stall the queue grew to about 100 KB. Bluedroid then needed 981 bytes when the largest free block was 928 bytes, `calloc` failed, an assert fired, and the board rebooted:

```
E BT_OSI: calloc failed (caller=0x4012a809 size=981)
E BT_OSI: heap info: free=1512, largest_block=928
assert failed: reassemble_and_dispatch packet_fragmenter.c:205
```

**Fix:** capped the TX buffers at 16 (~26 KB worst case) and let send-side retries push back on the ring buffer instead. A stall now costs a few packets instead of a reboot. Central logs `heap_min` (lowest free heap since boot) so this headroom stays visible.

### 4. Cross-core race in the ring buffer

The ring buffer was built and host-tested on its own before going on hardware. Concurrent testing found two bugs:

- **Inconsistent snapshot:** the available-bytes count was read twice in one expression.
- **Memory-ordering race:** on the dual-core target, the consumer could see the new `write_idx` before the data it covers.

Fixed with a release fence after the data copy and before publishing the index, and an acquire fence after reading it. Validated by streaming 4 MB through a 64-byte buffer with mismatched chunk sizes over repeated concurrent runs.

### Also

- **Latency control after a stall:** central sends its backlog in a burst. Without a cap, satellite buffer fill (which equals latency) jumped to ~100% and stayed there. The satellite now drops packets above 70% fill, and after an underrun it plays silence until the buffer is back to 50%.
- **AVRCP must be initialized before the A2DP sink.** This is the opposite of the order in Espressif's example. With the wrong order, the phone never delegates absolute volume.

---

## Hardware

- 2× ESP32-WROOM-32E DevKitC
- MAX98357A I2S DAC / class-D amplifier breakout on the satellite
- I2S pins: **BCLK = GPIO 26, LRCLK = GPIO 25, DOUT = GPIO 27**
- Breadboard prototype; a custom KiCad PCB is planned once validation is complete

## Project layout

```
multiroom-audio/              central node (BT in, WiFi out)
  components/
    audio_proto/              packet format + network constants (shared)
    ring_buffer/              lock-free SPSC ring buffer (shared)
    i2s_output/               I2S feed task + Q15 volume (shared; only the satellite plays)
    bt_sink/                  A2DP sink + AVRCP absolute volume
    wifi_sta/                 joins the satellite's SoftAP
    udp_tx/                   packetizer + sender with backpressure
  main/main.c                 TEST_TONE=1 swaps Bluetooth for a 440 Hz tone

satellite-audio/              satellite node (WiFi in, speaker out)
  components/
    wifi_ap/                  hosts the network
    udp_rx/                   receive, validate, loss accounting, latency clamp
  main/satellite-audio.c
```

The satellite reuses `ring_buffer`, `i2s_output` and `audio_proto` from `multiroom-audio/components/` through `EXTRA_COMPONENT_DIRS`, so keep both folders side by side.

## Build and run

Requires ESP-IDF v6.0.x.

```
# 1. Satellite first: it hosts the network
cd satellite-audio
idf.py set-target esp32
idf.py build flash monitor

# 2. Central
cd multiroom-audio
idf.py set-target esp32
idf.py build flash monitor
```

Then pair a phone with **ESP32-Multiroom** and play audio.

Both nodes print health lines every 2 s:

- **Central:** `sent`, `enomem`, `gave_up`, ring-buffer fill, `bt_drop`, `heap`, `heap_min`
- **Satellite:** `rx`, `lost`, `bad`, `trimmed`, `overflow`, plus buffer fill each second

## Roadmap

- [x] I2S tone and embedded-PCM playback
- [x] A2DP sink + lock-free ring buffer
- [x] AVRCP absolute volume (Q15 gain)
- [x] WiFi transport: central → satellite over UDP
- [ ] **Clock sync:** NTP-style round-trip offset estimation; playback scheduled on `frame_pos`
- [ ] **Drift correction:** fractional APLL tuning on each satellite, no sample insertion or deletion
- [ ] **Measured validation:** GPIO markers on both boards, skew measured with a logic analyzer
- [ ] Multiple satellites
- [ ] Custom PCB (KiCad)

## Known limitations

- **No synchronization yet.** Satellite latency is whatever its buffer fill happens to be, not a controlled value.
- **Only one satellite tested.**
- **Open network** (no WiFi authentication), suitable for a prototype only.
- **Not yet soak-tested** over long sessions.
- Occasional pops on the breadboard reproduce on the stored-PCM path with Bluetooth bypassed, so they're analog/wiring artifacts, deferred to the PCB.