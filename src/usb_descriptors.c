/*
 * USB descriptors : UAC2 speaker (async + explicit feedback), Full-Speed
 * Based on TinyUSB examples/device/uac2_speaker_fb (HiFiPhile)
 *
 * The MIT License (MIT)
 * Copyright (c) 2023 HiFiPhile
 */

#include "tusb.h"
#include "uac2_compat.h"
#include "pico/unique_id.h"
#include "usb_descriptors.h"
#include "common_types.h"
#include "common.h" // MFG_NAME / DEVICE_NAME

// Same VID as the previous firmware. The PID is intentionally NEW: the device class changed
// from UAC1 (LUFA style) to UAC2, so Windows must not reuse the cached UAC1 driver binding.
#define USB_VID           0x2e8a
#define USB_PID           0xfede

//--------------------------------------------------------------------+
// Device Descriptors
//--------------------------------------------------------------------+
static tusb_desc_device_t const desc_device =
{
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,

    // Use Interface Association Descriptor (IAD) for Audio
    // As required by USB Specs IAD's subclass must be common class (2) and protocol must be IAD (1)
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0300,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01
};

// Invoked when received GET DEVICE DESCRIPTOR
uint8_t const * tud_descriptor_device_cb(void) {
  return (uint8_t const *) &desc_device;
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+
#define CONFIG_UAC2_TOTAL_LEN    (TUD_CONFIG_DESC_LEN + TUD_AUDIO20_SPEAKER_STEREO_FB_MULTIALT_DESC_LEN)

uint8_t const desc_uac2_configuration[] = {
  // Config number, interface count, string index, total length, attribute, power in mA
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_UAC2_TOTAL_LEN, 0x00, 100),

  // First interface number, string index, EP Out, EP feedback, feedback EP size
  TUD_AUDIO20_SPEAKER_STEREO_FB_MULTIALT_DESCRIPTOR(ITF_NUM_AUDIO_CONTROL, 0, UAC2_EPNUM_AUDIO_OUT, UAC2_EPNUM_AUDIO_FB | 0x80, UAC2_FB_EP_SIZE),
};

TU_VERIFY_STATIC(sizeof(desc_uac2_configuration) == CONFIG_UAC2_TOTAL_LEN, "Incorrect size");
TU_VERIFY_STATIC(sizeof(desc_uac2_configuration) == TUD_CONFIG_DESC_LEN + UAC2_FUNC_DESC_LEN_NUM, "UAC2_FUNC_DESC_LEN_NUM mismatch");
TU_VERIFY_STATIC(UAC2_EP_SZ_MAX <= 1023, "Full-Speed isochronous packet size limit exceeded");

// Invoked when received GET CONFIGURATION DESCRIPTOR
uint8_t const * tud_descriptor_configuration_cb(uint8_t index) {
  (void) index; // for multiple configurations
  return desc_uac2_configuration;
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+
enum {
  STRID_LANGID = 0,
  STRID_MANUFACTURER,
  STRID_PRODUCT,
  STRID_SERIAL,
  STRID_AUDIO_INTERFACE,
};

static char const *string_desc_arr[] =
{
  (const char[]) { 0x09, 0x04 },  // 0: is supported language is English (0x0409)
  MFG_NAME,                       // 1: Manufacturer
  DEVICE_NAME,                    // 2: Product
  NULL,                           // 3: Serial = RP2350 unique board ID
  DEVICE_NAME,                    // 4: Audio Interface
};

static uint16_t _desc_str[32 + 1];

// Invoked when received GET STRING DESCRIPTOR request
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void) langid;
  size_t chr_count;

  switch ( index ) {
    case STRID_LANGID:
      memcpy(&_desc_str[1], string_desc_arr[0], 2);
      chr_count = 1;
      break;

    case STRID_SERIAL: {
      char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
      pico_get_unique_board_id_string(serial, sizeof(serial));
      chr_count = strlen(serial);
      for ( size_t i = 0; i < chr_count; i++ ) {
        _desc_str[1 + i] = serial[i];
      }
      break;
    }

    default: {
      if ( !(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) ) return NULL;

      const char *str = string_desc_arr[index];

      // Cap at max char
      chr_count = strlen(str);
      size_t const max_count = sizeof(_desc_str) / sizeof(_desc_str[0]) - 1; // -1 for string type
      if ( chr_count > max_count ) chr_count = max_count;

      // Convert ASCII string into UTF-16
      for ( size_t i = 0; i < chr_count; i++ ) {
        _desc_str[1 + i] = str[i];
      }
      break;
    }
  }

  // first byte is length (including header), second byte is string type
  _desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));

  return _desc_str;
}
