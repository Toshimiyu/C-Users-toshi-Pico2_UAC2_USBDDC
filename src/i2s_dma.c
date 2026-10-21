/*
* Copyright (c) 2026 Toshimiyu 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#include "i2s_dma.h"
#include "hardware/i2s.h"
#include "hardware/dma.h"
#include "pico/stdlib.h"

void i2s_dma_init(uint32_t sample_rate) {
i2s_config_t i2s_config = i2s_get_default_config();
i2s_config.sample_rate = sample_rate;
i2s_config.bits_per_sample = 32;
i2s_configure(&i2s_config, sample_rate, 128000000);
}

void i2s_dma_start(void) {
i2s_enable(true);
}

void i2s_dma_stop(void) {
i2s_enable(false);
}