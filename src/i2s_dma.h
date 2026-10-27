/*
* Copyright (c) 2026 Toshimiyu 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#ifndef I2S_DMA_H
#define I2S_DMA_H

#include <stdint.h>
#include <stdbool.h>

void i2s_dma_init(uint32_t sample_rate);
void i2s_dma_start(void);
void i2s_dma_stop(void);

#endif