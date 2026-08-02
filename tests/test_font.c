/* Scan 01 — host tests for the racing-digits font (task T4 gate)
 *
 * Compile: gcc -Wall -Werror -Wextra -I. tests/test_font.c font_racing.c -o /tmp/test_font
 * Run:    /tmp/test_font
 *
 * The (hw) gate — screenshots via screenshot.c over UART, reviewed against
 * vision §5.6 — needs a real radio; these host tests verify the data
 * integrity and the renderer math (advances, right alignment, no clipping).
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "font_racing.h"

static int g_checks = 0;
static int g_failures = 0;

static void expect(bool cond, const char *what)
{
    g_checks++;
    if (!cond) {
        g_failures++;
        printf("FAIL: %s\n", what);
    }
}

int main(void)
{
    uint8_t fb[7][128];

    /* ---- advances per the spec: digits 16 ("1" = 12), letters 14 (M/W 16) ---- */
    expect(gRacingGlyphAdvance[0] == 16 && gRacingGlyphAdvance[1] == 12,
           "0=16, 1=12");
    for (int i = 2; i <= 9; i++)
        expect(gRacingGlyphAdvance[i] == 16, "digit advance 16");
    for (int i = 0; i < 26; i++) {
        char ch = gRacingGlyphOrder[10 + i];
        uint8_t want = (ch == 'M' || ch == 'W') ? 16 : 14;
        expect(gRacingGlyphAdvance[10 + i] == want, "letter advance");
    }

    /* ---- data integrity: every glyph has ink within its strips ---- */
    for (int i = 0; i < 36; i++) {
        const RacingGlyph_t *g = &gRacingGlyphs[i];
        int ink = 0;
        for (int s = 0; s < 4; s++)
            for (int c = 0; c < g->width; c++)
                ink += g->strips[s][c];
        expect(ink > 0, "glyph has ink");
        expect(g->width == gRacingGlyphAdvance[i], "width matches advance");
    }

    /* ---- text width: advances + 1 px spacing ---- */
    expect(RACING_TextWidth("29A") == 16 + 1 + 16 + 1 + 14, "29A = 48");
    expect(RACING_TextWidth("24") == 16 + 1 + 16, "24 = 33");
    expect(RACING_TextWidth("100") == 12 + 1 + 16 + 1 + 16, "100 = 46");
    expect(RACING_TextWidth("") == 0, "empty = 0");
    expect(RACING_TextWidth("2 4") == 16 + 1 + 16, "space skipped, spacing still 1");

    /* ---- render "29A" right-aligned to x=100 into lines 1..4 ----
     * property: glyphs land at their computed cells and their ink stays
     * within the advance; the right edge of the string is at right_x */
    memset(fb, 0, sizeof(fb));
    RACING_PrintNumber(fb, 1, 100, "29A");
    int leftmost = -1, rightmost = -1, ink = 0;
    for (int line = 1; line <= 4; line++)
        for (int x = 0; x < 100; x++)
            if (fb[line][x]) {
                ink++;
                if (leftmost < 0) leftmost = x;
                rightmost = x;
            }
    expect(ink > 0, "29A rendered ink");
    /* expected bounds from the glyph data: cell starts 52, 69, 86 */
    int cell = 100 - RACING_TextWidth("29A");
    int exp_left = -1, exp_right = -1;
    for (int gi = 0; gi < 3; gi++) {
        const char *p = strchr(gRacingGlyphOrder, "29A"[gi]);
        const RacingGlyph_t *g = &gRacingGlyphs[p - gRacingGlyphOrder];
        int first = 16, last = -1;
        for (int s = 0; s < 4; s++)
            for (int c = 0; c < g->width; c++)
                if (g->strips[s][c]) {
                    if (c < first) first = c;
                    if (c > last) last = c;
                }
        if (exp_left < 0) exp_left = cell + first;
        if (cell + last > exp_right) exp_right = cell + last;
        cell += g->width + 1;
    }
    expect(leftmost == exp_left, "left edge matches glyph ink");
    expect(rightmost == exp_right, "right edge matches glyph ink");
    expect(fb[1][100] == 0, "no ink past right_x");
    expect(fb[4][99] == 0 && rightmost < 100, "ink strictly within the zone");
    /* lines 0 and 5..6 untouched (32 px = 4 strips) */
    int outside = 0;
    for (int x = 0; x < 128; x++)
        outside += fb[0][x] + fb[5][x] + fb[6][x];
    expect(outside == 0, "no ink outside the 4-strip block");

    /* ---- no clipping: every glyph rendered alone fits its advance ---- */
    for (int i = 0; i < 36; i++) {
        char s[2] = { gRacingGlyphOrder[i], 0 };
        memset(fb, 0, sizeof(fb));
        RACING_PrintNumber(fb, 0, gRacingGlyphAdvance[i], s);
        int any = 0;
        for (int l = 0; l < 4; l++)
            for (int x = 0; x < gRacingGlyphAdvance[i]; x++)
                any += fb[l][x];
        expect(any > 0, "single glyph renders");
    }

    /* ---- underflow guard: text wider than the zone clamps, no wrap ---- */
    memset(fb, 0, sizeof(fb));
    RACING_PrintNumber(fb, 0, 10, "29A");    /* 48 > 10 */
    int clipped = 0;
    for (int l = 0; l < 4; l++)
        for (int x = 0; x < 128; x++)
            clipped += fb[l][x];
    expect(clipped > 0, "clamped render still draws");

    printf("racing font: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
