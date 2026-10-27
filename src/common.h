/*
* Copyright (c) 2026 Toshimiyu 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#ifndef _COMMON_H_
#define _COMMON_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "ringbuffer.h"

// User Configurable ------------------------------------------------------------------

// Test mode
#define TEST_MODE (false)
#define TEST_PIN1 (1)
#define TEST_PIN2 (2)

// String Desc.
#define MFG_NAME ("Toshimiyu")
#define DEVICE_NAME ("Pico2 UAC2 USB DDC")
#define WEBSITE_ADDR ("toshimi.yuze@gmail.com:")

// Faster I2S slew rate
#define I2S_SLEWRATE_FAST_ENABLE (false)

// Enhancement I2S signal output current
#define I2S_STRENGTH_REINFORCE_ENABLE (false)

// Power Mode Switch Pin
// Power mode switch retained for clock and voltage selection
#define POWER_MODE_SWITCH_PIN (0)
#define ALWAYS_HIGH_POWER (true)
#define ALWAYS_LOW_POWER (false)

// I2S Pin : sideset0:BCLK, sideset1:LRCK (if No Changed)
#define I2S_SIDESET_CHANGE (false)
#define I2S_DATA_PIN (9)
#define I2S_SIDESET_BASE (10)

// Output level scale retained from the previous PCM-to-DAC path.
#define DEFAULT_GAIN_RATIO (0.80f)

// Other Function
#define USE_EXT_POWER_ENABLE (false)
#define EXT_POWER_ENABLE_PIN (3)
#define BOOT_WAIT_TIME_US (50000)

// User Configurable end ------------------------------------------------------------

// システムクロック
// #define SYS_CLOCK_KHZ_44K (412000)
// #define SYS_CLOCK_KHZ_48K (430000)
#define SYS_CLOCK_KHZ_44K (336000)
#define SYS_CLOCK_KHZ_48K (360000)
#define SYS_CLOCK_KHZ_LP_44K (208000)
#define SYS_CLOCK_KHZ_LP_48K (208000)
// 208M8/48k/64 = 67.968->68, 208M8/44k1/64 = 73.979->74

// Core Voltage
// #define V_CORE_HI VREG_VOLTAGE_1_50
// #define V_CORE_HI VREG_VOLTAGE_1_30
#define V_CORE_HI VREG_VOLTAGE_1_25
#define V_CORE_LO VREG_VOLTAGE_1_05


// 初期オーディオサンプル周波数
#define AUDIO_INITIAL_FREQ (44100)

// DCDC Control
#define DCDC_MODE_PIN (23)

// LED
#define ONBOARD_LED_PIN (25)

// Core0 Timer0 period (us) 雑多な処理用
#define TIMER0_US (250)

// DMA block duration used to size the interleaved I2S transfer buffers.
#define DMA_BLOCK_US (2200)

// エンドポイントバッファサイズ((96+1)kHz*1ms=97以上あればよい)
#define SIZE_EP_BUFFER (896)

// Audio ring capacity in stereo frames.
#define SIZE_AUDIO_RING_FRAMES (1024)

// DMA転送バッファサイズ 3以上あればよい
#define DEPTH_DMA_TX_BUFFER (4)

// FB水位(50%が望ましい)
#define SIZE_BUFFER_FB_THRESHOLD (SIZE_AUDIO_RING_FRAMES / 2)

// Feedback(±1サンプルになる値を返す 基準は1000)
#define FB_ADJ_LIMIT (1000)

// ボリューム管理
#define VOLUME_RESOLUTION (256)
#define MIN_VOLUME (-64 * VOLUME_RESOLUTION)
#define MAX_VOLUME (0 * VOLUME_RESOLUTION)
#define DEFAULT_VOLUME (0 * VOLUME_RESOLUTION)

typedef struct
{
	volatile uint32_t freq;		// USB control callback updates this asynchronously
	volatile uint32_t bit_depth; // USB alternate-setting callback updates this asynchronously
	int16_t now_volume;
	int16_t acq_volume;
	float vol_float;
	int16_t vol_mul;
	uint32_t vol_shift;
	bool mute;
} AUDIO_STATE;

extern RINGBUFFER buffer_audio;

extern AUDIO_STATE audio_state;
extern volatile bool is_high_power_mode;
extern uint32_t now_playing;
extern uint16_t length_remain_to_I2S_FIFO;
static inline uint32_t audio_ring_total_frames(void)
{
	return (uint32_t)get_size_using(&buffer_audio) / 2u;
}

extern uint32_t calc_pwm_period_us(float period_us, uint16_t prescale);
extern void volume_control(void);

extern void renew_clock(bool is_high_power);
extern void cancel_timer0(void);
extern void restart_timer0(void);

inline int32_t saturation_i32(int32_t in, int32_t max, int32_t min)
{
	if (in > max)
		return max;
	else if (in < min)
		return min;
	return in;
}

inline float saturation_f32(float in, float max, float min)
{
	if (in > max)
		return max;
	else if (in < min)
		return min;
	return in;
}

#endif
