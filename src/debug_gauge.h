/*
* Copyright (c) 2026 Toshimiyu 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/


/*
 * Debug gauge: RingBuffer level meter drawn in one horizontal row under the format text (y=218..231).
 *
 *   BUF [########|---------]  50.1%  STRM
 *                ^ TARGET (SIZE_BUFFER_FB_THRESHOLD)
 *
 * Purpose: watch interleaved buffer_audio usage (the Feedback control input) while playing music.
 * Only the format text in display_rta.c was made smaller to free the row. This module has its own small
 * LCD routines (same SPI0 / pins), so it can be removed again with DEBUG_GAUGE_ENABLE = 0, and runs from the Core0 main loop, one SPI operation per call, only when the audio side has headroom
 * (same rule as display_rta_render_service) -> Audio > FFT > Display is kept.
 *
 * Set DEBUG_GAUGE_ENABLE to 0 for the final firmware.
 */
#ifndef _DEBUG_GAUGE_H_
#define _DEBUG_GAUGE_H_

#ifndef DEBUG_GAUGE_ENABLE
#define DEBUG_GAUGE_ENABLE (1)
#endif

#if DEBUG_GAUGE_ENABLE
// Call once AFTER display_rta_init() (which clears the screen)
extern void debug_gauge_init(void);
// Call every main-loop pass, after display_rta_render_service()
extern void debug_gauge_service(void);
#else
static inline void debug_gauge_init(void) {}
static inline void debug_gauge_service(void) {}
#endif

#endif /* _DEBUG_GAUGE_H_ */
