/*
* Copyright (c) 2026 Toshimiyu 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#ifndef AUDIO_RINGBUF_H
#define AUDIO_RINGBUF_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

#define AUDIO_RING_BUFFER_FRAMES 2048

typedef struct {
int32_t left;
int32_t right;
} stereo_frame_t;

typedef struct {
stereo_frame_t buffer[AUDIO_RING_BUFFER_FRAMES];
atomic_uint writer_index;
atomic_uint reader_index;
} audio_ring_buffer_t;

void audio_ringbuf_init(audio_ring_buffer_t *rb);
uint32_t audio_ringbuf_get_count(const audio_ring_buffer_t *rb);
uint32_t audio_ringbuf_get_free(const audio_ring_buffer_t *rb);
bool audio_ringbuf_push(audio_ring_buffer_t *rb, const stereo_frame_t *frame);
bool audio_ringbuf_pop(audio_ring_buffer_t *rb, stereo_frame_t *frame);

#endif