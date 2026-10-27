/*
* Copyright (c) 2026 Toshimiyu 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#include "spectrum.h"
#include "arm_math.h"
#include <math.h>
#include <string.h>

#define FFT_LEN 1024
#define FFT_BINS (FFT_LEN / 2 + 1)
#define LOW_FFT_LEN 8192
#define LOW_BAND_COUNT 3
#define LOW_GOERTZEL_BIN_MAX 16
#define CAPTURE_BUFFERS 2
#define DB_FLOOR (-80.0f)
#define DB_LUT_MIN_EXP (-27)
#define DB_LUT_EXP_COUNT (1 - DB_LUT_MIN_EXP)
#define DB_LUT_MANTISSA_COUNT 256

// バンドのレベル算出方法（起動時の係数計算に反映される）
//   1: バンド内のビンのパワーを平均する（ノイズ・音楽信号が周波数に対して平らに見える）
//   0: バンド内のビンのパワーを合計する（正弦波・矩形波テスト信号がバンド幅によらず正しく表示される）
#define SPECTRUM_BAND_AVERAGE 0

typedef enum { BUFFER_FREE, BUFFER_FILLING, BUFFER_READY, BUFFER_PROCESSING } buffer_state_t;
static float capture[CAPTURE_BUFFERS][2][FFT_LEN];
static volatile buffer_state_t state[CAPTURE_BUFFERS] = {BUFFER_FILLING, BUFFER_FREE};
static volatile int8_t ready_index = -1;
static uint8_t fill_index = 0;
static uint16_t capture_pos = 0;
static float window[FFT_LEN];
static float fft_input[FFT_LEN];
static float fft_output[FFT_LEN];

#define SUPPORTED_RATE_COUNT 6
static const uint32_t supported_rates[SUPPORTED_RATE_COUNT] = {
44100u, 48000u, 88200u, 96000u, 176400u, 192000u
};

// At 176.4/192 kHz, a 1024-point FFT cannot resolve the first few bands.
// Track the first three bands by summing their Goertzel-bin powers over a longer window.
static float low_goertzel_coeff[LOW_GOERTZEL_BIN_MAX];
static float low_goertzel_gain[LOW_GOERTZEL_BIN_MAX];
static uint8_t low_goertzel_band[LOW_GOERTZEL_BIN_MAX];
static float low_goertzel_q1[2][LOW_GOERTZEL_BIN_MAX];
static float low_goertzel_q2[2][LOW_GOERTZEL_BIN_MAX];
static volatile uint8_t low_goertzel_levels[2][LOW_BAND_COUNT];
static float low_goertzel_window[LOW_FFT_LEN];
static uint16_t low_goertzel_pos;
static uint16_t low_goertzel_window_len;
static uint8_t low_goertzel_window_stride;
static uint8_t low_goertzel_bin_count;
static bool low_goertzel_enabled;

// ---------------------------------------------------------------------------
// 起動時に計算するテーブル（サンプルレートごと）
//   bin_gain[r][bin] : FFT振幅正規化 × Aウェイト(パワー) × (平均時は 1/バンド内ビン数)
//   bin_band[r][bin] : そのビンが属するバンド番号（範囲外は BAND_NONE）
//   first_bin/last_bin: 集計対象ビンの範囲 [first, last)
//   fb_bin/fb_gain   : ビンが1つも入らないバンドの代用ビンとその係数（fb_bin==0 なら不要）
// FFT実行時は「|X|^2 × bin_gain をバンドへ加算する」だけでレベルのパワーが得られる。
// ---------------------------------------------------------------------------
#define BAND_NONE 0xFFu
static float bin_gain[SUPPORTED_RATE_COUNT][FFT_BINS];
static uint8_t bin_band[SUPPORTED_RATE_COUNT][FFT_BINS];
static uint16_t first_bin[SUPPORTED_RATE_COUNT];
static uint16_t last_bin[SUPPORTED_RATE_COUNT];
static uint16_t fb_bin[SUPPORTED_RATE_COUNT][SPECTRUM_BAND_COUNT];
static float fb_gain[SUPPORTED_RATE_COUNT][SPECTRUM_BAND_COUNT];

static uint8_t power_to_level_lut[DB_LUT_EXP_COUNT][DB_LUT_MANTISSA_COUNT];
static float hann_sum;
static arm_rfft_fast_instance_f32 fft;
static bool initialized;
static bool discard_next_frame;
static uint8_t active_rate_index = 0;

// 各バンドの中心が 63, 125, 250, 500, 1k, 2k, 4k, 8k, 12k, 16k, 20k になるように、
// 隣り合う中心の幾何平均で境界を決めた値 (ISO 1/1オクターブ準拠 11バンド)。
static const float band_edges[SPECTRUM_BAND_COUNT + 1] =
{
45.0f,    // Band 0  ( 63Hz) 下限
88.0f,    // Band 1  (125Hz) 下限 / Band 0 上限
177.0f,   // Band 2  (250Hz) 下限 / Band 1 上限
354.0f,   // Band 3  (500Hz) 下限 / Band 2 上限
707.0f,   // Band 4  (  1kHz) 下限 / Band 3 上限
1414.0f,  // Band 5  (  2kHz) 下限 / Band 4 上限
2828.0f,  // Band 6  (  4kHz) 下限 / Band 5 上限
5657.0f,  // Band 7  (  8kHz) 下限 / Band 6 上限
9800.0f,  // Band 8  ( 12kHz) 下限 / Band 7 上限
13900.0f, // Band 9  ( 16kHz) 下限 / Band 8 上限
17900.0f, // Band 10 ( 20kHz) 下限 / Band 9 上限
22000.0f  // Band 10 ( 20kHz) 上限
};

// IEC 61672 A特性の振幅ゲイン（1kHzで約1.0）
static float a_weight_gain(float f)
{
const float f2 = f * f;
const float numerator = 12194.0f * 12194.0f * f2 * f2;
const float denominator = (f2 + 20.6f * 20.6f) *
sqrtf((f2 + 107.7f * 107.7f) * (f2 + 737.9f * 737.9f)) *
(f2 + 12194.0f * 12194.0f);
if (denominator <= 0.0f) return 0.0f;
return powf(10.0f, (2.0f + 20.0f * log10f(numerator / denominator)) / 20.0f);
}

static uint8_t power_to_level(float power)
{
if (!(power > 0.0f)) return 0;
union { float f; uint32_t u; } bits = { .f = power };
const int exponent = (int)((bits.u >> 23) & 0xffu) - 127;
if (exponent < DB_LUT_MIN_EXP) return 0;
if (exponent > 0) return 100;
const uint8_t mantissa = (uint8_t)((bits.u >> 15) & 0xffu);
return power_to_level_lut[exponent - DB_LUT_MIN_EXP][mantissa];
}

// 1つのサンプルレートについて、バンド割当・Aウェイト・平均係数を計算する
static void build_rate_tables(uint8_t rate, float normalization)
{
const float bin_hz = (float)supported_rates[rate] / (float)FFT_LEN;
uint16_t counts[SPECTRUM_BAND_COUNT] = {0};

first_bin[rate] = FFT_BINS;
last_bin[rate] = FFT_BINS;

// 1) ビンをバンドへ割り当てる
for (uint16_t bin = 0; bin < FFT_BINS; ++bin) {
    bin_band[rate][bin] = BAND_NONE;
    bin_gain[rate][bin] = 0.0f;
    if (bin == 0) continue;

    const float frequency = (float)bin * bin_hz;
    if (frequency < band_edges[0]) continue;
    if (frequency >= band_edges[SPECTRUM_BAND_COUNT]) {
        if (last_bin[rate] == FFT_BINS) last_bin[rate] = bin;
        break;
    }
    if (first_bin[rate] == FFT_BINS) first_bin[rate] = bin;

    uint8_t band = 0;
    while (band + 1 < SPECTRUM_BAND_COUNT && frequency >= band_edges[band + 1]) ++band;
    bin_band[rate][bin] = band;
    counts[band]++;
}

// 2) ビンごとの係数 = 正規化^2 × Aウェイト^2 (× 平均時は 1/ビン数)
for (uint16_t bin = first_bin[rate]; bin < last_bin[rate]; ++bin) {
    const float a = a_weight_gain((float)bin * bin_hz);
    float gain = normalization * normalization * a * a;
#if SPECTRUM_BAND_AVERAGE
gain /= (float)counts[bin_band[rate][bin]];
#endif
bin_gain[rate][bin] = gain;
}

// 3) ビンが1つも入らないバンドは、中心に最も近いビンで代用する
for (uint8_t band = 0; band < SPECTRUM_BAND_COUNT; ++band) {
    fb_bin[rate][band] = 0;
    fb_gain[rate][band] = 0.0f;
    if (counts[band] != 0) continue;

    const float center = sqrtf(band_edges[band] * band_edges[band + 1]);
    uint16_t bin = (uint16_t)(center / bin_hz + 0.5f);
    if (bin < 1) bin = 1;
    if (bin >= FFT_BINS) bin = FFT_BINS - 1;
    /* 修正 */
    const float a = a_weight_gain(center);
    fb_bin[rate][band] = bin;
    fb_gain[rate][band] = normalization * normalization * a * a;
}
}

void spectrum_init(void)
{
if (arm_rfft_fast_init_f32(&fft, FFT_LEN) != ARM_MATH_SUCCESS) return;

// RAM上の Hann 窓テーブル作成（起動時に1度だけ計算）
hann_sum = 0.0f;
for (uint32_t i = 0; i < FFT_LEN; ++i) {
    window[i] = 0.5f - 0.5f * cosf(6.28318530718f * (float)i / (float)(FFT_LEN - 1));
    hann_sum += window[i];
}
for (uint32_t i = 0; i < LOW_FFT_LEN; ++i)
    low_goertzel_window[i] = 0.5f - 0.5f * cosf(6.28318530718f * (float)i / (float)(LOW_FFT_LEN - 1));
const float normalization = 2.0f / hann_sum;
for (uint8_t rate = 0; rate < SUPPORTED_RATE_COUNT; ++rate)
    build_rate_tables(rate, normalization);

// Precompute the approximate power-to-dBFS display conversion at startup.
// Runtime FFT processing then uses a bit-field lookup instead of sqrt/log.
for (int exponent = DB_LUT_MIN_EXP; exponent <= 0; ++exponent) {
    float exponent_scale = 1.0f;
    for (int e = 0; e > exponent; --e) exponent_scale *= 0.5f;
    for (uint16_t mantissa = 0; mantissa < DB_LUT_MANTISSA_COUNT; ++mantissa) {
        const float significand = 1.0f + (float)mantissa / 256.0f;
        const float power = significand * exponent_scale;
        const float dbfs = 10.0f * log10f(power);
        float scaled = (dbfs - DB_FLOOR) * (100.0f / -DB_FLOOR);
        if (scaled < 0.0f) scaled = 0.0f;
        if (scaled > 100.0f) scaled = 100.0f;
        power_to_level_lut[exponent - DB_LUT_MIN_EXP][mantissa] = (uint8_t)(scaled + 0.5f);
    }
}
initialized = true;
}

void spectrum_set_sample_rate(uint32_t sample_rate)
{
for (uint8_t rate = 0; rate < SUPPORTED_RATE_COUNT; ++rate) {
if (sample_rate == supported_rates[rate]) {
active_rate_index = rate;
low_goertzel_enabled = rate >= 2;
low_goertzel_pos = 0;
memset(low_goertzel_q1, 0, sizeof(low_goertzel_q1));
memset(low_goertzel_q2, 0, sizeof(low_goertzel_q2));
memset((void *)low_goertzel_levels, 0, sizeof(low_goertzel_levels));
if (low_goertzel_enabled) {
    // Keep low-band resolution close to 22 Hz while avoiding an unnecessarily
    // long update window at 88.2/96 kHz.
    low_goertzel_window_len = rate < 4 ? LOW_FFT_LEN / 2 : LOW_FFT_LEN;
    low_goertzel_window_stride = LOW_FFT_LEN / low_goertzel_window_len;
    float low_window_sum = 0.0f;
    for (uint16_t i = 0; i < low_goertzel_window_len; ++i)
        low_window_sum += low_goertzel_window[i * low_goertzel_window_stride];
    const float normalization = 2.0f / low_window_sum;
    const float normalization_power = normalization * normalization;
    const float bin_hz = (float)sample_rate / (float)low_goertzel_window_len;
    low_goertzel_bin_count = 0;
    for (uint16_t bin = 1; bin < low_goertzel_window_len / 2; ++bin) {
        const float frequency = (float)bin * bin_hz;
        if (frequency >= band_edges[LOW_BAND_COUNT]) break;
        if (frequency < band_edges[0]) continue;
        const uint8_t index = low_goertzel_bin_count++;
        uint8_t band = 0;
        while (band + 1 < LOW_BAND_COUNT && frequency >= band_edges[band + 1]) ++band;
        const float omega = 6.28318530718f * frequency / (float)sample_rate;
        const float a = a_weight_gain(frequency);
        low_goertzel_coeff[index] = 2.0f * cosf(omega);
        low_goertzel_gain[index] = normalization_power * a * a;
        low_goertzel_band[index] = band;
    }
}
return;
}
}
}

static inline void low_goertzel_feed(float left, float right)
{
if (!low_goertzel_enabled) return;
const float window = low_goertzel_window[low_goertzel_pos * low_goertzel_window_stride];
const float input[2] = {left * (1.0f / 2147483648.0f) * window, right * (1.0f / 2147483648.0f) * window};
for (uint8_t channel = 0; channel < 2; ++channel) {
    for (uint8_t bin = 0; bin < low_goertzel_bin_count; ++bin) {
        const float q0 = input[channel] + low_goertzel_coeff[bin] * low_goertzel_q1[channel][bin] - low_goertzel_q2[channel][bin];
        low_goertzel_q2[channel][bin] = low_goertzel_q1[channel][bin];
        low_goertzel_q1[channel][bin] = q0;
    }
}
if (++low_goertzel_pos < low_goertzel_window_len) return;

for (uint8_t channel = 0; channel < 2; ++channel) {
    float band_power[LOW_BAND_COUNT] = {0.0f};
    for (uint8_t bin = 0; bin < low_goertzel_bin_count; ++bin) {
        const float q1 = low_goertzel_q1[channel][bin];
        const float q2 = low_goertzel_q2[channel][bin];
        const float magnitude_sq = q1 * q1 + q2 * q2 - low_goertzel_coeff[bin] * q1 * q2;
        band_power[low_goertzel_band[bin]] += magnitude_sq * low_goertzel_gain[bin];
        low_goertzel_q1[channel][bin] = 0.0f;
        low_goertzel_q2[channel][bin] = 0.0f;
    }
    for (uint8_t band = 0; band < LOW_BAND_COUNT; ++band) {
        low_goertzel_levels[channel][band] = power_to_level(band_power[band]);
    }
}
low_goertzel_pos = 0;
}

void spectrum_feed(float left_q31, float right_q31)
{
if (!initialized) return;
low_goertzel_feed(left_q31, right_q31);
if (state[fill_index] != BUFFER_FILLING) {
const uint8_t other = fill_index ^ 1u;
if (state[other] != BUFFER_FREE) return;
fill_index = other;
state[fill_index] = BUFFER_FILLING;
capture_pos = 0;
}
capture[fill_index][0][capture_pos] = left_q31 * (1.0f / 2147483648.0f);
capture[fill_index][1][capture_pos] = right_q31 * (1.0f / 2147483648.0f);
if (++capture_pos < FFT_LEN) return;

if (ready_index < 0) {
    state[fill_index] = BUFFER_READY;
    ready_index = (int8_t)fill_index;
    const uint8_t other = fill_index ^ 1u;
    if (state[other] == BUFFER_FREE) {
        fill_index = other;
        state[fill_index] = BUFFER_FILLING;
    }
} else {
    // Discard this analysis frame when the previous one has not been consumed.
}
capture_pos = 0;
}

void spectrum_feed_block(const float *left_q31, const float *right_q31, uint32_t length)
{
if (!initialized) return;
for (uint32_t i = 0; i < length; ++i)
spectrum_feed(left_q31[i], right_q31[i]);
}

// 指定ビンの |X|^2（係数を掛ける前）
static inline float bin_magnitude_sq(uint16_t bin)
{
float real, imag;
if (bin == FFT_LEN / 2) { real = fft_output[1]; imag = 0.0f; }
else { real = fft_output[2 * bin]; imag = fft_output[2 * bin + 1]; }
return real * real + imag * imag;
}

static void calculate_channel(const float *samples, uint8_t bands[SPECTRUM_BAND_COUNT])
{
// RAM上の Hann 窓テーブルを参照し、乗算のみで前処理
for (uint32_t i = 0; i < FFT_LEN; ++i) fft_input[i] = samples[i] * window[i];
arm_rfft_fast_f32(&fft, fft_input, fft_output, 0);

const uint8_t rate = active_rate_index;
const float *gain = bin_gain[rate];
const uint8_t *band_of = bin_band[rate];
float power[SPECTRUM_BAND_COUNT] = {0};

// |X|^2 × 事前計算済み係数（Aウェイト・正規化・平均/合計）を、割り当て済みバンドへ加算
for (uint16_t bin = first_bin[rate]; bin < last_bin[rate]; ++bin)
    power[band_of[bin]] += bin_magnitude_sq(bin) * gain[bin];

for (uint8_t band = 0; band < SPECTRUM_BAND_COUNT; ++band) {
    const uint16_t fb = fb_bin[rate][band];
    if (fb != 0)
        power[band] = bin_magnitude_sq(fb) * fb_gain[rate][band];
    bands[band] = power_to_level(power[band]);
}
}

bool spectrum_process(uint8_t left[SPECTRUM_BAND_COUNT], uint8_t right[SPECTRUM_BAND_COUNT])
{
if (!initialized || ready_index < 0) return false;
const uint8_t index = (uint8_t)ready_index;
ready_index = -1;
if (discard_next_frame) {
discard_next_frame = false;
state[index] = BUFFER_FREE;
return false;
}
discard_next_frame = true;
state[index] = BUFFER_PROCESSING;
calculate_channel(capture[index][0], left);
calculate_channel(capture[index][1], right);
if (low_goertzel_enabled) {
    for (uint8_t band = 0; band < LOW_BAND_COUNT; ++band) {
        left[band] = low_goertzel_levels[0][band];
        right[band] = low_goertzel_levels[1][band];
    }
}
state[index] = BUFFER_FREE;
return true;
}
