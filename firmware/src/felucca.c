/* SPDX-License-Identifier: GPL-3.0-only */
/* AUDILUNE Block 1: config_and_boot two-phase 4000ms startup splash */
#ifndef UI_SMALL
#define UI_SMALL
#endif

#include "sloop_logo.h"

#define AUDILUNE_TURQUOISE RGB(0, 197, 205) /* #00C5CD */

/* Phase 1: Centered AUDILUNE crescent moon graphic on #000000 black canvas */
UI_SMALL static void audilune_logo_draw(void)
{
    uint32_t r0;
    uint32_t x0 = (240u - SLOOP_SPLASH_W) / 2u;
    uint32_t y0 = (240u - SLOOP_SPLASH_H) / 2u;
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
        cv_blit(x0, y0 + r0);
    }
}

/* Phase 2: Vivid turquoise "AUDILUNE" (#00C5CD) + white "v1.0.0" (#FFFFFF) */
UI_SMALL static void audilune_version_draw(void)
{
    uint32_t w = text_w(&FONT_L, "AUDILUNE");
    uint32_t k = (w * 2u <= 220u) ? 2u : 1u;
    uint32_t h = 32u, y, x, off;

    /* Line 1: "AUDILUNE" in vivid turquoise (#00C5CD) */
    cv_begin(240, h * k, C_BLACK);
    cv_text(0, 0, &FONT_L, "AUDILUNE", AUDILUNE_TURQUOISE);
    off = (240u - w * k) / 2u;
    if (k > 1u) {
        for (y = h * k; y-- > 0;)
            for (x = w * k; x-- > 0;)
                cv_px[y * 240u + off + x] = cv_px[(y / k) * 240u + x / k];
        for (y = 0; y < h * k; y++)
            for (x = 0; x < off; x++)
                cv_px[y * 240u + x] = swap16(C_BLACK);
    }
    cv_blit(0, 82);

    /* Line 2: "v1.0.0" centered below in pure white (#FFFFFF) */
    cv_begin(240, 24, C_BLACK);
    cv_text(120 - text_w(&FONT_S, "v1.0.0") / 2, 0, &FONT_S, "v1.0.0", C_WHITE);
    cv_blit(0, 152);
}

/* Complete 4000ms two-phase boot splash sequence */
UI_SMALL static void sloop_splash(void)
{
    /* Phase 1: Graphic Splash (2000 ms) */
    lcd_fill(0, 0, 240, 240, C_BLACK);
    audilune_logo_draw();
    fm1_delay_ms(2000);

    /* Phase 2: Version Splash (2000 ms) */
    lcd_fill(0, 0, 240, 240, C_BLACK);
    audilune_version_draw();
    fm1_delay_ms(2000);

    /* Performance Initialization: clear to pure black at 4000 ms */
    lcd_fill(0, 0, 240, 240, C_BLACK);
}
