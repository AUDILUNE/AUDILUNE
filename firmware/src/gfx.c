/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* AUDILUNE Block 1: Small-canvas renderer, locked #000000 black background, #FFFFFF white
 * fallback, and 8 AUDILUNE themes (Teal #008080 default). */
#ifndef UI_SMALL
#if defined(__clang__)
#define UI_SMALL __attribute__((minsize))
#else
#define UI_SMALL
#endif
#endif
typedef struct {
    uint8_t h;
    uint8_t pad;
    uint8_t first, last;
    uint8_t sh;
    const uint8_t *adv;
    const uint8_t *bw;
    const uint16_t *off;
    const uint8_t *data;
    uint8_t bits;
} felucca_font_t;
#include "felucca_font.h"

#define CV_MAX (240u * 124u)
static uint16_t cv_px[CV_MAX] __attribute__((section(".pool")));
static uint32_t cv_w, cv_h;
static int32_t cv_oy;

#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
static uint16_t pal_bg = 0u;             /* Background Lock: strictly #000000 */
#define C_BLACK pal_bg
static uint16_t pal_hi = 0xFFFFu;        /* Default Foreground: strictly #FFFFFF */
#define C_WHITE pal_hi

typedef struct {
    const char *name;
    uint16_t c[5];
} palette_t;

/* AUDILUNE Block 1: 8 Color Themes (Teal #008080 default at index 0) */
static const palette_t PALETTES[] = {
    {"TEAL",     {RGB(0, 32, 32),   RGB(0, 64, 64),   RGB(0, 96, 96),   RGB(0, 128, 128), RGB(0, 197, 205)}}, /* #008080 */
    {"ORANGE",   {RGB(64, 27, 0),   RGB(128, 54, 0),  RGB(192, 80, 0),  RGB(255, 107, 0), RGB(255, 160, 64)}}, /* #FF6B00 */
    {"BURGUNDY", {RGB(32, 0, 8),    RGB(64, 0, 16),   RGB(96, 0, 24),   RGB(128, 0, 32),  RGB(190, 40, 72)}}, /* #800020 */
    {"PURPLE",   {RGB(32, 0, 32),   RGB(64, 0, 64),   RGB(96, 0, 96),   RGB(128, 0, 128), RGB(192, 64, 192)}}, /* #800080 */
    {"PINK",     {RGB(64, 26, 45),  RGB(128, 52, 90), RGB(192, 78, 135),RGB(255, 105, 180),RGB(255, 165, 210)}}, /* #FF69B4 */
    {"BLUE",     {RGB(0, 0, 64),    RGB(0, 0, 128),   RGB(0, 0, 192),   RGB(0, 0, 255),   RGB(96, 140, 255)}}, /* #0000FF */
    {"MINT",     {RGB(38, 64, 38),  RGB(76, 128, 76), RGB(114, 192, 114),RGB(152, 255, 152),RGB(200, 255, 200)}}, /* #98FF98 */
    {"GREY",     {RGB(32, 32, 32),  RGB(64, 64, 64),  RGB(96, 96, 96),  RGB(128, 128, 128),RGB(196, 196, 196)}}, /* #808080 */
};
#define NPALETTES (sizeof(PALETTES) / sizeof(PALETTES[0]))
static uint16_t pal[5];
#define C_LINE pal[0]
#define C_DIM pal[1]
#define C_GRAY pal[2]
#define C_AMB pal[3]
#define C_HI pal[4]
#define C_ACCENT pal[3] /* Active theme exact accent color */

#define THEME_MAGIC 0x324D4854u
#define THEME_MAGIC1 0x314D4854u
typedef struct {
    uint32_t magic;
    char name[8];
    uint16_t c[5];
    uint16_t trk[4];
    uint16_t bg;
    uint16_t hi;
    uint16_t rsv;
    uint32_t crc;
} theme_t;
_Static_assert(sizeof(theme_t) == 40u, "theme layout");
static theme_t theme;
static uint8_t theme_valid, theme_on;

static const uint16_t TE_COL_DEF[4] = {RGB(0, 128, 128), RGB(0, 197, 205), RGB(255, 255, 255), RGB(128, 128, 128)};
static const uint16_t TE_MID_DEF[4] = {RGB(0, 86, 86),   RGB(0, 132, 138), RGB(170, 170, 170), RGB(86, 86, 86)};
static const uint16_t TE_DIM_DEF[4] = {RGB(0, 44, 44),   RGB(0, 66, 68),   RGB(86, 86, 86),    RGB(44, 44, 44)};
static uint16_t TE_COL[4] = {RGB(0, 128, 128), RGB(0, 197, 205), RGB(255, 255, 255), RGB(128, 128, 128)};
static uint16_t TE_MID[4] = {RGB(0, 86, 86),   RGB(0, 132, 138), RGB(170, 170, 170), RGB(86, 86, 86)};
static uint16_t TE_DIM[4] = {RGB(0, 44, 44),   RGB(0, 66, 68),   RGB(86, 86, 86),    RGB(44, 44, 44)};

static uint16_t rgb565_scale(uint32_t c, uint32_t num, uint32_t den)
{
    uint32_t r = ((c >> 11) & 31u) * num / den, g = ((c >> 5) & 63u) * num / den, b = (c & 31u) * num / den;
    return (uint16_t)(r << 11 | g << 5 | b);
}
static uint32_t st_crc32(const void *p, uint32_t n);

static int32_t rgb565_ch(uint32_t c, uint32_t k)
{
    return k == 0u ? (int32_t)((c >> 11) & 31u) * 8 : k == 1u ? (int32_t)((c >> 5) & 63u) * 4 : (int32_t)(c & 31u) * 8;
}

static int theme_apart(uint32_t c, uint32_t bg)
{
    uint32_t k;
    for (k = 0; k < 3u; k++) {
        int32_t d = rgb565_ch(c, k) - rgb565_ch(bg, k);
        if (d >= 96 || d <= -96)
            return 1;
    }
    return 0;
}

static int theme_check(theme_t *t)
{
    uint32_t k;
    if (t->magic == THEME_MAGIC1) {
        if (((uint32_t)t->hi | (uint32_t)t->rsv << 16) != st_crc32(t, 32u))
            return 0;
        t->hi = 0xFFFFu;
        t->rsv = 0;
        t->magic = THEME_MAGIC;
        t->crc = st_crc32(t, __builtin_offsetof(theme_t, crc));
    }
    if (t->magic != THEME_MAGIC || t->crc != st_crc32(t, __builtin_offsetof(theme_t, crc)))
        return 0;
    for (k = 0; k < sizeof t->name; k++)
        if (t->name[k] && (t->name[k] < 0x20 || t->name[k] > 0x7E))
            return 0;
    return theme_apart(t->c[4], 0u);
}

static uint16_t rgb565_over(uint32_t c, uint32_t a, uint32_t n)
{
    uint32_t b = pal_bg, r, g, bl;
    r = (((b >> 11) & 31u) * (n - a) + ((c >> 11) & 31u) * a) / n;
    g = (((b >> 5) & 63u) * (n - a) + ((c >> 5) & 63u) * a) / n;
    bl = ((b & 31u) * (n - a) + (c & 31u) * a) / n;
    return (uint16_t)(r << 11 | g << 5 | bl);
}
static int theme_shown(void) { return theme_on && theme_valid; }

static const uint16_t TE_G_DEF[4] = {RGB(26, 26, 30), RGB(54, 54, 60), RGB(118, 118, 126), RGB(255, 255, 255)};
static uint16_t te_g[4] = {RGB(26, 26, 30), RGB(54, 54, 60), RGB(118, 118, 126), RGB(255, 255, 255)};
#define TE_G1 te_g[0]
#define TE_G2 te_g[1]
#define TE_G3 te_g[2]
#define TE_G4 te_g[3]

static uint16_t te_gray(uint32_t c)
{
    return (uint16_t)c;
}

static void palette_set(uint32_t i)
{
    uint32_t k;
    int cu = theme_shown();
    for (k = 0; k < 5u; k++)
        pal[k] = cu ? theme.c[k] : PALETTES[i % NPALETTES].c[k];
    /* AUDILUNE Block 1: Lock pure black background (#000000) and white fallback (#FFFFFF) */
    pal_bg = 0u;
    pal_hi = 0xFFFFu;
    for (k = 0; k < 4u; k++)
        te_g[k] = TE_G_DEF[k];
    for (k = 0; k < 4u; k++) {
        TE_COL[k] = cu ? theme.trk[k] : PALETTES[i % NPALETTES].c[3];
        TE_MID[k] = rgb565_scale(TE_COL[k], 2u, 3u);
        TE_DIM[k] = rgb565_scale(TE_COL[k], 1u, 3u);
    }
}

static inline uint16_t swap16(uint32_t c) { return (uint16_t)(((c >> 8) & 0xFFu) | ((c & 0xFFu) << 8)); }

static void cv_begin(uint32_t w, uint32_t h, uint16_t bg)
{
    uint32_t i, n;
    if (w * h > CV_MAX)
        h = CV_MAX / w;
    lcd_sync();
    cv_w = w;
    cv_h = h;
    n = w * h;
    for (i = 0; i < n; i++)
        cv_px[i] = swap16(bg);
}

static void cv_blit(uint32_t x, uint32_t y) { lcd_blit(x, y, cv_w, cv_h, cv_px); }

static void cv_blit_from(uint32_t x, uint32_t y, uint32_t r0)
{
    if (r0 < cv_h)
        lcd_blit(x, y + r0, cv_w, cv_h - r0, cv_px + r0 * cv_w);
}

static inline void cv_pset(int32_t x, int32_t y, uint16_t c)
{
    y += cv_oy;
    if ((uint32_t)x < cv_w && (uint32_t)y < cv_h)
        cv_px[(uint32_t)y * cv_w + (uint32_t)x] = swap16(c);
}

static void cv_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    int32_t x1 = x + w, y1 = y + h + cv_oy, i;
    uint16_t sc = swap16(c);
    y += cv_oy;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x1 > (int32_t)cv_w)
        x1 = (int32_t)cv_w;
    if (y1 > (int32_t)cv_h)
        y1 = (int32_t)cv_h;
    for (; y < y1; y++)
        for (i = x; i < x1; i++)
            cv_px[(uint32_t)y * cv_w + (uint32_t)i] = sc;
}

static void cv_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int32_t dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy, guard = 2000;
    while (guard--) {
        int32_t e2 = 2 * err;
        cv_pset(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static uint32_t glyph(const felucca_font_t *f, uint32_t ch)
{
    if ((ch >= 'a' && ch <= 'z') || (ch >= 0xE0u && ch <= 0xFEu && ch != 0xF7u))
        ch -= 32u;
    if (ch < f->first || ch > f->last || (ch >= 127u && ch < 160u))
        ch = '?';
    return ch - f->first;
}

static int32_t cv_text(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    uint16_t ramp[16];
    uint32_t a;
    for (a = 0; a < 16u; a++)
        ramp[a] = rgb565_over(c, a, 15u);
    for (; *s; s++) {
        uint32_t gi = glyph(f, (uint8_t)*s), gx, gy, w, bpr;
        const uint8_t *gd;
        w = f->bw[gi];
        gd = f->data + f->off[gi];
        if (f->bits == 1u) {
            uint32_t sw = w >> f->sh;
            for (gy = 0; gy < f->h; gy++)
                for (gx = 0; gx < w; gx++) {
                    uint32_t i = (gy >> f->sh) * sw + (gx >> f->sh);
                    if ((gd[i >> 3] >> (7u - (i & 7u))) & 1u)
                        cv_pset(x - f->pad + (int32_t)gx, y + (int32_t)gy, ramp[15]);
                }
            x += f->adv[gi];
            continue;
        }
        bpr = ((w >> f->sh) + 1u) / 2u;
        for (gy = 0; gy < f->h; gy++)
            for (gx = 0; gx < w; gx++) {
                uint32_t sx = gx >> f->sh, v = gd[(gy >> f->sh) * bpr + sx / 2u];
                v = (sx & 1u) ? (v & 15u) : (v >> 4);
                if (v)
                    cv_pset(x - f->pad + (int32_t)gx, y + (int32_t)gy, ramp[v]);
            }
        x += f->adv[gi];
    }
    return x;
}

static int32_t text_w(const felucca_font_t *f, const char *s)
{
    int32_t w = 0;
    for (; *s; s++)
        w += f->adv[glyph(f, (uint8_t)*s)];
    return w;
}

static void draw_text_box(uint32_t x, uint32_t y, uint32_t w, const felucca_font_t *f, const char *s,
                          uint16_t c, int align)
{
    int32_t tw = text_w(f, s), tx = 0;
    cv_begin(w, f->h, C_BLACK);
    if (align == 1)
        tx = ((int32_t)w - tw) / 2;
    else if (align == 2)
