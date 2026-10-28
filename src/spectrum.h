/*
* Copyright (c) 2026 Toshimiyu 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#ifndef SPECTRUM_H
#define SPECTRUM_H

#include <stdint.h>
#include <stdbool.h>

#define SPECTRUM_BAND_COUNT 11

void spectrum_init(void);
void spectrum_set_sample_rate(uint32_t sample_rate);
void spectrum_feed(float left_q31, float right_q31);
void spectrum_feed_block(const float *left_q31, const float *right_q31, uint32_t length);
bool spectrum_process(uint8_t left[SPECTRUM_BAND_COUNT], uint8_t right[SPECTRUM_BAND_COUNT]);

#endif
