/*
 * Copyright (c) 2025 ArqAlice
 *
 * Released under the MIT license
 * https://opensource.org/licenses/mit-license.php
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/i2c.h"
#include "hardware/watchdog.h"
#include "hardware/vreg.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "hardware/regs/intctrl.h"
#include "common.h"
#include "usb_device_control.h"
#include "transmit_to_dac.h"
#include "upsampling.h"
#include "ringbuffer.h"
#include "debug_with_gpio.h"
#include "ess_specific.h"
#include "nonblocking_i2c.h"
#include "display_rta.h"
#include "spectrum.h"
#include "debug_gauge.h"

// パワー管理
volatile bool is_high_power_mode = true;

// 処理タイミング制御用
#define MILLISEC50 (500000 / TIMER0_US)

// タイマー割り込み
struct repeating_timer timer0; // デジタルフィルタ演算を割り込みでトリガする

// ring buffer
RINGBUFFER buffer_ep_Lch;
RINGBUFFER buffer_ep_Rch;
RINGBUFFER buffer_upsr_data_Lch_0;
RINGBUFFER buffer_upsr_data_Rch_0;

// I2C ring buffer
I2C_RINGBUFFER i2c_ringbuffer0;

// Audio State
AUDIO_STATE audio_state;
uint32_t now_playing = 0;
static uint32_t now_playing_old = 0;
static bool is_cleared_buffer = false;

// 出力開始時間
volatile absolute_time_t time_start_output;

bool __not_in_flash_func(core0_timer_callback)(struct repeating_timer *t);

// TinyUSB は tud_task() をポーリングで回す必要がある。起動演出中の待機でも
// USB 処理とアップサンプリングを止めないための sleep_ms() 代替。
static void boot_wait_ms(uint32_t ms)
{
	absolute_time_t end = make_timeout_time_ms(ms);
	while (!time_reached(end))
	{
		usb_audio_task();
#if USB_BRINGUP_LEVEL >= 2
		upsampling_process_core0();
#endif
		sleep_us(1);
	}
}

// I2C送信インターバル
volatile absolute_time_t time_start_i2c_transfer = 0;

// 前回表示したオーディオフォーマット保持用 (追加)
static uint32_t last_display_freq = 0;
static uint8_t last_display_bit_depth = 0;
extern volatile uint32_t audio_last_data_us;
extern volatile bool audio_data_seen;

// Core1メイン
extern void core1_main();

// ミュート解除タイミング確認用
extern volatile bool enable_output;

void cancel_timer0(void)
{
	cancel_repeating_timer(&timer0);
}

void restart_timer0(void)
{
	add_repeating_timer_us(-TIMER0_US, core0_timer_callback, NULL, &timer0);
}

// アップサンプリング処理のタイミングをセットする
bool __not_in_flash_func(core0_timer_callback)(struct repeating_timer *t)
{
	// ES9038Q2Mの周波数切り替え時のノイズ対策
	if (USE_ESS_DAC && KIND_ESS_DAC == ES9038Q2M && get_ess_dac_mute())
	{
		if (enable_output)
		{
			int64_t elapsed_us = absolute_time_diff_us(time_start_output, get_absolute_time());
			if (elapsed_us > TIME_ES9038Q2M_DEPOP_USEC)
			{
				ess_dac_unmute();
			}
		}
	}

	// volatile static uint32_t now_playing_old = 0;
	static volatile int count = 0;
	count++;
	if (count >= MILLISEC50)
	{
		// パワーモード切り替え
		if ((gpio_get(POWER_MODE_SWITCH_PIN) || ALWAYS_HIGH_POWER) && (!ALWAYS_LOW_POWER))
		{
			// HiPowerMode
			if (!is_high_power_mode)
			{
				is_high_power_mode = true;
				if (USE_ESS_DAC && KIND_ESS_DAC == ES9038Q2M)
					ess_dac_mute();
				clear_ringbuffer(&buffer_ep_Lch);
				clear_ringbuffer(&buffer_ep_Rch);
				clear_ringbuffer(&buffer_upsr_data_Lch_0);
				clear_ringbuffer(&buffer_upsr_data_Rch_0);
				clear_bq_filter_delay();
				renew_clock(is_high_power_mode);
			}
		}
		else
		{
			// LoPowerMode
			if (is_high_power_mode)
			{
				is_high_power_mode = false;
				if (USE_ESS_DAC && KIND_ESS_DAC == ES9038Q2M)
					ess_dac_mute();
				clear_ringbuffer(&buffer_ep_Lch);
				clear_ringbuffer(&buffer_ep_Rch);
				clear_ringbuffer(&buffer_upsr_data_Lch_0);
				clear_ringbuffer(&buffer_upsr_data_Rch_0);
				clear_bq_filter_delay();
				renew_clock(is_high_power_mode);
			}
		}

		gpio_put(ONBOARD_LED_PIN, is_high_power_mode);


		// 再生停止時にアップサンプリングフラグとバッファをクリアする
		if ((now_playing == now_playing_old) && (!is_cleared_buffer))
		{
			clear_ringbuffer(&buffer_ep_Lch);
			clear_ringbuffer(&buffer_ep_Rch);
			clear_ringbuffer(&buffer_upsr_data_Lch_0);
			clear_ringbuffer(&buffer_upsr_data_Rch_0);
			// clear_i2c_ringbuffer(&i2c_ringbuffer0);
			clear_bq_filter_delay();
			// i2c_dma_stop_and_clear();
			renew_clock(is_high_power_mode);
			now_playing = 0;
			is_cleared_buffer = true;
		}
		else if (now_playing != now_playing_old)
		{
			is_cleared_buffer = false;
		}

		now_playing_old = now_playing;

		count = 0;
	}
	return true;
}

int main(void)
{
	// 外部電源有効化ピン
	if (USE_EXT_POWER_ENABLE)
	{
		gpio_init(EXT_POWER_ENABLE_PIN);
		gpio_set_dir(EXT_POWER_ENABLE_PIN, GPIO_OUT);
		gpio_put(EXT_POWER_ENABLE_PIN, false);
		sleep_us(BOOT_WAIT_TIME_US);
		gpio_put(EXT_POWER_ENABLE_PIN, true);
	}
	else
	{
		sleep_us(BOOT_WAIT_TIME_US);
	}

	// 動作電圧とクロックを引き上げる
	vreg_set_voltage(V_CORE_HI);
	sleep_ms(2);
	set_sys_clock_khz(SYS_CLOCK_KHZ_44K, true);
	sleep_us(1);

	// UARTは使用しない（stdout_uart_init() を削除。pico_enable_stdio_uart(... 0) と対）

	// テストモード用ピンを有効化
	if (TEST_MODE)
	{
		gpio_init(TEST_PIN1);
		gpio_init(TEST_PIN2);
		gpio_set_dir(TEST_PIN1, GPIO_OUT);
		gpio_set_dir(TEST_PIN2, GPIO_OUT);
	}

	// 各種バッファ初期化
	initialize_ringbuffer(SIZE_EP_BUFFER, true, &buffer_ep_Lch);				// USB EP受け取り用
	initialize_ringbuffer(SIZE_EP_BUFFER, true, &buffer_ep_Rch);				// USB EP受け取り用
	initialize_ringbuffer(SIZE_UPSAMPLE_CORE0, false, &buffer_upsr_data_Lch_0); // Core1転送用
	initialize_ringbuffer(SIZE_UPSAMPLE_CORE0, false, &buffer_upsr_data_Rch_0); // Core1転送用

	// オーディオステータス初期化
	audio_state.freq = AUDIO_INITIAL_FREQ;
	audio_state.bit_depth = 16;
	audio_state.mute = false;

	// Show the boot screen before the slower peripheral and DSP setup. The splash
	// routine returns immediately, so initialization continues with it visible.
	display_rta_hw_init();
	display_rta_clear_screen();
	
	display_rta_draw_splash_screen();

	// パワーモード切り替え用
	gpio_init(POWER_MODE_SWITCH_PIN);
	gpio_set_dir(POWER_MODE_SWITCH_PIN, GPIO_IN);
	gpio_pull_down(POWER_MODE_SWITCH_PIN);

	// オンボードLED点灯用
	gpio_init(ONBOARD_LED_PIN);
	gpio_set_dir(ONBOARD_LED_PIN, GPIO_OUT);
	gpio_put(ONBOARD_LED_PIN, true);

	// DACチップ制御用I2Cの初期化
	setup_I2C();

	// DCDCの動作モード、trueでFPWM、falseでPFM
	gpio_init(DCDC_MODE_PIN);
	gpio_set_dir(DCDC_MODE_PIN, true);
	gpio_put(DCDC_MODE_PIN, true);

	// ESS DACを初期化
	if (USE_ESS_DAC)
	{
		ess_dac_initialize();
		sleep_ms(10);
	}

	// アップサンプリング処理用Timer割り込みをアタッチする
	add_repeating_timer_us(-TIMER0_US, core0_timer_callback, NULL, &timer0);

	// Core1を起動する Core1ではI2S出力処理をしている
	multicore_launch_core1(core1_main);

	// Activate DAC
	if (USE_ESS_DAC)
	{
		ess_dac_activate();
		sleep_ms(10);
	}

	// ESS DAC SETUP
	if (USE_ESS_DAC)
	{
    	ess_dac_i2c_setup();
    	initialize_i2c_ringbuffer(
        	SIZE_I2C_RINGBUFFER,
        	&i2c_ringbuffer0
    	);
	}


	// アップサンプリングフィルタを初期化する
	init_upsampling_filter();

	spectrum_init();
	spectrum_set_sample_rate(audio_state.freq);



	// USBを先に公開する
	usb_sound_card_init();
	boot_wait_ms(100);

	//起動画面描画
	display_rta_set_boot_stage(0);
	boot_wait_ms(1000);

	display_rta_set_boot_stage(1);
	boot_wait_ms(1000);

	display_rta_set_boot_stage(2);
	boot_wait_ms(1000);

	display_rta_set_boot_stage(3);
	boot_wait_ms(1000);

	display_rta_set_boot_stage(4);
	boot_wait_ms(1000);

	display_rta_set_boot_stage(5);
	boot_wait_ms(1000);

	// FFT表示画面へ切り替え
	display_rta_init();
	debug_gauge_init(); // FFT画面の水位ゲージ（DEBUG_GAUGE_ENABLE=0で無効）

	// watchdog_enable(50, 1);

	while (true)
	{
		// watchdog_update();

		// TinyUSB device task + PCM受信 -> buffer_ep_Lch/Rch + Feedback制御 + 状態管理
		usb_audio_task();

		if (TEST_MODE)
			gpio_put(TEST_PIN1, true);
		upsampling_process_core0();
		if (TEST_MODE)
			gpio_put(TEST_PIN1, false);

		// ESS DAC用I2C送信処理
		if (USE_ESS_DAC)
		{
			int64_t elapsed_us = absolute_time_diff_us(time_start_i2c_transfer, get_absolute_time());
			static int size_transfer = 0;

			if ((!i2c_dma_is_busy()) && (elapsed_us >= I2C_ESS_DAC_TRANSFER_INTERVAL_USEC * (size_transfer + 1)))
			{
				uint16_t size_using = i2c_ringbuf_get_size_using(&i2c_ringbuffer0);
				if (size_using > 0)
				{
					I2C_RB_DATA buffer;
					i2c_ringbuf_read(&buffer, &i2c_ringbuffer0);
					i2c_write_dma(buffer.i2c, buffer.addr_7bit, buffer.data, buffer.len, buffer.nostop);
					size_transfer = buffer.len;
					time_start_i2c_transfer = get_absolute_time();
				}
			}
		}

		sleep_us(1);

		// --- ディスプレイ制御処理 ---
		
		// USBオーディオフォーマット（周波数 / ビット深度）に変更があれば表示を更新
    	if (audio_state.freq != last_display_freq || audio_state.bit_depth != last_display_bit_depth)
    	{
        last_display_freq = audio_state.freq;
        last_display_bit_depth = audio_state.bit_depth;
        
        // 周波数とビット深度を画面に反映
        display_rta_set_format(audio_state.bit_depth, (float)audio_state.freq / 1000.0f);
        spectrum_set_sample_rate(audio_state.freq);
    	}

	    // Monitor input inactivity on Core0; reset drawing is queued for the render service below.
	    static bool inactivity_screen_reset = false;
	    const bool audio_timed_out = audio_data_seen &&
	        ((uint32_t)(time_us_32() - audio_last_data_us) >= 200000u);
	    if (!audio_timed_out) inactivity_screen_reset = false;

	    // Clear once 200ms after the last non-empty USB audio packet, even after the output queue drains.
	    if (audio_timed_out && !inactivity_screen_reset)
	    {
        display_rta_reset_levels();
	        inactivity_screen_reset = true;
	    }

	    // TFT SPI drawing runs on Core0 in short steps and only with audio headroom.
	    display_rta_render_service();
	    debug_gauge_service();
	}
}
