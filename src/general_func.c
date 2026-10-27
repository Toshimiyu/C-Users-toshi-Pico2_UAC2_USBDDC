/*
* Copyright (c) 2026 Toshimiyu 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#include <math.h>
#include "common.h"
#include "hardware/vreg.h"
#include "hardware/clocks.h"
#include "transmit_to_dac.h"

void renew_cpu_clock(bool is_high_power)
{
	// CPUクロックを再設定
	switch (audio_state.freq)
	{
	case 192000:
	case 96000:
	case 48000:
		if (is_high_power)
		{
			vreg_set_voltage(V_CORE_HI);
			busy_wait_us(100);
			set_sys_clock_khz(SYS_CLOCK_KHZ_48K, true);
			busy_wait_us(50);
		}
		else
		{
			set_sys_clock_khz(SYS_CLOCK_KHZ_LP_48K, true);
			busy_wait_us(100);
			vreg_set_voltage(V_CORE_LO);
			busy_wait_us(50);
		}
		break;
	case 176400:
	case 88200:
	case 44100:
	default:
		if (is_high_power)
		{
			vreg_set_voltage(V_CORE_HI);
			busy_wait_us(100);
			set_sys_clock_khz(SYS_CLOCK_KHZ_44K, true);
			busy_wait_us(50);
		}
		else
		{
			set_sys_clock_khz(SYS_CLOCK_KHZ_LP_44K, true);
			busy_wait_us(100);
			vreg_set_voltage(V_CORE_LO);
			busy_wait_us(50);
		}
		break;
	}
}

void renew_clock(bool is_high_power)
{
	// Core0の割り込みタイマを停止
	cancel_timer0();

	// CPUクロックを再設定
	renew_cpu_clock(is_high_power);

	// Core0の割り込みタイマを再開
	restart_timer0();

	// DMAをクリア
	dma_stop_and_clear();

	// PIO分周率を再設定
	reset_i2s_freq();

}


void volume_control(void)
{
	if (!audio_state.mute)
	{
		audio_state.vol_float = saturation_f32(powf(10.0f, (float)audio_state.acq_volume / (float)VOLUME_RESOLUTION / 20.0f), 1.0f, 0.0f);
	}
	else
	{
		audio_state.vol_float = 0.;
	}
}
