/*
 * TinyUSB UAC2 class callbacks (control requests, alternate-setting changes, feedback setup).
 * Control-request handling follows examples/device/uac2_speaker_fb; the DDC-specific work
 * (clock tree, buffers, volume) is delegated to usb_device_control.c / common.h.
 *
 * Audio data is NOT received here: it is pulled from the TinyUSB software FIFO by
 * usb_audio_task() in the Core0 main loop (see usb_device_control.c).
 */

#include "tusb.h"
#include "uac2_compat.h"
#include "usb_descriptors.h"
#include "common_types.h"
#include "common.h"
#include "usb_device_control.h"

//--------------------------------------------------------------------+
// Clock source (sample rate)
//--------------------------------------------------------------------+
static bool clock_get_request(uint8_t rhport, tusb_control_request_t const *request)
{
  uint8_t const ctrl_sel = TU_U16_HIGH(request->wValue);

  if (ctrl_sel == AUDIO20_CS_CTRL_SAM_FREQ) {
    if (request->bRequest == AUDIO20_CS_REQ_CUR) {
      audio20_control_cur_4_t curf = { (int32_t) tu_htole32(audio_state.freq) };
      return tud_audio_buffer_and_schedule_control_xfer(rhport, request, &curf, sizeof(curf));
    } else if (request->bRequest == AUDIO20_CS_REQ_RANGE) {
      audio20_control_range_4_n_t(USB_AUDIO_N_RATES) rangef = {
        .wNumSubRanges = tu_htole16(USB_AUDIO_N_RATES)
      };
      for (uint8_t i = 0; i < USB_AUDIO_N_RATES; i++) {
        rangef.subrange[i].bMin = (int32_t) usb_audio_sample_rates[i];
        rangef.subrange[i].bMax = (int32_t) usb_audio_sample_rates[i];
        rangef.subrange[i].bRes = 0;
      }
      return tud_audio_buffer_and_schedule_control_xfer(rhport, request, &rangef, sizeof(rangef));
    } else if (request->bRequest == 4) { // UAC2 CS_REQ_INFO = 4
      uint8_t info = 3; // GET and SET supported
      return tud_audio_buffer_and_schedule_control_xfer(rhport, request, &info, sizeof(info));
    }
  } else if (ctrl_sel == AUDIO20_CS_CTRL_CLK_VALID) {
    if (request->bRequest == AUDIO20_CS_REQ_CUR) {
      audio20_control_cur_1_t cur_valid = { .bCur = 1 };
      return tud_audio_buffer_and_schedule_control_xfer(rhport, request, &cur_valid, sizeof(cur_valid));
    } else if (request->bRequest == 4) {
      uint8_t info = 1; // Read-only (GET supported)
      return tud_audio_buffer_and_schedule_control_xfer(rhport, request, &info, sizeof(info));
    }
  }
  return false;
}

static bool clock_set_request(tusb_control_request_t const *request, uint8_t const *buf)
{
  uint8_t const ctrl_sel = TU_U16_HIGH(request->wValue);

  TU_VERIFY(request->bRequest == AUDIO20_CS_REQ_CUR);

  if (ctrl_sel == AUDIO20_CS_CTRL_SAM_FREQ) {
    TU_VERIFY(request->wLength == sizeof(audio20_control_cur_4_t));
    uint32_t const new_freq = (uint32_t) ((audio20_control_cur_4_t const *) buf)->bCur;
    TU_VERIFY(usb_audio_rate_supported(new_freq));

    usb_audio_apply_sample_rate(new_freq);
    return true;
  }
  return false;
}

//--------------------------------------------------------------------+
// Feature unit (master mute / volume only)
//--------------------------------------------------------------------+
static bool feature_unit_get_request(uint8_t rhport, tusb_control_request_t const *request)
{
  uint8_t const ctrl_sel    = TU_U16_HIGH(request->wValue);
  uint8_t const channel_num = TU_U16_LOW(request->wValue);

  // Only the master channel (0) is exposed in the descriptor
  TU_VERIFY(channel_num == 0);

  if (ctrl_sel == AUDIO20_FU_CTRL_MUTE) {
    if (request->bRequest == AUDIO20_CS_REQ_CUR) {
      audio20_control_cur_1_t mute1 = { .bCur = audio_state.mute ? 1 : 0 };
      return tud_audio_buffer_and_schedule_control_xfer(rhport, request, &mute1, sizeof(mute1));
    } else if (request->bRequest == 4) {
      uint8_t info = 3; // GET/SET supported
      return tud_audio_buffer_and_schedule_control_xfer(rhport, request, &info, sizeof(info));
    }
  } else if (ctrl_sel == AUDIO20_FU_CTRL_VOLUME) {
    if (request->bRequest == AUDIO20_CS_REQ_RANGE) {
      audio20_control_range_2_n_t(1) range_vol = {
        .wNumSubRanges = tu_htole16(1),
        .subrange[0] = { .bMin = tu_htole16(MIN_VOLUME), tu_htole16(MAX_VOLUME), tu_htole16(VOLUME_RESOLUTION) }
      };
      return tud_audio_buffer_and_schedule_control_xfer(rhport, request, &range_vol, sizeof(range_vol));
    } else if (request->bRequest == AUDIO20_CS_REQ_CUR) {
      audio20_control_cur_2_t cur_vol = { .bCur = tu_htole16(audio_state.acq_volume) };
      return tud_audio_buffer_and_schedule_control_xfer(rhport, request, &cur_vol, sizeof(cur_vol));
    } else if (request->bRequest == 4) {
      uint8_t info = 3; // GET/SET supported
      return tud_audio_buffer_and_schedule_control_xfer(rhport, request, &info, sizeof(info));
    }
  }
  return false;
}

static bool feature_unit_set_request(tusb_control_request_t const *request, uint8_t const *buf)
{
  uint8_t const ctrl_sel    = TU_U16_HIGH(request->wValue);
  uint8_t const channel_num = TU_U16_LOW(request->wValue);

  TU_VERIFY(channel_num == 0);
  TU_VERIFY(request->bRequest == AUDIO20_CS_REQ_CUR);

  if (ctrl_sel == AUDIO20_FU_CTRL_MUTE) {
    TU_VERIFY(request->wLength == sizeof(audio20_control_cur_1_t));
    audio_state.mute = (((audio20_control_cur_1_t const *) buf)->bCur != 0);
    volume_control();
    return true;
  } else if (ctrl_sel == AUDIO20_FU_CTRL_VOLUME) {
    TU_VERIFY(request->wLength == sizeof(audio20_control_cur_2_t));
    int16_t vol = (int16_t) ((audio20_control_cur_2_t const *) buf)->bCur;
    if (vol > MAX_VOLUME) vol = MAX_VOLUME;
    if (vol < MIN_VOLUME) vol = MIN_VOLUME;
    audio_state.acq_volume = vol;
    volume_control();
    return true;
  }
  return false;
}

//--------------------------------------------------------------------+
// TinyUSB audio class callbacks
//--------------------------------------------------------------------+

bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
  uint8_t const entity_id = TU_U16_HIGH(p_request->wIndex);

  if (entity_id == UAC2_ENTITY_CLOCK)
    return clock_get_request(rhport, p_request);
  if (entity_id == UAC2_ENTITY_FEATURE_UNIT)
    return feature_unit_get_request(rhport, p_request);
  return false;
}

bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *buf)
{
  (void) rhport;
  uint8_t const entity_id = TU_U16_HIGH(p_request->wIndex);

  if (entity_id == UAC2_ENTITY_FEATURE_UNIT)
    return feature_unit_set_request(p_request, buf);
  if (entity_id == UAC2_ENTITY_CLOCK)
    return clock_set_request(p_request, buf);
  return false;
}

bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
  (void) rhport;
  uint8_t const itf = tu_u16_low(p_request->wIndex);
  uint8_t const alt = tu_u16_low(p_request->wValue);

  if (itf != ITF_NUM_AUDIO_STREAMING)
    return true;

  if (alt == 0)
    usb_audio_stream_close();
  else
    usb_audio_stream_open(alt);
  return true;
}

bool tud_audio_set_itf_close_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
  (void) rhport;
  uint8_t const itf = tu_u16_low(p_request->wIndex);
  uint8_t const alt = tu_u16_low(p_request->wValue);

  if (itf == ITF_NUM_AUDIO_STREAMING && alt == 0)
    usb_audio_stream_close();
  return true;
}

void tud_audio_feedback_params_cb(uint8_t func_id, uint8_t alt_itf, audio_feedback_params_t *feedback_param)
{
  (void) func_id;
  (void) alt_itf;
  feedback_param->method = AUDIO_FEEDBACK_METHOD_DISABLED;
  feedback_param->sample_freq = audio_state.freq;
}

void tud_umount_cb(void)
{
  usb_audio_stream_close();
}