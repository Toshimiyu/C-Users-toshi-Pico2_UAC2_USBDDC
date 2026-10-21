/*
* Copyright (c) 2026 Toshimiyu 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#ifndef FFT_TASK_H
#define FFT_TASK_H

#include <stdint.h>
#include <stdatomic.h>
#include "audio_ringbuf.h"

void core0_push_fft_sample(int32_t l, int32_t r);
void core1_fft_task_init(void);
void core1_fft_processing_loop(void);

#endif