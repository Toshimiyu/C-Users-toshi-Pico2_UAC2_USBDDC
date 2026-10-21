/*
* Copyright (c) 2026 Toshimiyu 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "fft_task.h"

#define FFT_LEN 2048
#define FFT_RING_BUFFER_FRAMES 512

typedef struct {
stereo_frame_t buffer[FFT_RING_BUFFER_FRAMES];
atomic_uint writer_index;
atomic_uint reader_index;
} fft_ring_buffer_t;

static fft_ring_buffer_t g_fft_ring_buf;
static stereo_frame_t g_fft_work_l[FFT_LEN];
static stereo_frame_t g_fft_work_r[FFT_LEN];

atomic_uint_fast32_t g_fft_drop_count = 0;

void core0_push_fft_sample(int32_t l, int32_t r) {
uint32_t w = atomic_load_explicit(&g_fft_ring_buf.writer_index, memory_order_relaxed);
uint32_t r_idx = atomic_load_explicit(&g_fft_ring_buf.reader_index, memory_order_acquire);
uint32_t next_w = (w + 1) % FFT_RING_BUFFER_FRAMES;

if (next_w == r_idx) {
    atomic_fetch_add_explicit(&g_fft_drop_count, 1, memory_order_relaxed);
    return;
}

g_fft_ring_buf.buffer[w].left = l;
g_fft_ring_buf.buffer[w].right = r;
atomic_store_explicit(&g_fft_ring_buf.writer_index, next_w, memory_order_release);
}

void core1_fft_task_init(void) {
atomic_init(&g_fft_ring_buf.writer_index, 0);
atomic_init(&g_fft_ring_buf.reader_index, 0);
atomic_init(&g_fft_drop_count, 0);
}

static void fft_buffer_flush(void) {
uint32_t w = atomic_load_explicit(&g_fft_ring_buf.writer_index, memory_order_acquire);
atomic_store_explicit(&g_fft_ring_buf.reader_index, w, memory_order_release);
}

void core1_fft_processing_loop(void) {
uint32_t last_drop_count = atomic_load_explicit(&g_fft_drop_count, memory_order_relaxed);
uint32_t sample_collected = 0;

while (sample_collected < FFT_LEN) {
    uint32_t current_drop = atomic_load_explicit(&g_fft_drop_count, memory_order_relaxed);
    if (current_drop != last_drop_count) {
        fft_buffer_flush();
        sample_collected = 0;
        last_drop_count = current_drop;
        continue;
    }

    uint32_t r_idx = atomic_load_explicit(&g_fft_ring_buf.reader_index, memory_order_relaxed);
    uint32_t w = atomic_load_explicit(&g_fft_ring_buf.writer_index, memory_order_acquire);

    if (r_idx == w) {
        tight_loop_contents();
        continue;
    }

    g_fft_work_l[sample_collected].left = g_fft_ring_buf.buffer[r_idx].left;
    g_fft_work_r[sample_collected].right = g_fft_ring_buf.buffer[r_idx].right;

    r_idx = (r_idx + 1) % FFT_RING_BUFFER_FRAMES;
    atomic_store_explicit(&g_fft_ring_buf.reader_index, r_idx, memory_order_release);
    sample_collected++;
}

if (atomic_load_explicit(&g_fft_drop_count, memory_order_relaxed) != last_drop_count) {
    fft_buffer_flush();
    return;
}

// Lch / Rch 独立11バンドFFT演算およびPico Display Pack 2.0描画処理
}