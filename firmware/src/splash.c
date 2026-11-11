/* SPDX-License-Identifier: GPL-3.0-only */
/* AUDILUNE boot splash:
 * Screen 1: Crescent moon logo only (2 seconds)
 * Screen 2: Bold 3D-embossed white AUDILUNE wordmark + version (2 seconds) */
#ifndef UI_SMALL
#define UI_SMALL
#endif

#include "sloop_logo.h"

/* Watchdog-safe hardware tick delay (works before Timer5 interrupts start) */
UI_SMALL static void splash_wait_ms(uint32_t ms)
{
    while (ms--) {
        uint32_t t0 = fm1_ticks();
        fm1_wdt_feed();
        while ((uint32_t)(fm1_ticks() - t0) < 1000u * FM1_TICKS_PER_US)
            ;
    }
}

UI_SMALL static void sloop_logo_draw(uint32_t y0)
{
    uint32_t r0;
    for (r0 = 0; r0 < SLOOP_SPLASH_H; r0 += 120u) {
        uint32_t rows = SLOOP_SPLASH_H - r0 > 120u ? 120u : SLOOP_SPLASH_H - r0;
        uint32_t lo = r0 * SLOOP_SPLASH_W, hi = (r0 + rows) * SLOOP_SPLASH_W, pos = 0, i;
        cv_begin(SLOOP_SPLASH_W, rows, C_BLACK);
        for (i = 0; i < sizeof SLOOP_SPLASH_RLE && pos < hi; i++) {
            uint32_t run = (SLOOP_SPLASH_RLE[i] >> 4) + 1u, c = SLOOP_SPLASH_RLE[i] & 15u, p;
            if (c && pos + run > lo)
                for (p = pos < lo ? lo : pos; p < pos + run && p < hi; p++)
                    cv_px[p - lo] = swap16(SLOOP_SPLASH_PAL[c]);
            pos += run;
        }
        cv_blit((240u - SLOOP_SPLASH_W) / 2u, y0 + r0);
    }
}

/* Draws bold geometric letters for "AUDILUNE" (each glyph is 18x26 px, stroke 4 px) */
UI_SMALL static void audilune_word_pass(int32_t ox, int32_t oy, uint16_t col)
{
    const int32_t w = 18, h = 26, s = 4, step = 23;
    int32_t x = ox;

    /* A */
    lcd_fill(x + 2, oy, w - 4, s, col);
    lcd_fill(x, oy + 2, s, h - 2, col);
    lcd_fill(x + w - s, oy + 2, s, h - 2, col);
    lcd_fill(x + s, oy + 12, w - 2 * s, s, col);
    x += step;

    /* U */
    lcd_fill(x, oy, s, h - 2, col);
    lcd_fill(x + w - s, oy, s, h - 2, col);
    lcd_fill(x + 2, oy + h - s, w - 4, s, col);
    x += step;

    /* D */
    lcd_fill(x, oy, s, h, col);
    lcd_fill(x + s, oy, w - s - 3, s, col);
    lcd_fill(x + s, oy + h - s, w - s - 3, s, col);
    lcd_fill(x + w - s, oy + 3, s, h - 6, col);
    x += step;

    /* I (compact width 10 px) */
    lcd_fill(x, oy, 10, s, col);
    lcd_fill(x + 3, oy + s, 4, h - 2 * s, col);
    lcd_fill(x, oy + h - s, 10, s, col);
    x += 15;

    /* L */
    lcd_fill(x, oy, s, h, col);
    lcd_fill(x + s, oy + h - s, w - s, s, col);
    x += step;

    /* U */
    lcd_fill(x, oy, s, h - 2, col);
    lcd_fill(x + w - s, oy, s, h - 2, col);
    lcd_fill(x + 2, oy + h - s, w - 4, s, col);
    x += step;

    /* N */
    lcd_fill(x, oy, s, h, col);
    lcd_fill(x + w - s, oy, s, h, col);
    lcd_fill(x + 4, oy + 4, 3, 6, col);
    lcd_fill(x + 7, oy + 9, 4, 7, col);
    lcd_fill(x + 11, oy + 15, 3, 6, col);
    x += step;

    /* E */
    lcd_fill(x, oy, s, h, col);
    lcd_fill(x + s, oy, w - s, s, col);
    lcd_fill(x + s, oy + 11, w - s - 3, s, col);
    lcd_fill(x + s, oy + h - s, w - s, s, col);
}

/* Two-stage boot screen */
UI_SMALL static void sloop_splash(void)
{
    const char *ver = "v1.0";
    /* Total wordmark width = 7*23 + 15 - 5 = 171 px -> centered at x = 34 */
    const int32_t wx = 34;
    const int32_t wy = 96;

    /* Screen 1: Crescent moon logo centered vertically */
    lcd_fill(0, 0, 240, 240, C_BLACK);
    sloop_logo_draw((240u - SLOOP_SPLASH_H) / 2u);
    splash_wait_ms(2000);

    /* Screen 2: Floating 3D-embossed bold white AUDILUNE + version number */
    lcd_fill(0, 0, 240, 240, C_BLACK);
    /* Deep drop shadow (bottom-right) */
    audilune_word_pass(wx + 2, wy + 2, RGB(45, 50, 62));
    /* Raised bevel edge (top-left) */
    audilune_word_pass(wx - 1, wy - 1, RGB(155, 165, 180));
    /* Crisp bold white top face */
    audilune_word_pass(wx, wy, C_WHITE);

    /* Version number centered neatly underneath */
    cv_begin(240, 24, C_BLACK);
    cv_text(120 - text_w(&FONT_S, ver) / 2, 0, &FONT_S, ver, RGB(145, 155, 170));
    cv_blit(0, wy + 38);

    splash_wait_ms(2000);
}
