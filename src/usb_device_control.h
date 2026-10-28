/*
* Copyright (c) 2026 Toshimiyu 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#ifndef _USB_DEVICE_CONTROL_H_
#define _USB_DEVICE_CONTROL_H_

#include <stdint.h>
#include <stdbool.h>

// ---- Bring-up level (no debug equipment needed: flash and observe) -----------------------------
//   0 : USB completely OFF  (original boot flow only: splash -> FFT screen). If this does not boot,
//       the problem is NOT in the USB code.
//   1 : TinyUSB init + tud_task only (the PC should show a UAC2 device; no audio processing)
//   2 : full firmware (default)
#ifndef USB_BRINGUP_LEVEL
#define USB_BRINGUP_LEVEL (2)
#endif

// Streaming state (XMOS-style buffering control)
typedef enum
{
	USB_AUDIO_IDLE = 0,		   // no stream (AS interface alt0 / not configured)
	USB_AUDIO_BUFFERING,	   // stream open, accumulating up to SIZE_BUFFER_FB_THRESHOLD
	USB_AUDIO_STREAMING,	   // normal operation, PI feedback active
	USB_AUDIO_OVERFLOW_FATAL,  // ring buffer too full : new USB data is discarded, playback continues
	USB_AUDIO_UNDERFLOW_FATAL, // ring buffer too empty : accumulate again
} usb_audio_state_t;

// Diagnostic counters (read-only)
typedef struct
{
	volatile usb_audio_state_t state;
	volatile uint32_t overflow_count;
	volatile uint32_t underflow_count;
	volatile uint32_t fifo_resync_count;	// TinyUSB software FIFO had to be flushed
	volatile uint32_t rate_limit_violation; // host selected a bit depth / rate pair that Full-Speed cannot carry
	volatile int32_t level_now;				// audio ring-buffer usage [samples]
	volatile int32_t level_min;				// min / max since STREAMING started
	volatile int32_t level_max;
	volatile float feedback_hz;				// effective sample rate currently requested from the host
	volatile float integ_hz;				// PI integrator [Hz] = learned clock offset (saturated at +-limit -> control failing)
	volatile int32_t win_avg;				// 1 s window statistics of the level (valid while STREAMING):
	volatile int32_t win_min;				//   a steadily rising / falling win_avg = drift = control failure
	volatile int32_t win_max;
	volatile int32_t win_slope;				// win_avg - previous win_avg  [samples per second]				//   win_avg / min / max staying near TARGET = control working
} usb_audio_stats_t;

// ---- Application API (main.c) -----------------------------------------------
// Initialise TinyUSB and publish the UAC2 device (name kept for main.c compatibility)
extern void usb_sound_card_init(void);

// Must be called from the Core0 main loop as often as possible:
// tud_task + PCM receive/convert -> audio ring buffer + feedback control + state machine
extern void usb_audio_task(void);

extern usb_audio_state_t usb_audio_get_state(void);
extern const usb_audio_stats_t *usb_audio_get_stats(void);

// ---- Used by uac2_callbacks.c -----------------------------------------------
#define USB_AUDIO_N_RATES (6)
extern const uint32_t usb_audio_sample_rates[USB_AUDIO_N_RATES];
extern bool usb_audio_rate_supported(uint32_t freq);
extern void usb_audio_apply_sample_rate(uint32_t freq); // reconfigure I2S and clear queued PCM
extern void usb_audio_stream_open(uint8_t alt);			 // AS interface alt 1..3
extern void usb_audio_stream_close(void);				 // AS interface alt 0 / unmount

#endif /* _USB_DEVICE_CONTROL_H_ */
