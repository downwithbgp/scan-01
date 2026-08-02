/* Scan 01 — racing-digits font (task T4, vision §5.6)
 *
 * Copyright 2026 Vadim Petrov
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef FONT_RACING_H
#define FONT_RACING_H

#include <stdint.h>

/* 36 glyphs (0-9, A-Z), 32 px tall (4 strips of 8), heavy condensed
 * grotesque — door-number style. Generated from tools/fontgen_racing.py
 * (the art there is the font source); do not edit font_racing.c by hand.
 * Advances: digits 16 px ("1" = 12), letters 14 px (M, W = 16), 1 px
 * inter-glyph spacing at render time. */
typedef struct {
    uint8_t width;
    uint8_t strips[4][16];
} RacingGlyph_t;

extern const RacingGlyph_t gRacingGlyphs[36];
extern const char          gRacingGlyphOrder[];      /* "0123456789ABC...XYZ" */
extern const uint8_t       gRacingGlyphAdvance[36];

/* Total rendered width of a string (advances + 1 px spacing). */
uint16_t RACING_TextWidth(const char *text);

/* Render a string right-aligned to `right_x` (exclusive) into the
 * framebuffer, blitting each glyph across 4 consecutive 8-row strips
 * starting at `line` (the gFontBig pattern). Unknown chars are skipped. */
void RACING_PrintNumber(uint8_t (*fb)[128], uint8_t line, uint8_t right_x,
                        const char *text);

#endif
