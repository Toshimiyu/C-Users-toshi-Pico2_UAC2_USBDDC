/*
* Copyright (c) 2026 Toshimiyu 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#include "audio_ringbuf.h"

void audio_ringbuf_init(audio_ring_buffer_t *rb) {
atomic_init(&rb->writer_index, 0);
atomic_init(&rb->reader_index, 0);
}

uint32_t audio_ringbuf_get_count(const audio_ring_buffer_t *rb) {
uint32_t w = atomic_load_explicit(&rb->writer_index, memory_order_relaxed);
uint32_t r = atomic_load_explicit(&rb->reader_index, memory_order_acquire);
return (w >= r) ? (w - r) : (AUDIO_RING_BUFFER_FRAMES - (r - w));
}

uint32_t audio_ringbuf_get_free(const audio_ring_buffer_t *rb) {
return AUDIO_RING_BUFFER_FRAMES - audio_ringbuf_get_count(rb) - 1;
}

bool audio_ringbuf_push(audio_ring_buffer_t *rb, const stereo_frame_t *frame) {
uint32_t w = atomic_load_explicit(&rb->writer_index, memory_order_relaxed);
uint32_t r = atomic_load_explicit(&rb->reader_index, memory_order_acquire);
uint32_t next_w = (w + 1) % AUDIO_RING_BUFFER_FRAMES;

if (next_w == r) {
    return false;
}

rb->buffer[w] = *frame;
atomic_store_explicit(&rb->writer_index, next_w, memory_order_release);
return true;
}

bool audio_ringbuf_pop(audio_ring_buffer_t *rb, stereo_frame_t *frame) {
uint32_t w = atomic_load_explicit(&rb->writer_index, memory_order_acquire);
uint32_t r = atomic_load_explicit(&rb->reader_index, memory_order_relaxed);

if (r == w) {
    return false;
}

*frame = rb->buffer[r];
uint32_t next_r = (r + 1) % AUDIO_RING_BUFFER_FRAMES;
atomic_store_explicit(&rb->reader_index, next_r, memory_order_release);
return true;
}