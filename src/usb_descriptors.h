/*
 * USB Audio Class 2.0 descriptor templates
 * Based on TinyUSB examples/device/uac2_speaker_fb (HiFiPhile), extended for
 * RP2350 (Full-Speed only): three AudioStreaming alternate settings (16/24/32bit).
 *
 * The MIT License (MIT)
 * Copyright (c) 2023 HiFiPhile
 */

#ifndef USB_DESCRIPTORS_H_
#define USB_DESCRIPTORS_H_

//--------------------------------------------------------------------+
// UAC2 entity IDs (same as the TinyUSB example)
//--------------------------------------------------------------------+
#define UAC2_ENTITY_CLOCK               0x04
#define UAC2_ENTITY_INPUT_TERMINAL      0x01
#define UAC2_ENTITY_FEATURE_UNIT        0x02
#define UAC2_ENTITY_OUTPUT_TERMINAL     0x03

// AudioStreaming alternate settings
#define UAC2_ALT_16BIT   1
#define UAC2_ALT_24BIT   2
#define UAC2_ALT_32BIT   3

// Endpoints (RP2350: EP1 OUT and EP1 IN may coexist)
#define UAC2_EPNUM_AUDIO_OUT   0x01
#define UAC2_EPNUM_AUDIO_FB    0x01

//--------------------------------------------------------------------+
// Endpoint sizes. Full-Speed isochronous wMaxPacketSize <= 1023 bytes.
//   16bit @192kHz : (192+1)*2*2 = 772
//   24bit @ 96kHz : ( 96+1)*3*2 = 582
//   32bit @ 96kHz : ( 96+1)*4*2 = 776
// 24/32bit at 176.4/192kHz would need 1058..1544 bytes/ms : impossible on Full-Speed.
//--------------------------------------------------------------------+
#define UAC2_FS_EP_SIZE(_fs, _bytes)   ((((_fs) / 1000) + 1) * (_bytes) * 2)
#define UAC2_EP_SZ_16BIT   UAC2_FS_EP_SIZE(192000, 2)
#define UAC2_EP_SZ_24BIT   UAC2_FS_EP_SIZE(96000, 3)
#define UAC2_EP_SZ_32BIT   UAC2_FS_EP_SIZE(96000, 4)
#define UAC2_EP_SZ_MAX     (UAC2_EP_SZ_32BIT > UAC2_EP_SZ_16BIT ? UAC2_EP_SZ_32BIT : UAC2_EP_SZ_16BIT)

#define UAC2_MAX_RATE_16BIT   192000
#define UAC2_MAX_RATE_24BIT   96000
#define UAC2_MAX_RATE_32BIT   96000

// Explicit feedback EP payload : 4 bytes, 16.16 samples/ms (UAC2)
#define UAC2_FB_EP_SIZE   4

//--------------------------------------------------------------------+
// Descriptor length
// UAC2_FUNC_DESC_LEN_NUM is the same value as TUD_AUDIO20_SPEAKER_STEREO_FB_MULTIALT_DESC_LEN written as a
// plain number (IAD 8, std AC 9, CS AC 9, clock 8, input 17, output 12, FU 18, AS alt0 9, 3 x 53 per alt).
// usb_descriptors.c verifies at compile time that both agree with the real descriptor.
//--------------------------------------------------------------------+
#define UAC2_FUNC_DESC_LEN_NUM (8 + 9 + 9 + (8 + 17 + 12 + 18) + 9 + 3 * (9 + 16 + 6 + 7 + 8 + 7))
#define UAC2_AS_ALT_LEN (TUD_AUDIO20_DESC_STD_AS_LEN\
  + TUD_AUDIO20_DESC_CS_AS_INT_LEN\
  + TUD_AUDIO20_DESC_TYPE_I_FORMAT_LEN\
  + TUD_AUDIO20_DESC_STD_AS_ISO_EP_LEN\
  + TUD_AUDIO20_DESC_CS_AS_ISO_EP_LEN\
  + TUD_AUDIO20_DESC_STD_AS_ISO_FB_EP_LEN)

#define UAC2_AC_BODY_LEN (TUD_AUDIO20_DESC_CLK_SRC_LEN\
  + TUD_AUDIO20_DESC_INPUT_TERM_LEN\
  + TUD_AUDIO20_DESC_OUTPUT_TERM_LEN\
  + TUD_AUDIO20_DESC_FEATURE_UNIT_LEN(2))

#define TUD_AUDIO20_SPEAKER_STEREO_FB_MULTIALT_DESC_LEN (TUD_AUDIO20_DESC_IAD_LEN\
  + TUD_AUDIO20_DESC_STD_AC_LEN\
  + TUD_AUDIO20_DESC_CS_AC_LEN\
  + UAC2_AC_BODY_LEN\
  + TUD_AUDIO20_DESC_STD_AS_LEN /* alt0 */\
  + 3 * UAC2_AS_ALT_LEN)

//--------------------------------------------------------------------+
// One AudioStreaming alternate setting (OUT iso EP, async + explicit feedback EP)
//--------------------------------------------------------------------+
#define UAC2_AS_ALT_DESCRIPTOR(_itfas, _alt, _subslot, _bits, _epout, _epoutsize, _epfb, _epfbsize) \
  /* Standard AS Interface Descriptor(4.9.1) : alternate _alt, 2 endpoints */\
  TUD_AUDIO20_DESC_STD_AS_INT(/*_itfnum*/ _itfas, /*_altset*/ _alt, /*_nEPs*/ 0x02, /*_stridx*/ 0x00),\
  /* Class-Specific AS Interface Descriptor(4.9.2) */\
  TUD_AUDIO20_DESC_CS_AS_INT(/*_termid*/ UAC2_ENTITY_INPUT_TERMINAL, /*_ctrl*/ AUDIO20_CTRL_NONE, /*_formattype*/ AUDIO20_FORMAT_TYPE_I, /*_formats*/ AUDIO20_DATA_FORMAT_TYPE_I_PCM, /*_nchannelsphysical*/ 0x02, /*_channelcfg*/ AUDIO20_CHANNEL_CONFIG_NON_PREDEFINED, /*_stridx*/ 0x00),\
  /* Type I Format Type Descriptor(2.3.1.6) */\
  TUD_AUDIO20_DESC_TYPE_I_FORMAT(_subslot, _bits),\
  /* Standard AS Isochronous Audio Data Endpoint Descriptor(4.10.1.1) */\
  TUD_AUDIO20_DESC_STD_AS_ISO_EP(/*_ep*/ _epout, /*_attr*/ (uint8_t) ((uint8_t)TUSB_XFER_ISOCHRONOUS | (uint8_t)TUSB_ISO_EP_ATT_ASYNCHRONOUS | (uint8_t)TUSB_ISO_EP_ATT_DATA), /*_maxEPsize*/ _epoutsize, /*_interval*/ 0x01),\
  /* Class-Specific AS Isochronous Audio Data Endpoint Descriptor(4.10.1.2) */\
  TUD_AUDIO20_DESC_CS_AS_ISO_EP(/*_attr*/ AUDIO20_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK, /*_ctrl*/ AUDIO20_CTRL_NONE, /*_lockdelayunit*/ AUDIO20_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_MILLISEC, /*_lockdelay*/ 0x0001),\
  /* Standard AS Isochronous Feedback Endpoint Descriptor(4.10.2.1) : Full-Speed, 1 ms */\
  TUD_AUDIO20_DESC_STD_AS_ISO_FB_EP(/*_ep*/ _epfb, /*_epsize*/ _epfbsize, /*_interval*/ 1)

//--------------------------------------------------------------------+
// Complete function descriptor (IAD + AC + 3 AS alternates)
//--------------------------------------------------------------------+
#define TUD_AUDIO20_SPEAKER_STEREO_FB_MULTIALT_DESCRIPTOR(_itfnum, _stridx, _epout, _epfb, _epfbsize) \
  /* Standard Interface Association Descriptor (IAD) */\
  TUD_AUDIO20_DESC_IAD(/*_firstitf*/ _itfnum, /*_nitfs*/ 0x02, /*_stridx*/ 0x00),\
  /* Standard AC Interface Descriptor(4.7.1) */\
  TUD_AUDIO20_DESC_STD_AC(/*_itfnum*/ _itfnum, /*_nEPs*/ 0x00, /*_stridx*/ _stridx),\
  /* Class-Specific AC Interface Header Descriptor(4.7.2) */\
  TUD_AUDIO20_DESC_CS_AC(/*_bcdADC*/ 0x0200, /*_category*/ AUDIO20_FUNC_DESKTOP_SPEAKER, /*_totallen*/ UAC2_AC_BODY_LEN, /*_ctrl*/ AUDIO20_CS_AS_INTERFACE_CTRL_LATENCY_POS),\
  /* Clock Source Descriptor(4.7.2.1) : sampling frequency read/write */\
  TUD_AUDIO20_DESC_CLK_SRC(/*_clkid*/ UAC2_ENTITY_CLOCK, /*_attr*/ AUDIO20_CLOCK_SOURCE_ATT_INT_PRO_CLK, /*_ctrl*/ (AUDIO20_CTRL_RW << AUDIO20_CLOCK_SOURCE_CTRL_CLK_FRQ_POS), /*_assocTerm*/ UAC2_ENTITY_INPUT_TERMINAL, /*_stridx*/ 0x00),\
  /* Input Terminal Descriptor(4.7.2.4) */\
  TUD_AUDIO20_DESC_INPUT_TERM(/*_termid*/ UAC2_ENTITY_INPUT_TERMINAL, /*_termtype*/ AUDIO_TERM_TYPE_USB_STREAMING, /*_assocTerm*/ 0x00, /*_clkid*/ UAC2_ENTITY_CLOCK, /*_nchannelslogical*/ 0x02, /*_channelcfg*/ AUDIO20_CHANNEL_CONFIG_NON_PREDEFINED, /*_idxchannelnames*/ 0x00, /*_ctrl*/ 0 * (AUDIO20_CTRL_R << AUDIO20_IN_TERM_CTRL_CONNECTOR_POS), /*_stridx*/ 0x00),\
  /* Output Terminal Descriptor(4.7.2.5) */\
  TUD_AUDIO20_DESC_OUTPUT_TERM(/*_termid*/ UAC2_ENTITY_OUTPUT_TERMINAL, /*_termtype*/ AUDIO_TERM_TYPE_OUT_DESKTOP_SPEAKER, /*_assocTerm*/ UAC2_ENTITY_INPUT_TERMINAL, /*_srcid*/ UAC2_ENTITY_FEATURE_UNIT, /*_clkid*/ UAC2_ENTITY_CLOCK, /*_ctrl*/ 0x0000, /*_stridx*/ 0x00),\
  /* Feature Unit Descriptor(4.7.2.8) : master mute/volume only (single hardware volume) */\
  TUD_AUDIO20_DESC_FEATURE_UNIT(/*_unitid*/ UAC2_ENTITY_FEATURE_UNIT, /*_srcid*/ UAC2_ENTITY_INPUT_TERMINAL, /*_stridx*/ 0x00, /*_ctrlch0master*/ AUDIO20_CTRL_RW << AUDIO20_FEATURE_UNIT_CTRL_MUTE_POS | AUDIO20_CTRL_RW << AUDIO20_FEATURE_UNIT_CTRL_VOLUME_POS, /*_ctrlch1*/ 0, /*_ctrlch2*/ 0),\
  /* Standard AS Interface Descriptor(4.9.1) : alternate 0, zero bandwidth */\
  TUD_AUDIO20_DESC_STD_AS_INT(/*_itfnum*/ (uint8_t)((_itfnum) + 1), /*_altset*/ 0x00, /*_nEPs*/ 0x00, /*_stridx*/ 0x00),\
  /* alternate 1 : 16bit / alternate 2 : 24bit / alternate 3 : 32bit */\
  UAC2_AS_ALT_DESCRIPTOR((uint8_t)((_itfnum) + 1), UAC2_ALT_16BIT, 2, 16, _epout, UAC2_EP_SZ_16BIT, _epfb, _epfbsize),\
  UAC2_AS_ALT_DESCRIPTOR((uint8_t)((_itfnum) + 1), UAC2_ALT_24BIT, 3, 24, _epout, UAC2_EP_SZ_24BIT, _epfb, _epfbsize),\
  UAC2_AS_ALT_DESCRIPTOR((uint8_t)((_itfnum) + 1), UAC2_ALT_32BIT, 4, 32, _epout, UAC2_EP_SZ_32BIT, _epfb, _epfbsize)

#endif
