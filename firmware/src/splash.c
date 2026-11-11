/* SPDX-License-Identifier: GPL-3.0-only */
/* AUDILUNE boot splash: draws the crescent moon logo first, then AUDILUNE + version. */
#ifndef UI_SMALL
#define UI_SMALL
#endif
#ifndef FELUCCA_LOGO_TEXT
#define FELUCCA_LOGO_TEXT 0
#endif

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

#if FELUCCA_LOGO_TEXT
UI_SMALL static void sloop_logo_draw(uint32_t y0)
{
    static const char L[8] = {'A', 'U', 'D', 'I', 'L', 'U', 'N', 'E'};
    uint32_t i, w = text_w(&FONT_L, "AUDILUNE"), k = w * 3u <= 232u ? 3u : 2u, h = 32u, off, y, x;
    int32_t px = 0;
    char c[2] = {0, 0};
    cv_begin(240, h * k, C_BLACK);
    for (i = 0; i < 8u; i++) {
        c[0] = L[i];
        px = cv_text(px, 0, &FONT_L, c, i ? TE_COL_DEF[(i - 1u) & 3u] : C_WHITE);
    }
    off = (240u - w * k) / 2u;
    for (y = h * k; y-- > 0;)
        for (x = w * k; x-- > 0;)
            cv_px[y * 240u + off + x] = cv_px[(y / k) * 240u + x / k];
    for (y = 0; y < h * k; y++)
        for (x = 0; x < off; x++)
            cv_px[y * 240u + x] = swap16(C_BLACK);
    cv_blit(0, y0 + 60u);
}
#else
#include "sloop_logo.h"
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
#endif

/* Two-stage boot screen:
 * 1) Crescent moon logo only (2 seconds)
 * 2) AUDILUNE title and version number (2 seconds) */
UI_SMALL static void sloop_splash(void)
{
    const char *title = "AUDILUNE";
    const char *ver = "v1.0";

    /* Screen 1: Crescent moon logo only */
    lcd_fill(0, 0, 240, 240, C_BLACK);
    sloop_logo_draw(10);
    splash_wait_ms(2000);

    /* Screen 2: AUDILUNE + version number */
    lcd_fill(0, 0, 240, 240, C_BLACK);
    cv_begin(240, 64, C_BLACK);
    cv_text(120 - text_w(&FONT_L, title) / 2, 0, &FONT_L, title, C_WHITE);
    cv_text(120 - text_w(&FONT_S, ver) / 2, 36, &FONT_S, ver, RGB(196, 196, 204));
    cv_blit(0, 88);
    splash_wait_ms(2000);
}
