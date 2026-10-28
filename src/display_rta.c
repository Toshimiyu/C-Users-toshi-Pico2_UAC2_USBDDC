/*
* Copyright (c) 2026 Toshimiyu 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/


#include "display_rta.h"
#include "transmit_to_dac.h"
#include "ringbuffer.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdbool.h>

// ===================================================
// 液晶ディスプレイ GPIO ピン定義
// ===================================================
#ifndef LCD_SCK_PIN
#define LCD_SCK_PIN   18
#endif
#ifndef LCD_MOSI_PIN
#define LCD_MOSI_PIN  19
#endif
#ifndef LCD_CS_PIN
#define LCD_CS_PIN    17
#endif
#ifndef LCD_DC_PIN
#define LCD_DC_PIN    16
#endif
#ifndef LCD_BL_PIN
#define LCD_BL_PIN    20
#endif

// ===================================================
// カラーテーマ定義（共同制作：GEMINI Amber × COP Navy）
// ===================================================
#define COLOR_BG         0x0000 // 背景：ブラック
#define COLOR_TITLE      0x03B0 // メインタイトル（COPカラー：ネイビー＋少しシアン）
#define COLOR_DIVIDER    0x0112 // 区切り線（COPカラー：濃いネイビー）
#define COLOR_L_THEME    0xFA00 // L/Rメータ通常色（GEMINIカラー：ディープオレンジ）
#define COLOR_R_THEME    0xFA00 // L/Rメータ通常色（GEMINIカラー：ディープオレンジ）
#define COLOR_PEAK_WARN  0xFD20 // 警告：明るいアンバー
#define COLOR_PEAK_CLIP  0xF800 // クリップ：レッド
#define COLOR_FRAME_L    0x4200 // L/Rバー枠：ダークディープオレンジ
#define COLOR_FRAME_R    0x4200 // L/Rバー枠：ダークディープオレンジ
#define COLOR_FREQ_TEXT  0xFD20 // 周波数文字（GEMINIカラー：明るいアンバー）
#define COLOR_FMT_TEXT   0x03B0 // フォーマット表示（COPカラー：ネイビー＋少しシアン）
#define COLOR_PEAK_DOT   0xFFFF // ピークホールド：白
#define COLOR_CH_LABEL   0xFD20 // チャンネルラベル（GEMINIカラー：明るいアンバー）

// ===================================================
// ピークバー・RTAレイアウト パラメーター
// ===================================================
#define PEAK_HOLD_US      300000u
#define PEAK_FALL_SPEED   4     // 50msごとのピークマーカー落下幅（FFT・L/R共通）
#define METER_PEAK_FALL_SPEED PEAK_FALL_SPEED
#define RTA_BAR_RELEASE_PIXELS_PER_SEC 100u // バーの下降速度（約1秒で全高を落下）
#define PEAK_NONE_Y       0xFFFF// ピーク非表示を示す無効値

#define RTA_BAR_W         10
#define RTA_CELL_W        13
#define RTA_BAR_H         100
#define RTA_BAR_Y         65
#define RTA_L_X_START     10
#define RTA_R_X_START     167

#define RTA_LABEL_Y_TOP     (RTA_BAR_Y + RTA_BAR_H + 3)   // 168 px (上段: Y=168-175)
#define RTA_LABEL_Y_BOTTOM  (RTA_BAR_Y + RTA_BAR_H + 14)  // 179 px (下段: Y=179-186)
#define FONT_CHAR_WIDTH     8                             // 1文字あたりの実際の幅(8px)

// 全ASCII文字 (0x20 ' ' ～ 0x7E '') 対応 8x8 ビットマップフォント
static const uint8_t font8x8[95][8] = {
{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ' '
{0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, // '!'
{0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00}, // '"'
{0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00}, // '#'
{0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00}, // '$'
{0x00,0x63,0x66,0x0C,0x18,0x33,0x63,0x00}, // '%'
{0x1C,0x36,0x1C,0x3B,0x6E,0x66,0x3B,0x00}, // '&'
{0x06,0x0C,0x18,0x00,0x00,0x00,0x00,0x00}, // '''
{0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00}, // '('
{0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00}, // ')'
{0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, // '*'
{0x00,0x0C,0x0C,0x3E,0x0C,0x0C,0x00,0x00}, // '+'
{0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x18}, // ','
{0x00,0x00,0x00,0x3E,0x00,0x00,0x00,0x00}, // '-'
{0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, // '.'
{0x00,0x03,0x06,0x0C,0x18,0x30,0x60,0x00}, // '/'
{0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00}, // '0'
{0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00}, // '1'
{0x3C,0x66,0x06,0x0C,0x18,0x30,0x7E,0x00}, // '2'
{0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00}, // '3'
{0x06,0x0E,0x1E,0x36,0x66,0x7F,0x06,0x00}, // '4'
{0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00}, // '5'
{0x3C,0x66,0x60,0x7C,0x66,0x66,0x3C,0x00}, // '6'
{0x7E,0x66,0x0C,0x18,0x18,0x18,0x18,0x00}, // '7'
{0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00}, // '8'
{0x3C,0x66,0x66,0x3E,0x06,0x66,0x3C,0x00}, // '9'
{0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00}, // ':'
{0x00,0x18,0x18,0x00,0x18,0x18,0x30,0x00}, // ';'
{0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, // '<'
{0x00,0x00,0x3E,0x00,0x3E,0x00,0x00,0x00}, // '='
{0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x00}, // '>'
{0x3C,0x66,0x06,0x0C,0x18,0x00,0x18,0x00}, // '?'
{0x3C,0x66,0x6E,0x6E,0x60,0x62,0x3C,0x00}, // '@'
{0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00}, // 'A'
{0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00}, // 'B'
{0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00}, // 'C'
{0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00}, // 'D'
{0x7E,0x60,0x60,0x78,0x60,0x60,0x7E,0x00}, // 'E'
{0x7E,0x60,0x60,0x78,0x60,0x60,0x60,0x00}, // 'F'
{0x3C,0x66,0x60,0x6E,0x66,0x66,0x3C,0x00}, // 'G'
{0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00}, // 'H'
{0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, // 'I'
{0x1E,0x0C,0x0C,0x0C,0x0C,0x6C,0x38,0x00}, // 'J'
{0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00}, // 'K'
{0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00}, // 'L'
{0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00}, // 'M'
{0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x00}, // 'N'
{0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}, // 'O'
{0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00}, // 'P'
{0x3C,0x66,0x66,0x66,0x66,0x3C,0x0E,0x00}, // 'Q'
{0x7C,0x66,0x66,0x7C,0x6C,0x66,0x66,0x00}, // 'R'
{0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00}, // 'S'
{0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00}, // 'T'
{0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}, // 'U'
{0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00}, // 'V'
{0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, // 'W'
{0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00}, // 'X'
{0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00}, // 'Y'
{0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00}, // 'Z'
{0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00}, // '['
{0x00,0x60,0x30,0x18,0x0C,0x06,0x03,0x00}, // ''
{0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00}, // ']'
{0x18,0x3C,0x66,0x00,0x00,0x00,0x00,0x00}, // '^'
{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, // '_'
{0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00}, // '`'
{0x00,0x00,0x3C,0x06,0x3E,0x66,0x3E,0x00}, // 'a'
{0x60,0x60,0x7C,0x66,0x66,0x66,0x7C,0x00}, // 'b'
{0x00,0x00,0x3C,0x66,0x60,0x66,0x3C,0x00}, // 'c'
{0x06,0x06,0x3E,0x66,0x66,0x66,0x3E,0x00}, // 'd'
{0x00,0x00,0x3C,0x66,0x7E,0x60,0x3C,0x00}, // 'e'
{0x1C,0x30,0x7C,0x30,0x30,0x30,0x30,0x00}, // 'f'
{0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x3C}, // 'g'
{0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x00}, // 'h'
{0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00}, // 'i'
{0x0C,0x00,0x1C,0x0C,0x0C,0x0C,0x0C,0x38}, // 'j'
{0x60,0x60,0x66,0x6C,0x78,0x6C,0x66,0x00}, // 'k'
{0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, // 'l'
{0x00,0x00,0x66,0x7F,0x7F,0x6B,0x63,0x00}, // 'm'
{0x00,0x00,0x7C,0x66,0x66,0x66,0x66,0x00}, // 'n'
{0x00,0x00,0x3C,0x66,0x66,0x66,0x3C,0x00}, // 'o'
{0x00,0x00,0x7C,0x66,0x66,0x7C,0x60,0x60}, // 'p'
{0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x06}, // 'q'
{0x00,0x00,0x7C,0x66,0x60,0x60,0x60,0x00}, // 'r'
{0x00,0x00,0x3E,0x60,0x3C,0x06,0x7C,0x00}, // 's'
{0x18,0x18,0x7E,0x18,0x18,0x18,0x0E,0x00}, // 't'
{0x00,0x00,0x66,0x66,0x66,0x66,0x3E,0x00}, // 'u'
{0x00,0x00,0x66,0x66,0x66,0x3C,0x18,0x00}, // 'v'
{0x00,0x00,0x63,0x6B,0x7F,0x77,0x63,0x00}, // 'w'
{0x00,0x00,0x66,0x3C,0x18,0x3C,0x66,0x00}, // 'x'
{0x00,0x00,0x66,0x66,0x66,0x3E,0x06,0x3C}, // 'y'
{0x00,0x00,0x7E,0x0C,0x18,0x30,0x7E,0x00}, // 'z'
{0x0E,0x18,0x18,0x30,0x18,0x18,0x0E,0x00}, // '{'
{0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, // '|'
{0x70,0x18,0x18,0x0C,0x18,0x18,0x70,0x00}, // '}'
{0x3B,0x6E,0x00,0x00,0x00,0x00,0x00,0x00}  // ''
};   

// 周波数ラベル情報構造体定義
typedef struct {
uint8_t index;
const char *text;
} freq_label_t;   

static const freq_label_t freq_labels[] = {
{ 0,  "45"  },
{ 1,  "90"  },
{ 2,  "125" },
{ 3,  "360" },
{ 4,  "700" },
{ 5,  "1k4" },
{ 6,  "2k8" },
{ 7,  "5k6" },
{ 8,  "9k8" },
{ 9,  "18k" },
{ 10, "22k" }
};
#define NUM_FREQ_LABELS (sizeof(freq_labels) / sizeof(freq_labels[0]))   

// 描画動的データ保持用
static uint8_t cur_l_bands[SPECTRUM_BAND_COUNT] = {0};
static uint8_t cur_r_bands[SPECTRUM_BAND_COUNT] = {0};
static volatile uint8_t input_l_bands[SPECTRUM_BAND_COUNT] = {0};
static volatile uint8_t input_r_bands[SPECTRUM_BAND_COUNT] = {0};
static uint8_t cur_l_peak = 0;
static uint8_t cur_r_peak = 0;
static volatile uint8_t input_l_peak = 0;
static volatile uint8_t input_r_peak = 0;

static uint16_t prev_l_bands[SPECTRUM_BAND_COUNT] = {0}; // 描画中バーの高さ(px)
static uint16_t prev_r_bands[SPECTRUM_BAND_COUNT] = {0};
static uint32_t bar_last_update_us[2][SPECTRUM_BAND_COUNT] = {{0}};
static uint16_t prev_l_meter_width = 0;
static uint16_t prev_r_meter_width = 0;
static uint16_t peak_l_meter_width = 0;
static uint16_t peak_r_meter_width = 0;
static uint32_t peak_l_meter_hold_until = 0;
static uint32_t peak_r_meter_hold_until = 0;
static uint32_t peak_l_meter_next_fall = 0;
static uint32_t peak_r_meter_next_fall = 0;
static uint16_t last_l_meter_marker = PEAK_NONE_Y;
static uint16_t last_r_meter_marker = PEAK_NONE_Y;
static uint32_t splash_start_us = 0;
static uint8_t splash_stage = 0;   

// ピークドット演出用バッファ
static uint8_t peak_l_dots[SPECTRUM_BAND_COUNT] = {0};
static uint8_t peak_r_dots[SPECTRUM_BAND_COUNT] = {0};
static uint32_t peak_l_hold_until[SPECTRUM_BAND_COUNT] = {0};
static uint32_t peak_r_hold_until[SPECTRUM_BAND_COUNT] = {0};
static uint32_t peak_l_next_fall[SPECTRUM_BAND_COUNT] = {0};
static uint32_t peak_r_next_fall[SPECTRUM_BAND_COUNT] = {0};   

// 前回描画したピークドットのY座標追跡バッファ
static uint16_t last_l_peak_y[SPECTRUM_BAND_COUNT];
static uint16_t last_r_peak_y[SPECTRUM_BAND_COUNT];

static volatile bool reset_peak_dots_pending = false;
static volatile bool format_pending = false;
static volatile bool display_ready = false;
static volatile uint8_t requested_bit_depth = 32;
static volatile uint32_t requested_sample_rate_tenth_khz = 3840;
extern volatile uint32_t audio_last_data_us;
extern volatile bool audio_data_seen;   

static volatile bool display_dirty = false;
static bool render_pending = false;
static uint8_t render_step = 0;
static char format_render_text[20];
static uint8_t format_render_pos = 0;
static bool format_render_active = false;
// フォーマット表示（ビット深度 / サンプリング周波数）は横倍角で描画する。
#define FORMAT_TEXT_SCALE_X 2
#define FORMAT_TEXT_SCALE_Y 1
#define FORMAT_TEXT_Y     203
#define FORMAT_TEXT_X0    (160 - (19 * 8 * FORMAT_TEXT_SCALE_X) / 2)
#define DISPLAY_MIN_AUDIO_HEADROOM_SAMPLES 512   

static uint8_t lcd_fill_chunk[512];   

static void lcd_write_cmd(uint8_t cmd) {
gpio_put(LCD_DC_PIN, 0);
gpio_put(LCD_CS_PIN, 0);
spi_write_blocking(spi0, &cmd, 1);
gpio_put(LCD_CS_PIN, 1);
}   

static void lcd_write_data(uint8_t data) {
gpio_put(LCD_DC_PIN, 1);
gpio_put(LCD_CS_PIN, 0);
spi_write_blocking(spi0, &data, 1);
gpio_put(LCD_CS_PIN, 1);
}   

static void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
lcd_write_cmd(0x2A); // Column Address Set
gpio_put(LCD_DC_PIN, 1);
gpio_put(LCD_CS_PIN, 0);
uint8_t x_data[4] = {x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF};
spi_write_blocking(spi0, x_data, 4);
gpio_put(LCD_CS_PIN, 1);   

lcd_write_cmd(0x2B); // Row Address Set
gpio_put(LCD_DC_PIN, 1);
gpio_put(LCD_CS_PIN, 0);
uint8_t y_data[4] = {y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF};
spi_write_blocking(spi0, y_data, 4);
gpio_put(LCD_CS_PIN, 1);

lcd_write_cmd(0x2C); // Memory Write
}

static void lcd_fill_color(uint16_t color) {
lcd_set_window(0, 0, 319, 239);
gpio_put(LCD_DC_PIN, 1);
gpio_put(LCD_CS_PIN, 0);
uint8_t data[2] = {color >> 8, color & 0xFF};
for (int i = 0; i < 320 * 240; i++) {
spi_write_blocking(spi0, data, 2);
}
gpio_put(LCD_CS_PIN, 1);
}

static void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
if (x >= 320 || y >= 240 || w == 0 || h == 0) return;
if (x + w > 320) w = 320 - x;
if (y + h > 240) h = 240 - y;

lcd_set_window(x, y, x + w - 1, y + h - 1);
gpio_put(LCD_DC_PIN, 1);
gpio_put(LCD_CS_PIN, 0);

const uint8_t hi = (uint8_t)(color >> 8);
const uint8_t lo = (uint8_t)color;
for (uint32_t i = 0; i < sizeof(lcd_fill_chunk); i += 2) {
lcd_fill_chunk[i] = hi;
lcd_fill_chunk[i + 1] = lo;
}
uint32_t remaining = (uint32_t)w * h * 2u;
while (remaining > 0) {
const uint32_t chunk = remaining > sizeof(lcd_fill_chunk)
? sizeof(lcd_fill_chunk) : remaining;
spi_write_blocking(spi0, lcd_fill_chunk, chunk);
remaining -= chunk;
}
gpio_put(LCD_CS_PIN, 1);
}

static void lcd_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
lcd_fill_rect(x, y, w, 1, color);
lcd_fill_rect(x, y + h - 1, w, 1, color);
lcd_fill_rect(x, y, 1, h, color);
lcd_fill_rect(x + w - 1, y, 1, h, color);
}

static void lcd_draw_char_scaled(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg,
                                 uint8_t scale_x, uint8_t scale_y) {
if (c < ' ' || c > '~') c = ' ';
uint8_t idx = c - ' ';
uint16_t width = 8 * scale_x;
uint16_t height = 8 * scale_y;

if (x + width > 320 || y + height > 240) return;

lcd_set_window(x, y, x + width - 1, y + height - 1);
gpio_put(LCD_DC_PIN, 1);
gpio_put(LCD_CS_PIN, 0);

uint8_t buf[24 * 24 * 2];
int buf_idx = 0;

for (int row = 0; row < 8; row++) {
uint8_t line = font8x8[idx][row];
for (int sy = 0; sy < scale_y; sy++) {
for (int col = 0; col < 8; col++) {
uint16_t px_color = (line & (0x80 >> col)) ? color : bg;
uint8_t high = px_color >> 8;
uint8_t low = px_color & 0xFF;
for (int sx = 0; sx < scale_x; sx++) {
buf[buf_idx++] = high;
buf[buf_idx++] = low;
}
}
}
}

spi_write_blocking(spi0, buf, buf_idx);
gpio_put(LCD_CS_PIN, 1);
}

static void lcd_draw_char(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg, uint8_t scale) {
lcd_draw_char_scaled(x, y, c, color, bg, scale, scale);
}

static void lcd_draw_string(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg, uint8_t scale) {
while (*str) {
lcd_draw_char(x, y, *str, color, bg, scale);
x += 8 * scale;
str++;
}
}

static void format_audio_info(char *buf, uint8_t bit_depth, float sample_rate_khz) {
int ptr = 0;
if (bit_depth >= 10) buf[ptr++] = '0' + (bit_depth / 10);
buf[ptr++] = '0' + (bit_depth % 10);

const char *s1 = "bit / ";
while (*s1) buf[ptr++] = *s1++;

int int_part = (int)sample_rate_khz;
int frac_part = (int)((sample_rate_khz - (float)int_part) * 10.0f + 0.5f);
if (frac_part >= 10) { int_part++; frac_part = 0; }

if (int_part >= 100) {
buf[ptr++] = '0' + (int_part / 100);
int_part %= 100;
buf[ptr++] = '0' + (int_part / 10);
buf[ptr++] = '0' + (int_part % 10);
} else if (int_part >= 10) {
buf[ptr++] = '0' + (int_part / 10);
buf[ptr++] = '0' + (int_part % 10);
} else {
buf[ptr++] = '0' + int_part;
}

buf[ptr++] = '.';
buf[ptr++] = '0' + frac_part;
buf[ptr++] = 'k'; buf[ptr++] = 'H'; buf[ptr++] = 'z';
buf[ptr] = '\0';
}

void display_rta_set_format(uint8_t bit_depth, float sample_rate_khz) {
requested_bit_depth = bit_depth;
requested_sample_rate_tenth_khz = (uint32_t)(sample_rate_khz * 10.0f + 0.5f);
format_pending = true;
}

static void prepare_requested_format(uint8_t bit_depth, uint32_t sample_rate_tenth_khz) {
char buf[32];
format_audio_info(buf, bit_depth, (float)sample_rate_tenth_khz / 10.0f);
int len = 0; while (buf[len]) len++;
if (len > 19) len = 19;
memset(format_render_text, ' ', sizeof(format_render_text) - 1);
format_render_text[sizeof(format_render_text) - 1] = '\0';
const uint8_t start = (uint8_t)((19 - len) / 2);
memcpy(&format_render_text[start], buf, (size_t)len);
format_render_pos = 0;
format_render_active = true;
}

void display_rta_set_bands(const uint8_t l_bands[SPECTRUM_BAND_COUNT], const uint8_t r_bands[SPECTRUM_BAND_COUNT]) {
bool changed = false;
for (int i = 0; i < SPECTRUM_BAND_COUNT; i++) {
const uint8_t left = (l_bands[i] > 100) ? 100 : l_bands[i];
const uint8_t right = (r_bands[i] > 100) ? 100 : r_bands[i];
if (input_l_bands[i] != left || input_r_bands[i] != right) changed = true;
input_l_bands[i] = left;
input_r_bands[i] = right;
}
if (changed) display_dirty = true;
}

void display_rta_set_peak(uint8_t l_peak, uint8_t r_peak) {
const uint8_t left = (l_peak > 100) ? 100 : l_peak;
const uint8_t right = (r_peak > 100) ? 100 : r_peak;
if (input_l_peak != left || input_r_peak != right) display_dirty = true;
input_l_peak = left;
input_r_peak = right;
}

void display_rta_reset_levels(void) {
reset_peak_dots_pending = true;
display_dirty = true;
}

void display_rta_set_levels(const uint8_t l_bands[SPECTRUM_BAND_COUNT], const uint8_t r_bands[SPECTRUM_BAND_COUNT], uint8_t l_peak, uint8_t r_peak) {
display_rta_set_bands(l_bands, r_bands);
display_rta_set_peak(l_peak, r_peak);
}

static void update_horizontal_meter(
uint16_t y,
uint8_t new_val,
uint16_t *prev_width,
uint16_t *peak_width,
uint32_t *hold_until,
uint32_t *next_fall,
uint16_t *last_marker,
uint16_t normal_color)
{
const uint16_t max_w = 284;
const uint16_t block_w = 2;
const uint16_t raw_w = (new_val * max_w) / 100;
const uint16_t prev_w = *prev_width;
uint16_t new_w = prev_w;

if (raw_w > prev_w + block_w / 2) {
new_w = (uint16_t)(((raw_w + block_w / 2) / block_w) * block_w);
} else if (raw_w + block_w / 2 < prev_w) {
new_w = (uint16_t)(((raw_w + block_w / 2) / block_w) * block_w);
}
if (new_w > max_w) new_w = max_w;

uint16_t warn_x = (((max_w * 74) / 100) / block_w) * block_w;
uint16_t clip_x = (((max_w * 96) / 100) / block_w) * block_w;

if (new_w > prev_w) {
if (prev_w < warn_x) {
uint16_t start = prev_w;
uint16_t end = (new_w < warn_x) ? new_w : warn_x;

    if (end > start) {
        lcd_fill_rect(23 + start, y, end - start, 4, normal_color);
    }
}

if (new_w > warn_x && prev_w < clip_x) {
    uint16_t start = (prev_w > warn_x) ? prev_w : warn_x;
    uint16_t end = (new_w < clip_x) ? new_w : clip_x;

    if (end > start) {
        lcd_fill_rect(23 + start, y, end - start, 4, COLOR_PEAK_WARN);
    }
}

if (new_w > clip_x) {
    uint16_t start = (prev_w > clip_x) ? prev_w : clip_x;

    if (new_w > start) {
        lcd_fill_rect(23 + start, y, new_w - start, 4, COLOR_PEAK_CLIP);
    }
}
} else if (new_w < prev_w) {
lcd_fill_rect(23 + new_w, y, prev_w - new_w, 4, COLOR_BG);
}

const uint32_t now = time_us_32();
if (new_w >= *peak_width && new_w > 0) {
*peak_width = new_w;
*hold_until = now + PEAK_HOLD_US;
*next_fall = *hold_until;
} else if (*peak_width > new_w &&
(int32_t)(now - *hold_until) >= 0 &&
(int32_t)(now - *next_fall) >= 0) {
*peak_width = (*peak_width > METER_PEAK_FALL_SPEED)
? (uint16_t)(*peak_width - METER_PEAK_FALL_SPEED) : 0;
if (*peak_width < new_w) *peak_width = new_w;
*next_fall = now + 25000u;
}

const uint16_t desired_marker = (*peak_width > 0)
? (uint16_t)(*peak_width - 1u) : PEAK_NONE_Y;
if (*last_marker != PEAK_NONE_Y &&
(*last_marker != desired_marker || new_w != prev_w)) {
const uint16_t marker_x = *last_marker;
uint16_t restore_color = COLOR_BG;
if (marker_x < new_w) {
restore_color = (marker_x >= clip_x) ? COLOR_PEAK_CLIP :
((marker_x >= warn_x) ? COLOR_PEAK_WARN : normal_color);
}
lcd_fill_rect(23 + marker_x, y, 1, 4, restore_color);
}
if (desired_marker != PEAK_NONE_Y &&
(*last_marker != desired_marker || new_w != prev_w)) {
lcd_fill_rect(23 + desired_marker, y, 1, 4, COLOR_PEAK_DOT);
}
*last_marker = desired_marker;
*prev_width = new_w;
}

static void update_vertical_bar(
uint16_t x,
uint8_t new_val,
uint16_t *prev_height,
uint32_t *last_update_us,
uint8_t *peak_dot,
uint32_t *hold_until,
uint32_t *next_fall,
uint16_t *last_peak_y,
uint16_t color)
{
uint16_t max_h = 98;
uint16_t y_base = 163;

const uint32_t now = time_us_32();
const uint16_t target_h = (new_val * max_h) / 100;
const uint16_t prev_h = *prev_height;
uint16_t new_h;
if (target_h >= prev_h) {
// 上昇は即時反映する。
new_h = target_h;
*last_update_us = now;
} else {
// 下降は経過時間に比例させ、描画周期に依存しない速度で落とす。
const uint32_t elapsed_us = now - *last_update_us;
const uint32_t fall_px = (uint32_t)(((uint64_t)elapsed_us * RTA_BAR_RELEASE_PIXELS_PER_SEC) / 1000000u);
new_h = prev_h;
if (fall_px > 0) {
const uint16_t remaining = prev_h - target_h;
new_h = (fall_px >= remaining) ? target_h : (uint16_t)(prev_h - fall_px);
*last_update_us = now;
}
}

if (target_h >= *peak_dot && target_h > 0) {
*peak_dot = target_h;
*hold_until = now + PEAK_HOLD_US;
*next_fall = *hold_until;
} else if (*peak_dot > 0 &&
(int32_t)(now - *hold_until) >= 0 &&
(int32_t)(now - *next_fall) >= 0) {
if (*peak_dot > PEAK_FALL_SPEED) {
*peak_dot -= PEAK_FALL_SPEED;
} else {
*peak_dot = 0;
}
*next_fall = now + 25000u;
}
if (*peak_dot < target_h) *peak_dot = target_h;

uint16_t new_bar_top = (new_h > 0) ? (y_base - new_h + 1) : (y_base + 1);
uint16_t new_peak_y = (*peak_dot > 0) ? (y_base - *peak_dot + 1) : PEAK_NONE_Y;

if (new_h > prev_h) {
lcd_fill_rect(x + 1, y_base - new_h + 1, 8, new_h - prev_h, color);
} else if (new_h < prev_h) {
lcd_fill_rect(x + 1, y_base - prev_h + 1, 8, prev_h - new_h, COLOR_BG);
}
*prev_height = new_h;

const bool marker_visible = new_peak_y != PEAK_NONE_Y &&
new_peak_y >= 66 && new_peak_y <= 163 &&
(new_peak_y < new_bar_top || new_h == 0);
if (*last_peak_y != PEAK_NONE_Y &&
(!marker_visible || *last_peak_y != new_peak_y)) {
if (*last_peak_y >= 66 && *last_peak_y <= 163) {
const uint16_t restore_color =
(new_h > 0 && *last_peak_y >= new_bar_top && *last_peak_y <= y_base)
? color : COLOR_BG;
lcd_fill_rect(x + 1, *last_peak_y, 8, 1, restore_color);
}
*last_peak_y = PEAK_NONE_Y;
}

if (marker_visible && *last_peak_y != new_peak_y) {
lcd_fill_rect(x + 1, new_peak_y, 8, 1, COLOR_PEAK_DOT);
*last_peak_y = new_peak_y;
}
}

void display_rta_clear_screen(void) {
lcd_fill_color(COLOR_BG);
}

void display_rta_draw_splash_screen(void) {
static const char *const boot_steps[6] = {
    "Display Initialization",
    "DAC Initialization",
    "Core1 Audio Engine",
    "FFT Initialization",
    "USB Audio Ready",
    "System Ready"
};

lcd_draw_rect(8, 8, 304, 224, COLOR_DIVIDER);
lcd_draw_rect(10, 10, 300, 220, COLOR_DIVIDER);

lcd_draw_string(20, 20, "SYSTEM BOOT: Quad Heritage", COLOR_TITLE, COLOR_BG, 1);
lcd_fill_rect(20, 32, 280, 1, COLOR_DIVIDER);

lcd_draw_string(20, 44, "Pico2 UAC2 USB DDC", COLOR_FREQ_TEXT, COLOR_BG, 1);
lcd_draw_string(20, 58, "Copilot x GEMINI x ChatGPT x Claude", COLOR_CH_LABEL, COLOR_BG, 1);
lcd_draw_string(20, 72, "Customized by Toshimiyu", COLOR_L_THEME, COLOR_BG, 1);

lcd_fill_rect(20, 92, 280, 48, 0x0081);
lcd_draw_rect(20, 92, 280, 48, COLOR_DIVIDER);

lcd_draw_string(28, 101, "Architecture : Pico2 & PCM5100A", COLOR_FREQ_TEXT, 0x0081, 1);
lcd_draw_string(28, 117, "Color Theme  : Amber x Navy", COLOR_FREQ_TEXT, 0x0081, 1);

lcd_fill_rect(20, 150, 280, 1, COLOR_DIVIDER);
lcd_draw_string(20, 162, "Initializing Subsystems", COLOR_TITLE, COLOR_BG, 1);

splash_start_us = time_us_32();

splash_stage = 255;

lcd_draw_string(240, 162, ".", COLOR_TITLE, COLOR_BG, 1);
lcd_draw_string(20, 178, boot_steps[0], COLOR_DIVIDER, COLOR_BG, 1);
}


void display_rta_set_boot_stage(uint8_t stage)
{
    static const char *const boot_steps[6] = {
        "Display Initialization",
        "DAC Initialization",
        "Core1 Audio Engine",
        "FFT Initialization",
        "USB Audio Ready",
        "System Ready"
    };

    if(stage > 5)
    {
        stage = 5;
    }

    lcd_fill_rect(20, 178, 250, 12, COLOR_BG);

    lcd_draw_string(
        20,
        178,
        boot_steps[stage],
        COLOR_DIVIDER,
        COLOR_BG,
        1
    );

    char dots[7] = {0};

    for(uint8_t i = 0; i <= stage; i++)
    {
        dots[i] = '.';
    }

    lcd_fill_rect(240, 162, 48, 8, COLOR_BG);

    lcd_draw_string(
        240,
        162,
        dots,
        COLOR_TITLE,
        COLOR_BG,
        1
    );
}

void display_rta_hw_init(void) {
spi_init(spi0, 40000000);
gpio_set_function(LCD_SCK_PIN, GPIO_FUNC_SPI);
gpio_set_function(LCD_MOSI_PIN, GPIO_FUNC_SPI);

gpio_init(LCD_CS_PIN);
gpio_set_dir(LCD_CS_PIN, GPIO_OUT);
gpio_put(LCD_CS_PIN, 1);

gpio_init(LCD_DC_PIN);
gpio_set_dir(LCD_DC_PIN, GPIO_OUT);

gpio_init(LCD_BL_PIN);
gpio_set_dir(LCD_BL_PIN, GPIO_OUT);
gpio_put(LCD_BL_PIN, 1);
sleep_ms(100);

// Pico Display Pack 2.0 RGB channels are active-low on GP6, GP7, and GP8.
// Drive all three high so the unused color LED remains off.
for (uint pin = 6; pin <= 8; ++pin) {
    gpio_init(pin);
    gpio_put(pin, true);
    gpio_set_dir(pin, GPIO_OUT);
}

lcd_write_cmd(0x01); // SWRESET
sleep_ms(150);

lcd_write_cmd(0x11); // SLPOUT
sleep_ms(150);

lcd_write_cmd(0x21); // Display Inversion ON
lcd_write_cmd(0x29); // DISPON

lcd_write_cmd(0x3A); // COLMOD
lcd_write_data(0x55); // 16-bit color

lcd_write_cmd(0x36); // MADCTL
lcd_write_data(0x60); // 画面回転

lcd_fill_color(COLOR_BG);
}

// 周波数ラベル描画 (千鳥配置処理)
static void draw_rta_labels(uint16_t x_start) {
for (size_t i = 0; i < NUM_FREQ_LABELS; i++) {
uint8_t idx = freq_labels[i].index;
const char *text = freq_labels[i].text;

    uint16_t center_x = x_start + (idx * RTA_CELL_W) + (RTA_BAR_W / 2);
    uint8_t text_len = strlen(text);
    int16_t text_x = center_x - ((text_len * FONT_CHAR_WIDTH) / 2);

    bool is_bottom = (i % 2 != 0);
    uint16_t text_y = is_bottom ? RTA_LABEL_Y_BOTTOM : RTA_LABEL_Y_TOP;

    lcd_draw_string(text_x, text_y, text, COLOR_FREQ_TEXT, COLOR_BG, 1);
}
}

void display_rta_init(void) {
for (int i = 0; i < SPECTRUM_BAND_COUNT; i++) {
last_l_peak_y[i] = PEAK_NONE_Y;
last_r_peak_y[i] = PEAK_NONE_Y;
}
lcd_fill_color(COLOR_BG);

lcd_draw_string(16, 6, "Pico2 UAC2 USB DDC", COLOR_TITLE, COLOR_BG, 2);
lcd_fill_rect(10, 26, 300, 1, COLOR_DIVIDER);

lcd_draw_string(10, 30, "L", COLOR_CH_LABEL, COLOR_BG, 1);
lcd_draw_rect(22, 30, 288, 6, COLOR_FRAME_L);

lcd_draw_string(10, 39, "R", COLOR_CH_LABEL, COLOR_BG, 1);
lcd_draw_rect(22, 39, 288, 6, COLOR_FRAME_R);

lcd_fill_rect(10, 48, 300, 1, COLOR_DIVIDER);

lcd_draw_string(20, 52, "L-Channel", COLOR_CH_LABEL, COLOR_BG, 1);
lcd_draw_string(180, 52, "R-Channel", COLOR_CH_LABEL, COLOR_BG, 1);
lcd_fill_rect(159, 50, 2, 145, COLOR_DIVIDER);

/* FFTバー枠描画 */
for (uint8_t i = 0; i < SPECTRUM_BAND_COUNT; ++i) {
const uint16_t lx = RTA_L_X_START + i * RTA_CELL_W;
const uint16_t rx = RTA_R_X_START + i * RTA_CELL_W;

lcd_draw_rect(lx, RTA_BAR_Y, RTA_BAR_W, RTA_BAR_H, COLOR_FRAME_L);
lcd_draw_rect(rx, RTA_BAR_Y, RTA_BAR_W, RTA_BAR_H, COLOR_FRAME_R);
}

/* 周波数ラベル描画 (千鳥配置呼び出し) */
draw_rta_labels(RTA_L_X_START);
draw_rta_labels(RTA_R_X_START);

lcd_fill_rect(10, 198, 300, 1, COLOR_DIVIDER);
display_rta_set_format(32, 384.0f);
display_ready = true;
}

void display_rta_service(void) {
if (!display_ready) return;

const uint32_t now = time_us_32();
const bool timed_out = audio_data_seen && (uint32_t)(now - audio_last_data_us) >= 200000u;
const bool audio_active = audio_data_seen && !timed_out;
if (!audio_active) return;

if (dma_tx.using < (DEPTH_DMA_TX_BUFFER / 2) &&
audio_ring_total_frames() < DISPLAY_MIN_AUDIO_HEADROOM_SAMPLES)
return;

uint8_t spectrum_l[SPECTRUM_BAND_COUNT];
uint8_t spectrum_r[SPECTRUM_BAND_COUNT];
if (spectrum_process(spectrum_l, spectrum_r))
display_rta_set_bands(spectrum_l, spectrum_r);
}

static bool peak_decay_due(void) {
const uint32_t now = time_us_32();
if ((peak_l_meter_width > prev_l_meter_width &&
(int32_t)(now - peak_l_meter_hold_until) >= 0 &&
(int32_t)(now - peak_l_meter_next_fall) >= 0) ||
(peak_r_meter_width > prev_r_meter_width &&
(int32_t)(now - peak_r_meter_hold_until) >= 0 &&
(int32_t)(now - peak_r_meter_next_fall) >= 0))
return true;
for (uint8_t i = 0; i < SPECTRUM_BAND_COUNT; ++i) {
if ((peak_l_dots[i] > 0 &&
(int32_t)(now - peak_l_hold_until[i]) >= 0 &&
(int32_t)(now - peak_l_next_fall[i]) >= 0) ||
(peak_r_dots[i] > 0 &&
(int32_t)(now - peak_r_hold_until[i]) >= 0 &&
(int32_t)(now - peak_r_next_fall[i]) >= 0))
return true;
}
return false;
}

void display_rta_render_service(void) {
if (!display_ready) return;

const uint32_t now = time_us_32();
const bool audio_active = audio_data_seen &&
(uint32_t)(now - audio_last_data_us) < 200000u;
const bool audio_slack = audio_active
? ((audio_ring_total_frames() >= DISPLAY_MIN_AUDIO_HEADROOM_SAMPLES) ||
(dma_tx.using >= (DEPTH_DMA_TX_BUFFER / 2)))
: !enable_output;

const bool format_slack = audio_active
? ((audio_ring_total_frames() >=
DISPLAY_MIN_AUDIO_HEADROOM_SAMPLES) ||
(dma_tx.using >= (DEPTH_DMA_TX_BUFFER / 2)))
: !enable_output;
if (format_pending || format_render_active) {
if (!format_slack) return;
if (format_pending) {
const uint8_t bit_depth = requested_bit_depth;
const uint32_t sample_rate = requested_sample_rate_tenth_khz;
format_pending = false;
prepare_requested_format(bit_depth, sample_rate);
}
const uint16_t x = (uint16_t)FORMAT_TEXT_X0 + (uint16_t)format_render_pos * (8u * FORMAT_TEXT_SCALE_X);
lcd_draw_char_scaled(x, FORMAT_TEXT_Y, format_render_text[format_render_pos],
COLOR_FMT_TEXT, COLOR_BG, FORMAT_TEXT_SCALE_X, FORMAT_TEXT_SCALE_Y);
if (++format_render_pos >= 19) format_render_active = false;
return;
}

if (!audio_slack) return;

if (!display_dirty && !reset_peak_dots_pending && !render_pending && !peak_decay_due()) return;
if (!render_pending) display_dirty = false;
display_rta_update();
}

void display_rta_update(void) {
if (!render_pending) {
if (reset_peak_dots_pending) {
reset_peak_dots_pending = false;
for (int i = 0; i < SPECTRUM_BAND_COUNT; ++i) {
input_l_bands[i] = 0;
input_r_bands[i] = 0;
}
input_l_peak = 0;
input_r_peak = 0;
memset(peak_l_dots, 0, sizeof(peak_l_dots));
memset(peak_r_dots, 0, sizeof(peak_r_dots));
memset(peak_l_hold_until, 0, sizeof(peak_l_hold_until));
memset(peak_r_hold_until, 0, sizeof(peak_r_hold_until));
memset(peak_l_next_fall, 0, sizeof(peak_l_next_fall));
memset(peak_r_next_fall, 0, sizeof(peak_r_next_fall));
}
for (int i = 0; i < SPECTRUM_BAND_COUNT; ++i) {
cur_l_bands[i] = input_l_bands[i];
cur_r_bands[i] = input_r_bands[i];
}
cur_l_peak = input_l_peak;
cur_r_peak = input_r_peak;
render_step = 0;
render_pending = true;
}

if (render_step == 0) {
update_horizontal_meter(31, cur_l_peak, &prev_l_meter_width,
&peak_l_meter_width, &peak_l_meter_hold_until,
&peak_l_meter_next_fall, &last_l_meter_marker,
COLOR_L_THEME);
} else if (render_step == 1) {
update_horizontal_meter(40, cur_r_peak, &prev_r_meter_width,
&peak_r_meter_width, &peak_r_meter_hold_until,
&peak_r_meter_next_fall, &last_r_meter_marker,
COLOR_R_THEME);
} else {
const uint8_t band = render_step - 2;
const uint16_t bar_step = 13;
const uint16_t lx = 10 + band * bar_step;
const uint16_t rx = 167 + band * bar_step;
update_vertical_bar(lx, cur_l_bands[band], &prev_l_bands[band],
&bar_last_update_us[0][band],
&peak_l_dots[band], &peak_l_hold_until[band],
&peak_l_next_fall[band],
&last_l_peak_y[band], COLOR_L_THEME);
update_vertical_bar(rx, cur_r_bands[band], &prev_r_bands[band],
&bar_last_update_us[1][band],
&peak_r_dots[band], &peak_r_hold_until[band],
&peak_r_next_fall[band],
&last_r_peak_y[band], COLOR_R_THEME);
}
if (++render_step >= SPECTRUM_BAND_COUNT + 2)
render_pending = false;
}
