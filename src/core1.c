/*
* Copyright (c) 2026 Toshimiyu 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/irq.h"
#include "hardware/watchdog.h"
#include "hardware/sync.h"
#include "pico/multicore.h"

#include "common.h"
#include "transmit_to_dac.h"
#include "ringbuffer.h"
#include "debug_with_gpio.h"
#include "display_rta.h"

void core1_main()
{
	// Allow Core0 to pause this core while changing the shared audio clock,
	// resetting DMA, or clearing the stereo ring buffers.
	multicore_lockout_victim_init();

	// I2S初期化
	init_i2s_interface();

	while (true)
	{
		if(TEST_MODE) gpio_put(TEST_PIN2, true);
		dma_tx_start();
		// FFT calculation only; all blocking TFT writes are serviced by Core0.
		display_rta_service();
		if(TEST_MODE) gpio_put(TEST_PIN2, false);
		sleep_us(1);
	}
}
