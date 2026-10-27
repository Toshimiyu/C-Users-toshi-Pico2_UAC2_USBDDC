/*
* Copyright (c) 2026 Toshimiyu 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/


/*
 * Debug gauge (see debug_gauge.h). Independent of display_rta.c.
 */

#include "debug_gauge.h"

#if DEBUG_GAUGE_ENABLE

#include <string.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/structs/qmi.h"
#include "hardware/spi.h"
#include "common.h"
#include "transmit_to_dac.h"
#include "usb_device_control.h"

// ---- LCD pins : identical to display_rta.c ---------------------------------------------------------
#ifndef LCD_CS_PIN
#define LCD_CS_PIN 17
#endif
#ifndef LCD_DC_PIN
#define LCD_DC_PIN 16
#endif

// ---- Layout (320x240) -------------------------------------------------------------------------------
// display_rta.c: divider y=198, format text (scale 1) y=203..210 -> this gauge row y=218..231
#define G_LABEL_X 10
#define G_TEXT_Y 221
#define G_FRAME_X 36
#define G_FRAME_Y 218
#define G_FRAME_W 94
#define G_FRAME_H 14
#define G_IN_X (G_FRAME_X + 1)
#define G_IN_Y (G_FRAME_Y + 1)
#define G_IN_W (G_FRAME_W - 2)
#define G_IN_H (G_FRAME_H - 2)
#define G_PCT_X 136
#define G_DIV_X 184
#define G_CLOCK_X 208
#define G_STATE_X 256

#define G_UPDATE_US 200000u // 5 Hz
#define G_EMA_TAU_S 0.25f

// ---- Colors (RGB565, same convention as display_rta.c) ----------------------------------------------
#define C_BG 0x0000
#define C_FRAME 0x4208
#define C_LABEL 0xFD20 // amber (same as the frequency labels)
#define C_MARK 0xFFFF
#define C_GREEN 0x07E0
#define C_YELLOW 0xFFE0
#define C_ORANGE 0xFC00
#define C_RED 0xF800
#define C_GRAY 0x7BEF

#define DISPLAY_MIN_AUDIO_HEADROOM_SAMPLES 512 // same threshold as display_rta.c

extern volatile uint32_t audio_last_data_us;
extern volatile bool audio_data_seen;

// ---- 8x8 font (copy of the table in display_rta.c, MSB = left pixel) --------------------------------
static const uint8_t gauge_font8x8[95][8] = {
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

// ---- Minimal LCD routines ----------------------------------------------------------------------------
static uint8_t fill_chunk[256];

static void lcd_cmd(uint8_t cmd)
{
	gpio_put(LCD_DC_PIN, 0);
	gpio_put(LCD_CS_PIN, 0);
	spi_write_blocking(spi0, &cmd, 1);
	gpio_put(LCD_CS_PIN, 1);
}

static void lcd_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
	uint8_t xd[4] = {x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF};
	uint8_t yd[4] = {y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF};
	lcd_cmd(0x2A);
	gpio_put(LCD_DC_PIN, 1);
	gpio_put(LCD_CS_PIN, 0);
	spi_write_blocking(spi0, xd, 4);
	gpio_put(LCD_CS_PIN, 1);
	lcd_cmd(0x2B);
	gpio_put(LCD_DC_PIN, 1);
	gpio_put(LCD_CS_PIN, 0);
	spi_write_blocking(spi0, yd, 4);
	gpio_put(LCD_CS_PIN, 1);
	lcd_cmd(0x2C);
}

static void lcd_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
	if (x >= 320 || y >= 240 || w == 0 || h == 0)
		return;
	if (x + w > 320)
		w = 320 - x;
	if (y + h > 240)
		h = 240 - y;

	for (uint32_t i = 0; i < sizeof(fill_chunk); i += 2)
	{
		fill_chunk[i] = (uint8_t)(color >> 8);
		fill_chunk[i + 1] = (uint8_t)color;
	}
	lcd_window(x, y, x + w - 1, y + h - 1);
	gpio_put(LCD_DC_PIN, 1);
	gpio_put(LCD_CS_PIN, 0);
	uint32_t remaining = (uint32_t)w * h * 2u;
	while (remaining > 0)
	{
		const uint32_t n = remaining > sizeof(fill_chunk) ? sizeof(fill_chunk) : remaining;
		spi_write_blocking(spi0, fill_chunk, n);
		remaining -= n;
	}
	gpio_put(LCD_CS_PIN, 1);
}

static void lcd_char(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg)
{
	if (c < ' ' || c > '~')
		c = ' ';
	const uint8_t *glyph = gauge_font8x8[c - ' '];
	uint8_t buf[8 * 8 * 2];
	int n = 0;
	for (int row = 0; row < 8; row++)
		for (int col = 0; col < 8; col++)
		{
			const uint16_t px = (glyph[row] & (0x80 >> col)) ? fg : bg;
			buf[n++] = (uint8_t)(px >> 8);
			buf[n++] = (uint8_t)px;
		}
	lcd_window(x, y, x + 7, y + 7);
	gpio_put(LCD_DC_PIN, 1);
	gpio_put(LCD_CS_PIN, 0);
	spi_write_blocking(spi0, buf, sizeof(buf));
	gpio_put(LCD_CS_PIN, 1);
}

// ---- One-operation-per-call queue ----------------------------------------------------------------------
typedef struct
{
	uint8_t is_char;
	char c;
	uint16_t x, y, w, h, fg, bg;
} gauge_op_t;

#define Q_SIZE 40
static gauge_op_t queue[Q_SIZE];
static uint8_t q_head, q_tail, q_count;

static void q_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
	if (q_count >= Q_SIZE)
		return;
	queue[q_tail] = (gauge_op_t){.is_char = 0, .x = x, .y = y, .w = w, .h = h, .fg = color};
	q_tail = (q_tail + 1) % Q_SIZE;
	q_count++;
}

static void q_char(uint16_t x, uint16_t y, char c, uint16_t fg)
{
	if (q_count >= Q_SIZE)
		return;
	queue[q_tail] = (gauge_op_t){.is_char = 1, .c = c, .x = x, .y = y, .fg = fg, .bg = C_BG};
	q_tail = (q_tail + 1) % Q_SIZE;
	q_count++;
}

// ---- Gauge state -------------------------------------------------------------------------------------------
static bool inited = false;
static float level_ema = 0.0f;
static uint32_t last_sample_us = 0, last_update_us = 0;

// what is currently drawn (forces a full draw at the beginning)
static int shown_fill = -1;
static uint16_t shown_color = 0;
static char shown_pct[6] = "     ";
static char shown_div[3] = "  ";
static char shown_clock[5] = "    ";
static char shown_state[5] = "    ";
static uint16_t shown_state_color = 0;

static const char *state_text(usb_audio_state_t st)
{
	switch (st)
	{
	case USB_AUDIO_BUFFERING:
		return "BUFF";
	case USB_AUDIO_STREAMING:
		return "STRM";
	case USB_AUDIO_OVERFLOW_FATAL:
		return "OVF!";
	case USB_AUDIO_UNDERFLOW_FATAL:
		return "UDF!";
	case USB_AUDIO_IDLE:
	default:
		return "IDLE";
	}
}

// green: normal / yellow: BUFFERING / orange: near-or-in UNDERFLOW / red: near-or-in OVERFLOW / gray: IDLE
static uint16_t state_color(usb_audio_state_t st, float level)
{
	switch (st)
	{
	case USB_AUDIO_BUFFERING:
		return C_YELLOW;
	case USB_AUDIO_OVERFLOW_FATAL:
		return C_RED;
	case USB_AUDIO_UNDERFLOW_FATAL:
		return C_ORANGE;
	case USB_AUDIO_STREAMING:
		if (level >= (float)SIZE_AUDIO_RING_FRAMES * 7.0f / 8.0f)
			return C_RED;
		if (level <= (float)SIZE_AUDIO_RING_FRAMES / 8.0f)
			return C_ORANGE;
		return C_GREEN;
	case USB_AUDIO_IDLE:
	default:
		return C_GRAY;
	}
}

static void queue_text(uint16_t x, const char *s, int len, uint16_t color)
{
	for (int i = 0; i < len; i++)
		q_char(x + 8 * i, G_TEXT_Y, s[i], color);
}

static inline uint16_t target_px(void)
{
	return (uint16_t)(((uint32_t)G_IN_W * SIZE_BUFFER_FB_THRESHOLD) / SIZE_AUDIO_RING_FRAMES);
}

void debug_gauge_init(void)
{
	// static frame: label, frame, empty interior, target marker
	queue_text(G_LABEL_X, "BUF", 3, C_LABEL);
	q_rect(G_FRAME_X, G_FRAME_Y, G_FRAME_W, 1, C_FRAME);
	q_rect(G_FRAME_X, G_FRAME_Y + G_FRAME_H - 1, G_FRAME_W, 1, C_FRAME);
	q_rect(G_FRAME_X, G_FRAME_Y, 1, G_FRAME_H, C_FRAME);
	q_rect(G_FRAME_X + G_FRAME_W - 1, G_FRAME_Y, 1, G_FRAME_H, C_FRAME);
	q_rect(G_IN_X, G_IN_Y, G_IN_W, G_IN_H, C_BG);
	q_rect(G_IN_X + target_px(), G_IN_Y, 1, G_IN_H, C_MARK);

	shown_fill = 0;
	shown_color = C_BG;
	memcpy(shown_pct, "     ", 6);
	memcpy(shown_div, "  ", 3);
	memcpy(shown_clock, "    ", 5);
	memcpy(shown_state, "    ", 5);
	shown_state_color = 0;
	last_sample_us = time_us_32();
	last_update_us = last_sample_us;
	level_ema = (float)audio_ring_total_frames(); // start from the current level (no ramp-up)
	inited = true;
}

static void refresh(float level, usb_audio_state_t st)
{
	// --- bar ---
	int fill = (int)(level * (float)G_IN_W / (float)SIZE_AUDIO_RING_FRAMES + 0.5f);
	if (fill < 0)
		fill = 0;
	if (fill > G_IN_W)
		fill = G_IN_W;
	const uint16_t color = state_color(st, level);
	const uint16_t mark_x = G_IN_X + target_px();

	if (color != shown_color)
	{
		// repaint the whole interior with the new color
		if (fill > 0)
			q_rect(G_IN_X, G_IN_Y, (uint16_t)fill, G_IN_H, color);
		if (fill < G_IN_W)
			q_rect(G_IN_X + (uint16_t)fill, G_IN_Y, (uint16_t)(G_IN_W - fill), G_IN_H, C_BG);
		q_rect(mark_x, G_IN_Y, 1, G_IN_H, C_MARK);
		shown_fill = fill;
		shown_color = color;
	}
	else if (fill > shown_fill)
	{
		q_rect(G_IN_X + (uint16_t)shown_fill, G_IN_Y, (uint16_t)(fill - shown_fill), G_IN_H, color);
		q_rect(mark_x, G_IN_Y, 1, G_IN_H, C_MARK);
		shown_fill = fill;
	}
	else if (fill < shown_fill)
	{
		q_rect(G_IN_X + (uint16_t)fill, G_IN_Y, (uint16_t)(shown_fill - fill), G_IN_H, C_BG);
		q_rect(mark_x, G_IN_Y, 1, G_IN_H, C_MARK);
		shown_fill = fill;
	}

	// Buffer percentage, QMI divider, and derived flash clock share this row.
	float pct = level * 100.0f / (float)SIZE_AUDIO_RING_FRAMES;
	if (pct > 99.9f)
		pct = 99.9f;
	if (pct < 0.0f)
		pct = 0.0f;
	char pct_text[6];
	const int tenths = (int)(pct * 10.0f + 0.5f);
	pct_text[0] = (tenths >= 100) ? (char)('0' + tenths / 100) : ' ';
	pct_text[1] = (char)('0' + (tenths / 10) % 10);
	pct_text[2] = '.';
	pct_text[3] = (char)('0' + tenths % 10);
	pct_text[4] = '%';
	pct_text[5] = '\0';
	for (int i = 0; i < 5; i++)
		if (pct_text[i] != shown_pct[i])
		{
			q_char(G_PCT_X + 8 * i, G_TEXT_Y, pct_text[i], C_LABEL);
			shown_pct[i] = pct_text[i];
		}

	uint32_t flash_div = (qmi_hw->m[0].timing & QMI_M0_TIMING_CLKDIV_BITS) >> QMI_M0_TIMING_CLKDIV_LSB;
	if (flash_div == 0)
		flash_div = 256;
	const uint32_t flash_mhz = clock_get_hz(clk_sys) / flash_div / 1000000u;
	char div_text[3] = {'D', flash_div <= 9 ? (char)('0' + flash_div) : '?', '\0'};
	for (int i = 0; i < 2; i++)
		if (div_text[i] != shown_div[i])
		{
			q_char(G_DIV_X + 8 * i, G_TEXT_Y, div_text[i], C_LABEL);
			shown_div[i] = div_text[i];
		}
	char clock_text[5] = {
		flash_mhz >= 100 ? (char)('0' + (flash_mhz / 100) % 10) : ' ',
		(char)('0' + (flash_mhz / 10) % 10),
		(char)('0' + flash_mhz % 10), 'M', '\0'};
	for (int i = 0; i < 4; i++)
		if (clock_text[i] != shown_clock[i])
		{
			q_char(G_CLOCK_X + 8 * i, G_TEXT_Y, clock_text[i], C_LABEL);
			shown_clock[i] = clock_text[i];
		}

	// --- state code (colored like the bar) ---
	const char *stxt = state_text(st);
	for (int i = 0; i < 4; i++)
		if (stxt[i] != shown_state[i] || color != shown_state_color)
		{
			q_char(G_STATE_X + 8 * i, G_TEXT_Y, stxt[i], color);
			shown_state[i] = stxt[i];
		}
	shown_state_color = color;
}

// Same rule as display_rta_render_service(): draw only when the audio side has headroom
static bool audio_has_slack(void)
{
	const uint32_t now = time_us_32();
	const bool audio_active = audio_data_seen && (uint32_t)(now - audio_last_data_us) < 200000u;
	if (audio_active)
		return audio_ring_total_frames() >= DISPLAY_MIN_AUDIO_HEADROOM_SAMPLES ||
			   dma_tx.using >= (DEPTH_DMA_TX_BUFFER / 2);
	return !enable_output;
}

void debug_gauge_service(void)
{
	if (!inited)
		return;

	// smoothed level (sampled on every call, independent of drawing)
	const uint32_t now = time_us_32();
	const float dt = (float)(uint32_t)(now - last_sample_us) * 1e-6f;
	last_sample_us = now;
	const float alpha = dt / (G_EMA_TAU_S + dt);
	level_ema += ((float)audio_ring_total_frames() - level_ema) * alpha;

	if (q_count == 0)
	{
		if ((uint32_t)(now - last_update_us) >= G_UPDATE_US)
		{
			last_update_us = now;
			refresh(level_ema, usb_audio_get_state());
		}
		return;
	}

	if (!audio_has_slack())
		return;

	// exactly one SPI operation per call
	const gauge_op_t op = queue[q_head];
	q_head = (q_head + 1) % Q_SIZE;
	q_count--;
	if (op.is_char)
		lcd_char(op.x, op.y, op.c, op.fg, op.bg);
	else
		lcd_rect(op.x, op.y, op.w, op.h, op.fg);
}

#endif /* DEBUG_GAUGE_ENABLE */
