/*
* Copyright (c) 2026 Toshimiyu 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include "pico/stdlib.h"
#include "tusb.h"
#include "audio_ringbuf.h"

#define TARGET_FILL_RATIO 0.5f

extern audio_ring_buffer_t g_audio_ring_buf;
extern atomic_uint_fast32_t g_sample_rate;
extern atomic_bool g_streaming;
extern atomic_uint_fast32_t g_overflow_drop_count;

void audiod_umac_rx_packet_cb(uint8_t rhport, uint8_t ep, const uint8_t *buffer, uint16_t len, uint8_t pcm_wd_size, uint8_t n_channels) {
uint32_t incoming_frames = len / (pcm_wd_size * n_channels);
uint32_t free_frames = audio_ringbuf_get_free(&g_audio_ring_buf);

if (free_frames < incoming_frames) {
    atomic_fetch_add_explicit(&g_overflow_drop_count, 1, memory_order_relaxed);
    atomic_store_explicit(&g_streaming, false, memory_order_release);
    return;
}

const uint8_t *p = buffer;
for (uint32_t i = 0; i < incoming_frames; i++) {
    stereo_frame_t frame = {0};

    if (pcm_wd_size == 2) {
        int16_t raw_l = (int16_t)(p[0] | (p[1] << 8));
        int16_t raw_r = (int16_t)(p[2] | (p[3] << 8));
        frame.left = ((int32_t)raw_l) << 16;
        frame.right = ((int32_t)raw_r) << 16;
        p += 4;
    } else if (pcm_wd_size == 3) {
        int32_t raw_l = (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16));
        if (raw_l & 0x00800000) raw_l |= 0xFF000000;
        int32_t raw_r = (int32_t)(p[3] | (p[4] << 8) | (p[5] << 16));
        if (raw_r & 0x00800000) raw_r |= 0xFF000000;
        frame.left = raw_l << 8;
        frame.right = raw_r << 8;
        p += 6;
    }

    audio_ringbuf_push(&g_audio_ring_buf, &frame);
}

atomic_store_explicit(&g_streaming, true, memory_order_release);
}

uint32_t feedback_pi_controller(void) {
static float integral = 0.0f;
const float kp = 0.1f;
const float ki = 0.01f;

uint32_t current_fill = audio_ringbuf_get_count(&g_audio_ring_buf);
float target_fill = (float)AUDIO_RING_BUFFER_FRAMES * TARGET_FILL_RATIO;
float error = (float)current_fill - target_fill;

integral += error;
if (integral >  1000.0f) integral =  1000.0f;
if (integral < -1000.0f) integral = -1000.0f;

float adjustment = (kp * error) + (ki * integral);
uint32_t base_rate = atomic_load_explicit(&g_sample_rate, memory_order_relaxed);
float nominal_fb = (float)base_rate * 65536.0f;
float fb_value = nominal_fb - adjustment * 65.536f;

return (uint32_t)fb_value;
}