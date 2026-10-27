/*
 * TinyUSB configuration (based on examples/device/uac2_speaker_fb)
 * UAC2 speaker, asynchronous OUT + explicit feedback, RP2350 Full-Speed only.
 *
 * The MIT License (MIT)
 * Copyright (c) 2020 Ha Thach (tinyusb.org)
 * Copyright (c) 2020 Jerzy Kasenberg
 */



#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "usb_descriptors.h"

//--------------------------------------------------------------------+
// Board Specific Configuration
//--------------------------------------------------------------------+
#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT      0
#endif

// RP2350 USB controller is Full-Speed only
#define BOARD_TUD_MAX_SPEED   OPT_MODE_FULL_SPEED

//--------------------------------------------------------------------
// Common Configuration
//--------------------------------------------------------------------
#define CFG_TUSB_MCU OPT_MCU_RP2040
#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS           OPT_OS_PICO
#endif

// Debug output costs time on the audio stream
#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG        0
#endif

#define CFG_TUD_ENABLED       1
#define CFG_TUD_MAX_SPEED     BOARD_TUD_MAX_SPEED

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN        __attribute__ ((aligned(4)))
#endif

//--------------------------------------------------------------------
// DEVICE CONFIGURATION
//--------------------------------------------------------------------
#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE    64
#endif

//------------- CLASS -------------//
#define CFG_TUD_AUDIO             1
#define CFG_TUD_HID               0
#define CFG_TUD_CDC               0
#define CFG_TUD_MSC               0
#define CFG_TUD_MIDI              0
#define CFG_TUD_VENDOR            0

//--------------------------------------------------------------------
// AUDIO CLASS DRIVER CONFIGURATION
//--------------------------------------------------------------------
#define CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX              2

// Largest sub-slot (32bit); the actual size is taken from the selected alternate setting
#define CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_RX      4
#define CFG_TUD_AUDIO_FUNC_1_RESOLUTION_RX              32

// Largest OUT packet over all alternate settings
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX              UAC2_EP_SZ_MAX

// Software FIFO between the USB ISR and the main loop: 16 ms at the largest packet size.
// It is drained on every main-loop pass; the depth absorbs short scheduling delays.
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ           (16 * UAC2_EP_SZ_MAX)

#define CFG_TUD_AUDIO_ENABLE_EP_OUT                     1

// Required by the TinyUSB bundled with pico-sdk 2.x (ignored by newer TinyUSB).
// Numeric on purpose: this header is parsed before TinyUSB's own descriptor macros exist.
#define CFG_TUD_AUDIO_FUNC_1_DESC_LEN                   UAC2_FUNC_DESC_LEN_NUM
#define CFG_TUD_AUDIO_FUNC_1_N_AS_INT                   1
// UAC2 clock RANGE is 2 bytes + 12 bytes per supported rate. The six
// advertised rates require 74 bytes; the TinyUSB default (64) rejects the
// response copy and can prevent Windows from starting the audio device.
#define CFG_TUD_AUDIO_CTRL_BUF_SZ                       80

// Retain the per-function spelling for compatibility with older TinyUSB trees.
#define CFG_TUD_AUDIO_FUNC_1_CTRL_BUF_SZ                CFG_TUD_AUDIO_CTRL_BUF_SZ

// Explicit feedback EP. The value is set by usb_device_control.c (tud_audio_fb_set),
// the built-in FIFO-count method is NOT used.
#define CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP                1

#ifdef __cplusplus
}
#endif

#endif /* TUSB_CONFIG_H_ */
