/*
* Copyright (c) 2026 Toshimiyu 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

// USB Audio Class 2.0 (TinyUSB uac2_speaker_fb based) front-end of the DDC.
//
//   Host -> UAC2 async OUT EP -> TinyUSB sw FIFO -> usb_audio_task()
//        -> PCM convert (16/24/32bit -> float in int32 full-scale)
//        -> buffer_ep_Lch / buffer_ep_Rch   (connection point to the existing DSP, unchanged)
//        -> upsampling_process_core0() -> buffer_upsr_data_Lch/Rch_0 -> DMA -> PIO-I2S
//
// Feedback (explicit, 16.16 samples/ms) is computed from the fill level of buffer_upsr_data_Lch_0
// with a small PI controller (XMOS-style), NOT from the TinyUSB FIFO count.

#include <math.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "tusb.h"
#include "usb_device_control.h"
#include "uac2_compat.h"
#include "usb_descriptors.h"
#include "common_types.h"
#include "common.h"
#include "ringbuffer.h"
#include "upsampling.h"
#include "fft_fir_core0.h"
#include "display_rta.h"

extern uint32_t now_playing;
extern volatile bool is_high_power_mode;

// Referenced from main.c / display_rta.c (activity monitor)
volatile uint32_t audio_last_data_us = 0;
volatile bool audio_data_seen = false;

//--------------------------------------------------------------------+
// Tunables
//--------------------------------------------------------------------+
#define RX_CHUNK_FRAMES (192)
#if (RX_CHUNK_FRAMES > SIZE_EP_BUFFER)
#error RX_CHUNK_FRAMES must not exceed SIZE_EP_BUFFER
#endif

// Ring buffer watermarks (buffer_upsr_data_Lch_0, in samples after Core0 upsampling)
#define FB_TARGET_LEVEL (SIZE_BUFFER_FB_THRESHOLD)
#define LEVEL_LOW_MARK (SIZE_UPSAMPLE_CORE0 / 32)						   // below -> UNDERFLOW_FATAL
#define LEVEL_HIGH_MARK (SIZE_UPSAMPLE_CORE0 - SIZE_UPSAMPLE_CORE0 / 32)   // above -> OVERFLOW_FATAL
#define LEVEL_OVERFLOW_RECOVER (FB_TARGET_LEVEL + SIZE_UPSAMPLE_CORE0 / 8) // OVERFLOW_FATAL -> BUFFERING
#define LEVEL_UNDERFLOW_RECOVER (FB_TARGET_LEVEL / 2)					   // UNDERFLOW_FATAL -> BUFFERING

// PI controller (units: Hz of requested sample rate per input sample of level error)
#define FB_KP (0.50f)			   // proportional [Hz / sample]
#define FB_KI (0.0625f)			   // integral     [Hz / (sample * s)]  -> zeta ~= 1, wn ~= 0.25 rad/s
#define FB_LEVEL_TAU_S (0.064f)	   // level low-pass time constant (removes block-size sawtooth)
#define FB_LIMIT_PPM (3000.0f)	   // hard limit of the correction : +-0.3 % of fs (never exceeds FB_ADJ_LIMIT Hz)
#define FB_UPDATE_PERIOD_US (1000) // one update per USB frame

// ---- Diagnostic build switches (NEVER ship with FIXED_FEEDBACK_TEST = 1) -------------------------
// FIXED_FEEDBACK_TEST = 1 : feedback control OFF. The requested rate is fs + FIXED_FEEDBACK_OFFSET_HZ.
//   OFFSET = 0     -> measure the natural drift of the buffer level (win_slope)
//   OFFSET = +-100 -> if the host follows feedback, win_slope changes by about the offset [samples/s]
//                     (ratio 1); if it does not change, the host ignores the feedback endpoint.
// Normal operation (PI control) is unchanged when the switch is 0.
#ifndef FIXED_FEEDBACK_TEST
#define FIXED_FEEDBACK_TEST (0)
#endif
#ifndef FIXED_FEEDBACK_OFFSET_HZ
#define FIXED_FEEDBACK_OFFSET_HZ (0.0f)
#endif
#if FIXED_FEEDBACK_TEST
#warning "FIXED_FEEDBACK_TEST is enabled: feedback control is OFF (diagnostic build only)"
#endif

// (UART diagnostics removed: the buffer level is shown by debug_gauge.c on the LCD)

//--------------------------------------------------------------------+
// Module state
//--------------------------------------------------------------------+
static usb_audio_stats_t stats;
static volatile uint8_t cur_alt = 0;
static volatile uint8_t bytes_per_sample = 2;
static volatile bool rate_unsupported = false;

typedef struct
{
	float level_f;	 // filtered buffer level
	float integ_hz;	 // integrator [Hz]
	uint32_t last_us;
	uint32_t last_fb;
	bool force_send;
} fb_ctrl_t;
static fb_ctrl_t fb;

const uint32_t usb_audio_sample_rates[USB_AUDIO_N_RATES] = {44100, 48000, 88200, 96000, 176400, 192000};

static uint8_t rx_raw[RX_CHUNK_FRAMES * 8];
static float ep_Lch[RX_CHUNK_FRAMES];
static float ep_Rch[RX_CHUNK_FRAMES];

//--------------------------------------------------------------------+
// Helpers
//--------------------------------------------------------------------+
bool usb_audio_rate_supported(uint32_t freq)
{
	for (uint32_t i = 0; i < USB_AUDIO_N_RATES; i++)
		if (usb_audio_sample_rates[i] == freq)
			return true;
	return false;
}

static uint32_t max_rate_of_alt(uint8_t alt)
{
	switch (alt)
	{
	case UAC2_ALT_24BIT:
		return UAC2_MAX_RATE_24BIT;
	case UAC2_ALT_32BIT:
		return UAC2_MAX_RATE_32BIT;
	case UAC2_ALT_16BIT:
	default:
		return UAC2_MAX_RATE_16BIT;
	}
}

static void set_state(usb_audio_state_t s)
{
	stats.state = s;
}

static void fb_reset(void)
{
	fb.level_f = (float)get_size_using(&buffer_upsr_data_Lch_0);
	fb.integ_hz = 0.0f;
	fb.last_us = time_us_32();
	fb.force_send = true;
}

static int32_t win_prev_avg = 0; // previous 1 s window average (for win_slope)
static bool win_prev_valid = false;

static void level_stats_reset(void)
{
	win_prev_valid = false;
	stats.win_slope = 0;
	stats.level_min = INT32_MAX;
	stats.level_max = 0;
	stats.win_avg = stats.win_min = stats.win_max = 0;
}

static void clear_audio_buffers(void)
{
	clear_ringbuffer(&buffer_ep_Lch);
	clear_ringbuffer(&buffer_ep_Rch);
	clear_ringbuffer(&buffer_upsr_data_Lch_0);
	clear_ringbuffer(&buffer_upsr_data_Rch_0);
}

//--------------------------------------------------------------------+
// Sample rate change : audio_state.freq -> set_sys_clock_khz -> reset_i2s_freq
//                      -> clear buffers -> BUFFERING
//--------------------------------------------------------------------+
void usb_audio_apply_sample_rate(uint32_t new_freq)
{
	if (audio_state.freq == new_freq)
		return;

	// Stop accepting data while the clock tree is being rebuilt
	set_state(USB_AUDIO_IDLE);

	// Core1 consumes the post-upsampling L/R buffers and services DMA. Stop it
	// before changing the shared clock or clearing those buffers so both channel
	// read positions and the DMA queue are reset as one operation.
	multicore_lockout_start_blocking();

	audio_state.freq = new_freq;

	if (USE_ESS_DAC && KIND_ESS_DAC == ES9038Q2M)
		ess_dac_mute();

	// renew_clock(): cancel timer0, set_sys_clock_khz() for the 44.1k / 48k family,
	// restart timer0, dma_stop_and_clear(), reset_i2s_freq()
	renew_clock(is_high_power_mode);

	clear_audio_buffers();
	clear_bq_filter_delay();
	tud_audio_clear_ep_out_ff();

	fb_reset();
	level_stats_reset();
	multicore_lockout_end_blocking();

	// The host may change the rate while a 24/32bit alternate setting is already open:
	// re-evaluate the Full-Speed bandwidth limit and fall back to silence (IDLE) if exceeded.
	rate_unsupported = (cur_alt != 0) && (new_freq > max_rate_of_alt(cur_alt));
	if (rate_unsupported)
		stats.rate_limit_violation++;

	if (cur_alt != 0 && !rate_unsupported)
		set_state(USB_AUDIO_BUFFERING);
	else
		set_state(USB_AUDIO_IDLE);
}

//--------------------------------------------------------------------+
// Stream open / close (AS interface alternate setting)
//--------------------------------------------------------------------+
void usb_audio_stream_open(uint8_t alt)
{
	cur_alt = alt;
	switch (alt)
	{
	case UAC2_ALT_32BIT:
		bytes_per_sample = 4;
		audio_state.bit_depth = 32;
		break;
	case UAC2_ALT_24BIT:
		bytes_per_sample = 3;
		audio_state.bit_depth = 24;
		break;
	case UAC2_ALT_16BIT:
	default:
		bytes_per_sample = 2;
		audio_state.bit_depth = 16;
		break;
	}

	// Full-Speed can not carry 24/32bit above 96kHz : do not pretend to play it
	rate_unsupported = (audio_state.freq > max_rate_of_alt(alt));
	if (rate_unsupported)
		stats.rate_limit_violation++;

	tud_audio_clear_ep_out_ff();
	clear_ringbuffer(&buffer_ep_Lch);
	clear_ringbuffer(&buffer_ep_Rch);
	fb_reset();
	level_stats_reset();
	set_state(rate_unsupported ? USB_AUDIO_IDLE : USB_AUDIO_BUFFERING);
}

void usb_audio_stream_close(void)
{
	cur_alt = 0;
	rate_unsupported = false;
	set_state(USB_AUDIO_IDLE);
	tud_audio_clear_ep_out_ff();
}

//--------------------------------------------------------------------+
// PCM receive : sw FIFO -> float (int32 full-scale) -> gain -> buffer_ep_Lch/Rch
//--------------------------------------------------------------------+
static inline int32_t rd_s16(const uint8_t *p)
{
	return (int32_t)(((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 24));
}

static inline int32_t rd_s24(const uint8_t *p)
{
	return (int32_t)(((uint32_t)p[0] << 8) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 24));
}

static inline int32_t rd_s32(const uint8_t *p)
{
	return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

static void __not_in_flash_func(pcm_to_float)(const uint8_t *src, uint32_t frames, uint8_t bps, float *left, float *right)
{
	switch (bps)
	{
	case 4:
		for (uint32_t i = 0; i < frames; i++, src += 8)
		{
			left[i] = (float)rd_s32(src);
			right[i] = (float)rd_s32(src + 4);
		}
		break;
	case 3:
		for (uint32_t i = 0; i < frames; i++, src += 6)
		{
			left[i] = (float)rd_s24(src);
			right[i] = (float)rd_s24(src + 3);
		}
		break;
	case 2:
	default:
		for (uint32_t i = 0; i < frames; i++, src += 4)
		{
			left[i] = (float)rd_s16(src);
			right[i] = (float)rd_s16(src + 2);
		}
		break;
	}
}

// Total filter gain compensation (kept identical to the previous implementation)
static float calc_fft_gain_ratio(uint32_t freq, uint16_t ratio, uint16_t ratio_core1, bool power_mode)
{
	float gain_core0 = 1.0f;
	if (ratio == 8 && !power_mode)
	{
		const FFT_FIR_PROFILE *profile_stage1 = fft_fir_core0_select_profile(freq, 4, power_mode);
		if (profile_stage1)
			gain_core0 = profile_stage1->gain_ratio;
		else
		{
			const FFT_FIR_PROFILE *profile = fft_fir_core0_select_profile(freq, ratio, power_mode);
			gain_core0 = profile ? profile->gain_ratio : 1.0f;
		}
	}
	else
	{
		const FFT_FIR_PROFILE *profile = fft_fir_core0_select_profile(freq, ratio, power_mode);
		gain_core0 = profile ? profile->gain_ratio : 1.0f;
	}

	float gain_core1 = 1.0f;
	if (ratio_core1 > 1)
	{
		uint32_t freq_in = freq * get_ratio_upsampling_core0(freq);
		const FFT_FIR_PROFILE *profile = fft_fir_core0_select_profile(freq_in, ratio_core1, power_mode);
		gain_core1 = profile ? profile->gain_ratio : 1.0f;
	}
	return gain_core0 * gain_core1;
}

static void report_peak(const float *left, const float *right, uint32_t frames)
{
	float peak_l = 0.0f, peak_r = 0.0f;
	for (uint32_t i = 0; i < frames; i++)
	{
		float abs_l = fabsf(left[i]);
		float abs_r = fabsf(right[i]);
		if (abs_l > peak_l)
			peak_l = abs_l;
		if (abs_r > peak_r)
			peak_r = abs_r;
	}

	float db_l = 20.0f * log10f(fmaxf(peak_l, 1.0f) / 2147483647.0f);
	float db_r = 20.0f * log10f(fmaxf(peak_r, 1.0f) / 2147483647.0f);
	// -80dBFS .. +3dBFS -> 0..100
	float level_l_f = ((db_l + 80.0f) / 83.0f) * 100.0f;
	float level_r_f = ((db_r + 80.0f) / 83.0f) * 100.0f;
	uint8_t level_l = (uint8_t)fminf(fmaxf(level_l_f, 0.0f), 100.0f);
	uint8_t level_r = (uint8_t)fminf(fmaxf(level_r_f, 0.0f), 100.0f);
	display_rta_set_peak(level_l, level_r);
}

static void service_rx(void)
{
	const usb_audio_state_t st = stats.state;

	// No stream, or ring buffer protection active: new USB data is discarded.
	if (st == USB_AUDIO_IDLE || st == USB_AUDIO_OVERFLOW_FATAL)
	{
		if (tud_audio_available())
			tud_audio_clear_ep_out_ff();
		return;
	}

	const uint8_t bps = bytes_per_sample;
	const uint32_t frame_bytes = 2u * bps;

	// Gain (volume * filter compensation), refreshed when the configuration changes
	static uint32_t last_freq = 0;
	static uint16_t last_ratio = 0, last_ratio_core1 = 0;
	static bool last_power = false;
	static float fft_gain_ratio = 1.0f;
	const uint32_t freq = audio_state.freq;
	const uint16_t ratio = get_ratio_upsampling_core0(freq);
	const uint16_t ratio_core1 = get_ratio_upsampling_core1();
	const bool power_mode = is_high_power_mode;
	if (freq != last_freq || ratio != last_ratio || ratio_core1 != last_ratio_core1 || power_mode != last_power)
	{
		fft_gain_ratio = calc_fft_gain_ratio(freq, ratio, ratio_core1, power_mode);
		last_freq = freq;
		last_ratio = ratio;
		last_ratio_core1 = ratio_core1;
		last_power = power_mode;
	}

	// The software FIFO is overwritable : if it ever backs up (main loop stalled while
	// buffer_ep is full) its frame alignment can no longer be trusted -> flush and resync.
	uint16_t avail = tud_audio_available();
	const uint16_t fifo_depth = CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ;
	if (avail > (fifo_depth / 4) * 3)
	{
		tud_audio_clear_ep_out_ff();
		stats.fifo_resync_count++;
		return;
	}

	while (avail >= frame_bytes)
	{
		int64_t space_l = get_size_remain(&buffer_ep_Lch);
		int64_t space_r = get_size_remain(&buffer_ep_Rch);
		int64_t space = (space_l < space_r) ? space_l : space_r;
		if (space <= 0)
			break; // staging buffer full : leave the rest in the FIFO (back-pressure)

		uint32_t frames = avail / frame_bytes;
		if (frames > RX_CHUNK_FRAMES)
			frames = RX_CHUNK_FRAMES;
		if ((int64_t)frames > space)
			frames = (uint32_t)space;

		uint16_t got = tud_audio_read(rx_raw, (uint16_t)(frames * frame_bytes));
		frames = got / frame_bytes;
		if (frames == 0)
			break;

		pcm_to_float(rx_raw, frames, bps, ep_Lch, ep_Rch);

		const float scale = audio_state.vol_float * DEFAULT_GAIN_RATIO * fft_gain_ratio;
		for (uint32_t i = 0; i < frames; i++)
		{
			ep_Lch[i] *= scale;
			ep_Rch[i] *= scale;
		}

		report_peak(ep_Lch, ep_Rch, frames);

		ringbuf_write_array_no_spinlock((int32_t *)ep_Lch, frames, &buffer_ep_Lch);
		ringbuf_write_array_no_spinlock((int32_t *)ep_Rch, frames, &buffer_ep_Rch);

		audio_last_data_us = time_us_32();
		audio_data_seen = true;
		now_playing++;

		avail = tud_audio_available();
	}
}

//--------------------------------------------------------------------+
// State machine (IDLE / BUFFERING / STREAMING / OVERFLOW_FATAL / UNDERFLOW_FATAL)
//--------------------------------------------------------------------+
static void service_state(void)
{
	const int32_t level = (int32_t)get_size_using(&buffer_upsr_data_Lch_0);
	stats.level_now = level;

	switch (stats.state)
	{
	case USB_AUDIO_BUFFERING:
		if (level >= FB_TARGET_LEVEL)
		{
			level_stats_reset();
			set_state(USB_AUDIO_STREAMING);
		}
		break;

	case USB_AUDIO_STREAMING:
		if (level < stats.level_min)
			stats.level_min = level;
		if (level > stats.level_max)
			stats.level_max = level;

		// 1 s window statistics (diagnostics only)
		{
			static uint32_t win_start_us = 0, win_count = 0;
			static int64_t win_sum = 0;
			static int32_t wmin = INT32_MAX, wmax = 0;
			const uint32_t now_us = time_us_32();
			if (win_count == 0)
				win_start_us = now_us;
			win_sum += level;
			win_count++;
			if (level < wmin)
				wmin = level;
			if (level > wmax)
				wmax = level;
			if ((uint32_t)(now_us - win_start_us) >= 1000000u)
			{
				const int32_t avg = (int32_t)(win_sum / (int64_t)win_count);
				stats.win_slope = win_prev_valid ? (avg - win_prev_avg) : 0;
				win_prev_avg = avg;
				win_prev_valid = true;
				stats.win_avg = avg;
				stats.win_min = wmin;
				stats.win_max = wmax;
				win_sum = 0;
				win_count = 0;
				wmin = INT32_MAX;
				wmax = 0;
			}
		}

		if (level >= LEVEL_HIGH_MARK)
		{
			stats.overflow_count++;
			set_state(USB_AUDIO_OVERFLOW_FATAL);
		}
		else if (level <= LEVEL_LOW_MARK)
		{
			stats.underflow_count++;
			set_state(USB_AUDIO_UNDERFLOW_FATAL);
		}
		break;

	case USB_AUDIO_OVERFLOW_FATAL:
		// DMA / PIO keep draining the buffer; USB data is discarded in service_rx()
		if (level <= LEVEL_OVERFLOW_RECOVER)
			set_state(USB_AUDIO_BUFFERING);
		break;

	case USB_AUDIO_UNDERFLOW_FATAL:
		// USB data keeps accumulating
		if (level >= LEVEL_UNDERFLOW_RECOVER)
			set_state(USB_AUDIO_BUFFERING);
		break;

	case USB_AUDIO_IDLE:
	default:
		break;
	}
}

//--------------------------------------------------------------------+
// Feedback (PI control on buffer_upsr_data_Lch_0 fill level)
//--------------------------------------------------------------------+
static void service_feedback(void)
{
	const usb_audio_state_t st = stats.state;
	if (st == USB_AUDIO_IDLE)
		return;

	const uint32_t now = time_us_32();
	const uint32_t dt_us = now - fb.last_us;
	if (!fb.force_send && dt_us < FB_UPDATE_PERIOD_US)
		return;
	fb.last_us = now;

	float dt = (float)dt_us * 1e-6f;
	if (dt > 0.02f)
		dt = 0.02f;

	const uint32_t fs = audio_state.freq;
	const float level = (float)get_size_using(&buffer_upsr_data_Lch_0);
	const float ratio = (float)get_ratio_upsampling_core0(fs);

	// Low-pass the level so that block-wise Core0 / DMA access does not leak into the feedback
	const float alpha = dt / (FB_LEVEL_TAU_S + dt);
	fb.level_f += (level - fb.level_f) * alpha;

	float adj_hz = 0.0f;
#if FIXED_FEEDBACK_TEST
	// Diagnostic: control OFF, constant (usually zero) offset
	fb.integ_hz = 0.0f;
	if (st != USB_AUDIO_BUFFERING)
		adj_hz = FIXED_FEEDBACK_OFFSET_HZ;
	(void)ratio;
#else
	// Keep level control active during BUFFERING as well as STREAMING. During
	// underflow recovery the output may still be consuming data; holding nominal
	// feedback here can keep the level below target forever, so the device never
	// returns to STREAMING. Preserve the integrator across the state transition.
	{
		float lim = (float)fs * FB_LIMIT_PPM * 1e-6f;
		if (lim > (float)FB_ADJ_LIMIT)
			lim = (float)FB_ADJ_LIMIT;

		// error in input-rate samples: positive -> buffer below target -> ask the host for more
		const float err = ((float)FB_TARGET_LEVEL - fb.level_f) / ratio;

		float p = FB_KP * err;
		float integ_next = fb.integ_hz + FB_KI * err * dt;
		if (integ_next > lim)
			integ_next = lim;
		if (integ_next < -lim)
			integ_next = -lim;

		adj_hz = p + integ_next;
		if (adj_hz > lim)
			adj_hz = lim; // saturated : do not wind the integrator up further
		else if (adj_hz < -lim)
			adj_hz = -lim;
		else
			fb.integ_hz = integ_next;
	}
#endif

	const float rate_hz = (float)fs + adj_hz;
	stats.feedback_hz = rate_hz;
	stats.integ_hz = fb.integ_hz;

	// 16.16 samples per 1 ms frame
	const uint32_t value = (uint32_t)((double)rate_hz * 65.536 + 0.5);
	if (fb.force_send || value != fb.last_fb)
	{
		tud_audio_fb_set(value);
		fb.last_fb = value;
		fb.force_send = false;
	}
}

//--------------------------------------------------------------------+
// Public API
//--------------------------------------------------------------------+
void usb_sound_card_init(void)
{
#if USB_BRINGUP_LEVEL == 0
	return;
#endif
	memset(&stats, 0, sizeof(stats));
	level_stats_reset();
	set_state(USB_AUDIO_IDLE);

	audio_state.acq_volume = DEFAULT_VOLUME;
	volume_control();

	tusb_rhport_init_t dev_init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL};
	tusb_init(BOARD_TUD_RHPORT, &dev_init);
}

void usb_audio_task(void)
{
#if USB_BRINGUP_LEVEL == 0
	return;
#endif
	tud_task();
#if USB_BRINGUP_LEVEL == 1
	return;
#endif
	service_rx();
	service_state();
	service_feedback();


}

usb_audio_state_t usb_audio_get_state(void)
{
	return stats.state;
}

const usb_audio_stats_t *usb_audio_get_stats(void)
{
	return &stats;
}
