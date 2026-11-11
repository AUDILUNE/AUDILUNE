/* SPDX-License-Identifier: GPL-3.0-only */
/* AUDILUNE single-screen 240x240 boot splash (crescent moon + 3D-embossed wordmark + version). */
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

/* Single unified 240x240 boot splash held for 3 seconds */
UI_SMALL static void sloop_splash(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    sloop_logo_draw(0);
    splash_wait_ms(3000);
}
