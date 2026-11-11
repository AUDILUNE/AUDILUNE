/* SPDX-License-Identifier: GPL-3.0-only */
/* AUDILUNE Block 2: Standalone Full-Screen Visualizer (Bottom HUD hidden).
 * Cycles through 4 styles: 1 OSCILLOSCOPE, 2 SPECTRUM BARS, 3 MOON LOOP, 4 STARFIELD CASCADE. */
#define VIS_N 4u
static const char *const VIS_NAME[ 4 ] = {"OSCILLOSCOPE", "SPECTRUM BARS", "MOON LOOP", "STARFIELD CASCADE"};

#define VIS_FFT 512u
#define VIS_STARS 48u
static int16_t vis_re[ 512 ] __attribute__((section(".pool"))), vis_im[ 512 ] __attribute__((section(".pool")));
#include "scratch.h"
#define vis_l (main_scratch.vis[ 0 ])
#define vis_r (main_scratch.vis[ 1 ])
_Static_assert(sizeof main_scratch.vis[ 0 ] == 512u * sizeof(int16_t), "scratch holds snapshot");

static struct {
    int32_t scale, peak;
    int16_t bar[ 32 ], cap[ 32 ], capv[ 32 ];
    int16_t lvl[ 5 ];
    uint8_t kick;
    uint32_t free_q8, last_ms, phase;
    int16_t sx[ 48 ], sy[ 48 ], sz[ 48 ];
    uint8_t stars_init;
} vs;

static const uint8_t VIS_EDGE[ 33 ] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 21, 25, 29, 35,
                                       41, 49, 58, 69, 82, 98, 116, 138, 164, 195, 232};

static int vis_shown(void) { return vis_on && !ui.home && !ui.menu && cur_page()->scope == SC_TRK; }
static void vis_open(void)
{
    vis_on = 1;
    vis_name_t = 30;
    ui.force = 1;
}
static int32_t isin(uint32_t a) { return SINE[a & 1023u]; }
static int32_t icos(uint32_t a) { return SINE[(a + 256u) & 1023u]; }

static int32_t vis_lvl(int32_t a)
{
    int32_t lg = 0, v;
    if (a < 0) a = -a;
    if (a < 16) return 0;
    while ((a >> lg) > 1) lg++;
    v = lg * 8 + (((a << 3) >> lg) & 7);
    return clamp((v - 56) * 1000 / 64, 0, 1000);
}

static void vis_ellipse(int32_t cx, int32_t cy, int32_t rx, int32_t ry, uint16_t c)
{
    int32_t y;
    if (rx < 1 || ry < 1) return;
    for (y = -ry; y <= ry; y++) {
        int32_t w = 0;
        while (w <= rx && (w * w) * (ry * ry) + (y * y) * (rx * rx) <= (rx * rx) * (ry * ry))
            w++;
        if (w)
            cv_rect(cx - w + 1, cy + y, 2 * w - 1, 1, c);
    }
}

static void vis_circle(int32_t cx, int32_t cy, int32_t r, uint16_t c)
{
    int32_t x = r, y = 0, e = 1 - r;
    while (x >= y) {
        cv_pset(cx + x, cy + y, c), cv_pset(cx - x, cy + y, c), cv_pset(cx + x, cy - y, c), cv_pset(cx - x, cy - y, c);
        cv_pset(cx + y, cy + x, c), cv_pset(cx - y, cy + x, c), cv_pset(cx + y, cy - x, c), cv_pset(cx - y, cy - x, c);
        y++;
        if (e < 0) e += 2 * y + 1;
        else e += 2 * (y - --x) + 1;
    }
}

static void vis_thick(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    cv_line(x0, y0, x1, y1, c);
    cv_line(x0, y0 + 1, x1, y1 + 1, c);
}

static uint32_t vis_beat_q8(void)
{
    if (song.playing)
        return clk_beat * 256u + clk_pos / (BEAT_U / 256u);
    return vs.free_q8;
}

static void vis_fft(void)
{
    uint32_t i, j = 0, bit, len;
    for (i = 0; i < 512u; i++) {
        int32_t w = (32768 - icos(i * 2u)) >> 1;
        vis_re[i] = (int16_t)((vis_l[i] * w) >> 15);
        vis_im[i] = 0;
    }
    for (i = 1; i < 512u; i++) {
        for (bit = 256u; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            int16_t t = vis_re[i]; vis_re[i] = vis_re[j]; vis_re[j] = t;
        }
    }
    for (len = 2; len <= 512u; len <<= 1) {
        uint32_t half = len >> 1, k;
        for (i = 0; i < 512u; i += len)
            for (k = 0; k < half; k++) {
                uint32_t a = i + k, b = a + half, ix = k * (1024u / len);
                int32_t wr = icos(ix), wi = -isin(ix);
                int32_t tr = (vis_re[b] * wr - vis_im[b] * wi) >> 15, ti = (vis_re[b] * wi + vis_im[b] * wr) >> 15;
                int32_t ar = vis_re[a], ai = vis_im[a];
                vis_re[b] = (int16_t)((ar - tr) >> 1);
                vis_im[b] = (int16_t)((ai - ti) >> 1);
                vis_re[a] = (int16_t)((ar + tr) >> 1);
                vis_im[a] = (int16_t)((ai + ti) >> 1);
            }
    }
}

static void vis_bands(int16_t *out)
{
    uint32_t k, i;
    for (k = 0; k < 32u; k++) {
        int32_t m = 0, lg = 0, v;
        for (i = VIS_EDGE[k]; i < VIS_EDGE[k + 1] && i < 256u; i++) {
            int32_t a = vis_re[i] < 0 ? -vis_re[i] : vis_re[i], b = vis_im[i] < 0 ? -vis_im[i] : vis_im[i];
            int32_t mag = a > b ? a + b / 2 : b + a / 2;
            if (mag > m) m = mag;
        }
        if (m < 2) { out[k] = 0; continue; }
        while ((m >> lg) > 1) lg++;
        v = lg * 8 + (((m << 3) >> lg) & 7);
        out[k] = (int16_t)clamp((v - 24) * 1000 / 80, 0, 1000);
    }
}
static void vis_stars_reset(void)
{
    uint32_t i, seed = 0x5A17C3u;
    for (i = 0; i < 48u; i++) {
        seed = seed * 1664525u + 1013904223u;
        vs.sx[i] = (int16_t)(((int32_t)(seed >> 16) - 32768) / 280);
        seed = seed * 1664525u + 1013904223u;
        vs.sy[i] = (int16_t)(((int32_t)(seed >> 16) - 32768) / 280);
        vs.sz[i] = (int16_t)(16 + (i * 5) % 220);
    }
    vs.stars_init = 1;
}

static void vis_update(void)
{
    uint32_t i, w, now = fm1_ms;
    int32_t pk[ 4 ], mpk = 0;
    w = scope_w;
    for (i = 0; i < 512u; i++) {
        vis_l[i] = scope_buf[(w + i) & (SCOPE_N - 1u)];
        vis_r[i] = scope_bufr[(w + i) & (SCOPE_N - 1u)];
        if (vis_l[i] > mpk) mpk = vis_l[i];
        if (-vis_l[i] > mpk) mpk = -vis_l[i];
    }
    fm1_irq_off();
    for (i = 0; i < NPART; i++) {
        pk[i] = trk[i].peak;
        trk[i].peak = 0;
    }
    pk[ 3 ] = drums.peak;
    drums.peak = 0;
    vis_hit = 0;
    if (vis_kick_hit) {
        vis_kick_hit = 0;
        vs.kick = 255;
    }
    fm1_irq_on();
    for (i = 0; i < 4u; i++)
        vs.lvl[i] = (int16_t)(trk_level(i) && !trk[i].p[P_MUTE] ? vis_lvl(pk[i]) : 0);
    vs.lvl[ 4 ] = (int16_t)vis_lvl(mpk);
    vs.kick = (uint8_t)(vs.kick > 20 ? vs.kick - 20 : 0);
    {
        uint32_t dt = now - vs.last_ms;
        vs.last_ms = now;
        if (dt > 200u) dt = 200u;
        vs.free_q8 += dt * (uint32_t)song.g[G_BPM] * 256u / 60000u;
    }
    vs.phase += 14u + (uint32_t)(vs.lvl[ 4 ] / 40);
    vs.peak = mpk;
    {
        int32_t target = mpk < 1600 ? 1600 : mpk;
        vs.scale += (target - vs.scale) / 5;
    }
    if ((vis_style % 4u) == 1u) {
        int16_t b[ 32 ];
        vis_fft();
        vis_bands(b);
        for (i = 0; i < 32u; i++) {
            int32_t bar = vs.bar[i] - vs.bar[i] * 18 / 100;
            vs.bar[i] = (int16_t)(b[i] > bar ? b[i] : bar);
            if (vs.bar[i] >= vs.cap[i]) {
                vs.cap[i] = vs.bar[i];
                vs.capv[i] = 0;
            } else {
                vs.capv[i] = (int16_t)(vs.capv[i] + 4);
                vs.cap[i] = (int16_t)(vs.cap[i] > vs.capv[i] ? vs.cap[i] - vs.capv[i] : 0);
            }
        }
    } else if ((vis_style % 4u) == 3u) {
        int32_t spd = 4 + vs.lvl[ 4 ] / 65 + vs.kick / 28;
        if (!vs.stars_init)
            vis_stars_reset();
        for (i = 0; i < 48u; i++) {
            vs.sz[i] = (int16_t)(vs.sz[i] - spd);
            if (vs.sz[i] <= 4) {
                vs.sz[i] = 230;
                vs.sx[i] = (int16_t)(((int32_t)((i * 73u + vs.phase) & 255u) - 128) * 9 / 10);
                vs.sy[i] = (int16_t)(((int32_t)((i * 137u + vs.phase * 3u) & 255u) - 128) * 9 / 10);
            }
        }
    }
}

/* Style 1: Oscilloscope */
static void vis_scope(void)
{
    uint32_t i0 = 0, i;
    int32_t py = 120;
    for (i = 1; i + 240u < 512u; i++)
        if (vis_l[i - 1u] < 0 && vis_l[i] >= 0) {
            i0 = i;
            break;
        }
    cv_rect(0, 120, 240, 1, C_LINE);
    for (i = 0; i < 240u; i += 40u)
        cv_rect((int32_t)i, 116, 1, 9, C_LINE);
    for (i = 0; i < 240u; i++) {
        int32_t y = 120 - vis_l[i0 + i] * 100 / (vs.scale ? vs.scale : 1);
        y = clamp(y, 2, 237);
        if (i) {
            cv_line((int32_t)i - 1, py - 1, (int32_t)i, y - 1, C_DIM);
            vis_thick((int32_t)i - 1, py, (int32_t)i, y, C_HI);
        }
        py = y;
    }
}

/* Style 2: Spectrum Bars */
static void vis_spectrum(void)
{
    uint32_t k;
    for (k = 0; k < 32u; k++) {
        int32_t x0 = 4 + (int32_t)k * 7, h = vs.bar[k] * 216 / 1000, y, cy = 234 - vs.cap[k] * 216 / 1000;
        for (y = 234; y > 234 - h; y -= 4)
            cv_rect(x0, y - 2, 5, 3, y < 90 ? C_WHITE : C_ACCENT);
        cv_rect(x0, cy - 1, 5, 2, C_WHITE);
    }
}

/* Style 3: Moon Loop (audio-reactive crescent moon + orbital phase rings) */
static void vis_moon_loop(void)
{
    uint32_t bq = vis_beat_q8(), i;
    int32_t pulse = vs.kick * 14 / 255 + vs.lvl[ 4 ] * 10 / 1000;
    int32_t r_outer = 44 + pulse;
    int32_t orbit_r = 74 + vs.lvl[ 4 ] * 16 / 1000;
    uint32_t moon_a = (bq * 2u + vs.phase) & 1023u;
    int32_t cx = 120 + ((icos(moon_a) * 10) >> 15);
    int32_t cy = 120 + ((isin(moon_a) * 10) >> 15);

    vis_circle(120, 120, orbit_r, C_DIM);
    vis_circle(120, 120, orbit_r + 12 + (vs.lvl[ 4 ] / 80), C_LINE);

    for (i = 0; i < 4u; i++) {
        uint32_t a = (bq * 2u + i * 256u) & 1023u;
        int32_t nx = 120 + ((icos(a) * orbit_r) >> 15);
        int32_t ny = 120 + ((isin(a) * orbit_r) >> 15);
        int32_t nr = 3 + vs.lvl[i] * 5 / 1000;
        vis_ellipse(nx, ny, nr, nr, i == 0u ? C_WHITE : C_ACCENT);
    }

    vis_ellipse(cx, cy, r_outer, r_outer, C_HI);
    vis_ellipse(cx + r_outer * 38 / 100, cy - r_outer * 14 / 100, r_outer * 84 / 100, r_outer * 84 / 100, C_BLACK);
}

/* Style 4: Starfield Cascade */
static void vis_starfield(void)
{
    uint32_t i;
    int32_t warp = 1 + vs.lvl[ 4 ] / 180;
    for (i = 0; i < 48u; i++) {
        int32_t z = vs.sz[i] > 1 ? vs.sz[i] : 1;
        int32_t px = 120 + (vs.sx[i] * 160) / z;
        int32_t py = 120 + (vs.sy[i] * 160) / z;
        int32_t pz = z + 10 * warp;
        int32_t ox = 120 + (vs.sx[i] * 160) / pz;
        int32_t oy = 120 + (vs.sy[i] * 160) / pz;
        uint16_t col = z < 75 ? C_WHITE : z < 150 ? C_ACCENT : C_DIM;
        if ((uint32_t)px < 240u && (uint32_t)py < 240u) {
            cv_line(ox, oy, px, py, col);
            if (z < 85)
                cv_rect(px, py, 2, 2, C_WHITE);
        }
    }
}

/* Full-screen render (Bottom HUD explicitly hidden) */
static void vis_draw(void)
{
    uint32_t pass;
    if (!vis_shown_last || ui.force) {
        vis_shown_last = 1;
        lcd_fill(0, 0, 240, 240, C_BLACK);
    } else if (ui.frame & 1u) {
        return;
    }
    vis_update();
    for (pass = 0; pass < 2u; pass++) {
        cv_begin(240, 120, C_BLACK);
        cv_oy = -(int32_t)(pass * 120u);
        switch (vis_style % 4u) {
        case 0: vis_scope(); break;
        case 1: vis_spectrum(); break;
        case 2: vis_moon_loop(); break;
        default: vis_starfield(); break;
        }
        if (!pass && (vis_name_t || ui.msg_t)) {
            char n[ 8 ];
            const char *s = ui.msg_t ? ui.msg : VIS_NAME[vis_style % 4u];
            cv_rect(0, 0, 240, 20, C_BLACK);
            cv_text(120 - text_w(&FONT_S, s) / 2, 2, &FONT_S, s, C_WHITE);
            if (!ui.msg_t) {
                fmt_int(n, (int32_t)(vis_style % 4u) + 1);
                str_cpy(n + str_len(n), "/4", 4);
                cv_text(4, 2, &FONT_S, n, C_DIM);
            }
        }
        cv_oy = 0;
        cv_blit(0, pass * 120u);
    }
    if (vis_name_t)
        vis_name_t--;
}

static void vis_select(int32_t s)
{
    vis_style = (uint8_t)((vis_style + (s > 0 ? 1u : 3u)) % 4u);
    vis_name_t = 30;
    ui.force = 1;
    settings_later = 1;
}
