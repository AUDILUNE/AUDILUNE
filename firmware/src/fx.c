/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Adapted for AUDILUNE Firmware (64 KB Delay Buffer for 6-Track x 128-Step .pool Fit) */
/* Effects: per-track DIST insert, then sends into three shared buses (chorus, tempo delay, reverb).
 * Stereo dry mix; the chorus and the reverb come back in stereo, the delay in the middle. */
#define DLY_LEN 32768u           /* 0.74 s: 1/4 down to 81 BPM, 1/8 down to 41 BPM (saves 64 KB .pool RAM) */
#define CHO_LEN 2048u
static int16_t dly_buf[DLY_LEN] __attribute__((section(".pool")));
static int16_t cho_buf[CHO_LEN] __attribute__((section(".pool")));
/* the reverb: two input diffusers, then four delay lines mixed by a Hadamard matrix (a feedback delay
 * network: every echo feeds all four, so it thickens instead of ringing like a comb), damped in the
 * loop, one line slowly modulated (no metallic tone on long tails); left and right take different lines */
#define REV_MOD 12               /* samples the modulated line moves (+-) */
static const uint16_t REV_LINE[4] = {1559, 1931, 2389, 2791};   /* 35..63 ms, coprime */
static const uint16_t REV_AP[2] = {556, 441};
static int16_t rev_line[1559 + 1931 + 2389 + 2791 + REV_MOD + 2] __attribute__((section(".lowram")));   /* (the pool is
                                                                      * full; .lowram: the SRAM below the RAM region, app.ld) */
static int16_t rev_ap[556 + 441] __attribute__((section(".pool")));
static struct {
    uint32_t dly_w, cho_w, cho_ph, rev_ph;
    int32_t dly_lp;
    uint16_t line_i[4], ap_i[2];
    int32_t line_lp[4];
} fx;
static volatile uint8_t rev_frz;         /* punch-in FREEZE (punch.c): the reverb holds, no loss, no damping */
static int32_t rev_l[CTL], rev_r[CTL];   /* the reverb's own return of this block (FREEZE plays it) */

/* DIST: low cut -> drive (1x..8x, exponential) -> asymmetric soft clip
 * (a little bias = even harmonics) -> tone low-pass that closes with drive ->
 * make-up gain. State per part (track_t dist_*). */
extern uint8_t al_layer_preset, al_layer_mix;
static void track_dist(track_t *t, int32_t *b, uint32_t n)
{
    int32_t d = t->p[P_DIST], i, g, k, mk, bias = 2400 + (d > 115 ? (d - 115) * 320 : 0), b0;
    if (!d) {
        t->dist_on = 0;
        return;
    }
    if (!t->dist_on) {                                  /* coming on: no stale high-pass state (a thump) */

        t->dist_hp = b[0];
        t->dist_lp1 = t->dist_lp2 = 0;
    }
    g = 4096 + d * d * 2;                                /* Q12: 1x .. ~9x, gentle at first */
    k = 32000 - d * 95;                                  /* tone: transparent at low drive .. ~3 kHz, Q15 */
    mk = 30000 - d * 120;                                /* make-up */
    b0 = softclip(bias);
    for (i = 0; i < (int32_t)n; i++) {
        int32_t x = b[i], y;
        t->dist_hp += (x - t->dist_hp + 64) >> 7;           /* ~55 Hz low cut: keep the bass out of the clipper */
        x = clamp(x - t->dist_hp, -230000, 230000);         /* (x >> 2) * g fits 32 bits; the clip is flat out there */
        y = softclip(MULQ12R(x, g, 2) + bias) - b0;         /* (as (x >> 2) * g >> 10 in 32 bits: no overflow for loud
                                                             * poly; #4: rounded, no step of 4 x the drive) */
        t->dist_lp1 += mulq15(y - t->dist_lp1, k);         /* two poles: tames the fizz */
        t->dist_lp2 += mulq15(t->dist_lp1 - t->dist_lp2, k);
        b[i] = mulq15(t->dist_lp2, mk);

    }
}
/* master: peak limiter in front of the soft clipper, a 35 ms hold then ~46 ms release (2.6), threshold where tanh is still nearly linear,
 * so chords get quieter instead of crushed. SLOOP 2.6: it looks one block ahead (lim_block): the output is the mix
 * CTL samples late (0.73 ms) and the gain glides over each block to what the loudest sample of it and of the next one
 * needs, so a hot kick or bass is already turned down when it comes. Before, the gain caught up in ~4 samples and the
 * first ones went through the tanh knee at up to 4x: a crackle on the beats with the MASTER up (in headphones most) */
#define LIM_T 18000
typedef struct {
    int32_t dl[CTL], dr[CTL];           /* the mix of the last block (the delay) */
    uint32_t w;                         /* its oldest sample (the next out) */
    int32_t env;                        /* the peak the gain fits under LIM_T (LIM_T: none) */
    int32_t g;                          /* the gain at the end of the last block, Q15 (32768: 1) */
    uint32_t hold;                      /* blocks before the release starts (LIM_HOLD after a peak near env) */
} lim_t;
/* (2.6, a whistle on loud basses) the release waits LIM_HOLD blocks after the last peak near the envelope: a held bass
 * (down to ~29 Hz, a period under 35 ms) keeps one gain, instead of the gain dipping and creeping back at every cycle
 * (a sawtooth at the bass's pitch with a corner at every block: tones around 1.4 kHz and up) */
#define LIM_HOLD 48u
#define LIM_REL 11                      /* the release after the hold: ~46 ms (12 before the hold: ~93 ms; the same loudness) */
static lim_t lim_dac = {.env = LIM_T, .g = 32768}, lim_usb = {.env = LIM_T, .g = 32768};
/* l, r: n (<= CTL) samples in; out: the n samples of CTL before, limited (before the knee) */
static void lim_block(lim_t *s, int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t i, w = s->w;
    int32_t pk = 0, a, g0 = s->g, g1, dg, e = s->env;
    for (i = 0; i < CTL; i++) {                         /* the samples going out now and the ones after them (the
                                                         * rest of the delay, then this block): CTL ahead */
        a = s->dl[i] < 0 ? -s->dl[i] : s->dl[i], pk = a > pk ? a : pk;
        a = s->dr[i] < 0 ? -s->dr[i] : s->dr[i], pk = a > pk ? a : pk;
    }
    for (i = 0; i < n; i++) {
        a = l[i] < 0 ? -l[i] : l[i], pk = a > pk ? a : pk;
        a = r[i] < 0 ? -r[i] : r[i], pk = a > pk ? a : pk;
    }
    if ((int64_t)pk * 17 >= (int64_t)e * 16)           /* a peak within ~0.5 dB of the envelope: the hold again */
        s->hold = LIM_HOLD;
    if (s->hold)
        s->hold--;
    else if (e > LIM_T)                                 /* the release (after the hold), a block at a time */
        e -= (int32_t)((((int64_t)(e - LIM_T) * (int32_t)n) >> LIM_REL) + 1);
    if (e < LIM_T)
        e = LIM_T;
    if (pk > e)
        e = pk, s->hold = LIM_HOLD;
    s->env = e;
    g1 = e > LIM_T ? (int32_t)(((uint32_t)LIM_T << 15) / (uint32_t)e) : 32768;
    s->g = g1;
    dg = (g1 - g0) / (int32_t)(n ? n : 1u);             /* (towards 0: the gain never above both ends' bound) */
    for (i = 0; i < n; i++) {
        int32_t xl = s->dl[w], xr = s->dr[w], g = g0 + dg * (int32_t)(i + 1u);
        s->dl[w] = l[i], s->dr[w] = r[i];
        w = (w + 1u) & (CTL - 1u);
        if (i + 1u == n)
            g = g1;
        if (g < 32768) {                                /* (64-bit: |x| may be far above Q15. 2.6 fix: x >> 4 first cut
                                                         * the limited signal to 12 bits, a whistle on loud basses) */
            xl = (int32_t)(((int64_t)xl * g) >> 15);
            xr = (int32_t)(((int64_t)xr * g) >> 15);
        }
        l[i] = xl, r[i] = xr;
    }
    s->w = w;
}

static volatile uint8_t fx_lowcut;     /* settings: 12 dB/oct ~110 Hz for the small speaker */
static uint8_t flt_reso;               /* (2.7.2) menu AUDIO > FILTER RESO: 0..10, the track filter's resonance (fx.c
                                        * tflt_reso); 0 = the clean filter as before. One setting for every track's filter */
static int32_t lc_l1, lc_l2, lc_r1, lc_r2, dc_l, dc_r, dce_l, dce_r;
/* DC blocker (~2 Hz), always on: a leaky integrator of the input (Q6 state) subtracted from it.
 * The >> 12 step keeps its remainder (error feedback, 0..4095) and adds it to the next one, so no
 * part of the step is lost: the state follows the input exactly, down to 0 after the sound stops.
 * (It was rounded, and a rounded step of (x - dc) / 4096 stops moving at |x - dc| < 2048: a
 * constant offset of up to +-31 stayed at the output after silence.) */
static inline int32_t dc_block(int32_t x, int32_t *dc, int32_t *err)
{
    int32_t e = (x << 6) - *dc + *err, d = e >> 12;
    *err = e - (d << 12);
    *dc += d;
    return x - ((*dc + 32) >> 6);
}

static int32_t lce[4];
static inline int32_t lowcut1(int32_t x, int32_t *lc, int32_t *err)   /* x minus its one-pole low-pass */
{
    int32_t e = x - *lc + *err, d = e >> 6;
    *err = e - (d << 6);
    *lc += d;
    return x - *lc;
}

/* the output stage: linear up to KNEE (a clean low end: no tanh harmonics on a loud sine), above it
 * a tanh knee with the same slope at the joint, to full scale */
#define KNEE 16384
static inline int32_t knee(int32_t x)
{
    int32_t a = x < 0 ? -x : x;
    if (a <= KNEE)
        return x;
    a = KNEE + (softclip((a - KNEE) * 2) >> 1);
    return x < 0 ? -a : a;
}

/* the USB audio input at full level (menu USB AUDIO = FULL): the same output stage with its own state,
 * fed as if MASTER were all the way up, so the recording level does not follow the knob. The DAC path
 * (master_pre, lim_dac) is untouched. audio.c sets usb_full_now for a render while the computer records. */
static uint8_t usb_full;                 /* menu USB AUDIO: 0 MASTER (follows the knob), 1 FULL (panel.c lights_word) */
static uint8_t usb_full_now;             /* this render fills usb_out (audio.c) */
static int32_t usb_out[2u * CTL];
/* the visualiser's input (audio.c fills the scope with it): the mix as if MASTER were all the way up, so the
 * picture does not follow the volume knob, even at 0 (2.4). The same soft knee as the output, no limiter state */
static int16_t vis_tap[2u * CTL];
typedef struct { int32_t dc[1 + 1], dce[1 + 1], lc[2 + 2], lce[2 + 2]; } mpre_t;
static mpre_t uo, mo;
static __attribute__((noinline)) void master_pre_st(mpre_t *s, int32_t *l, int32_t *r)
{
    *l = dc_block(*l, s->dc + 0, s->dce + 0);
    *r = dc_block(*r, s->dc + 1, s->dce + 1);
    if (fx_lowcut) {
        *l = lowcut1(*l, s->lc + 0, s->lce + 0);
        *l = lowcut1(*l, s->lc + 1, s->lce + 1);
        *r = lowcut1(*r, s->lc + 2, s->lce + 2);
        *r = lowcut1(*r, s->lc + 3, s->lce + 3);
    }
}
static inline void master_pre_usb(int32_t *l, int32_t *r) { master_pre_st(&uo, l, r); }

/* the output stage before the limiter: the DC blocker, the low cut */
static inline void master_pre(int32_t *l, int32_t *r)
{
    (void)lc_l1; (void)lc_l2; (void)lc_r1; (void)lc_r2; (void)dc_l; (void)dc_r; (void)dce_l; (void)dce_r; (void)lce;
    master_pre_st(&mo, l, r);
    if (0) {
        *l = 0;
        *r = 0;
        *l = 0;
        *r = 0;

    }
}

static uint32_t delay_samples(void)
{
    uint32_t s = dly_samples((uint32_t)song.g[G_DTIME]);
    return s < 16u ? 16u : s >= DLY_LEN ? DLY_LEN - 1u : s;
}
/* process the three buses for one block; sends in, wet out (stereo). The LFOs (chorus, reverb line)
 * are computed per block and ramped: no sine per sample */
#define HALF0(v) ((v) >= 0 ? (v) >> 1 : -(-(v) >> 1))   /* v / 2 towards zero */
static inline int32_t fx_slew(int32_t *st, int32_t target)
{
    int32_t d = target - *st;
    *st += (d >> 3) + (d > 0 ? 1 : (d < 0 ? -1 : 0));
    return *st;
}
static void fx_buses(const int32_t *cho_in, const int32_t *dly_in, const int32_t *rev_in, int32_t *wet_l,
                     int32_t *wet_r, uint32_t n)
{
    static int32_t s_fb, s_col, s_dmix, s_g, s_lpk, s_cdep;
    uint32_t i, k, ti, dl = delay_samples();
    int32_t max_cho_send = 0, max_dly_send = 0, max_rev_send = 0;
    for (ti = 0; ti < NTRK; ti++) {
        if (trk[ti].p[P_CHOR] > max_cho_send)
            max_cho_send = trk[ti].p[P_CHOR];
        if (trk[ti].p[P_DLY] > max_dly_send)
            max_dly_send = trk[ti].p[P_DLY];
        if (trk[ti].p[P_REV] > max_rev_send)
            max_rev_send = trk[ti].p[P_REV];
    }
      int32_t fb = song.g[G_DFDBK] * 230, col = 2000 + song.g[G_DCOLOR] * 240;
    if (thr_dly && fb < 89 * 230)                       /* ECHO THROW: FDBK at least 70 %, gliding with it (2.6) */
        fb += ((89 * 230 - fb) * thr_dly) >> 15;
    if (max_dly_send > 127) {                           /* 101%..110% expansion zone: warm self-oscillating cascade */
        int32_t cascade_fb = 29210 + (max_dly_send - 127) * 320;
        if (fb < cascade_fb)
            fb = cascade_fb;
    }
    fb = fx_slew(&s_fb, fb);
    col = fx_slew(&s_col, col);
    int32_t dmix = fx_slew(&s_dmix, song.g[G_DMIX] * 258);
    int32_t g = 17000 + song.g[G_RSIZE] * 104, lpk = 32767 - song.g[G_RDAMP] * 200;   /* loop gain (RT60 ~0.4..4 s), damping */
    if (max_rev_send > 127) {                           /* 101%..110% shimmer bloom: lush feedback & bright harmonics */
        int32_t shim_g = 30208 + (max_rev_send - 127) * 180;
        if (g < shim_g)
            g = shim_g > 32600 ? 32600 : shim_g;
        lpk += ((32767 - lpk) * (max_rev_send - 127)) / 13;
    }
    g = fx_slew(&s_g, g);
    lpk = fx_slew(&s_lpk, lpk);
    int32_t frnd = rev_frz ? 128 : 0;                  /* FREEZE: what is in the lines stays there (below: no
                                                         * loss, rounded to nearest; 0: as always, bit for bit) */
    int32_t cdepth = fx_slew(&s_cdep, song.g[G_CDEPTH] * 6 + (max_cho_send > 127 ? (max_cho_send - 127) * 28 : 0));
    int32_t ca0, ca1, cb0, cb1, ma, mb, dca, dcb;
    const uint32_t L0 = REV_LINE[0] + REV_MOD + 2u, B1 = L0, B2 = B1 + REV_LINE[1], B3 = B2 + REV_LINE[2];
    {   /* the chorus' two read points (Q8 samples back) and line 0's extra length, at both ends of the block */
        int32_t s0 = osc_sine(fx.cho_ph), s1, m0 = osc_sine(fx.rev_ph), m1;

        fx.cho_ph += LFO_INC[song.g[G_CRATE] & 127];
        fx.rev_ph += LFO_INC[30];
        s1 = osc_sine(fx.cho_ph);
        m1 = osc_sine(fx.rev_ph);
        ca0 = (400 << 8) + ((s0 + 32768) * cdepth >> 8), ca1 = (400 << 8) + ((s1 + 32768) * cdepth >> 8);
        cb0 = (400 << 8) + ((32767 - s0) * cdepth >> 8), cb1 = (400 << 8) + ((32767 - s1) * cdepth >> 8);
        ma = (REV_MOD << 8) + ((m0 * REV_MOD) >> 7), mb = (REV_MOD << 8) + ((m1 * REV_MOD) >> 7);
        dca = (ca1 - ca0) >> CTL_LOG2, dcb = (cb1 - cb0) >> CTL_LOG2;
    }
    for (i = 0; i < n; i++) {
        int32_t yl, yr, x, a, o0, o1, o2, o3;
        /* chorus: two modulated short delays, 5..15 ms, the LFO half a turn apart: left and right move apart */
        cho_buf[fx.cho_w & (CHO_LEN - 1u)] = (int16_t)clamp(cho_in[i] >> 1, -32768, 32767);
        {
            int32_t r0 = ca0 + dca * (int32_t)i, r1 = cb0 + dcb * (int32_t)i;
            uint32_t i0 = (uint32_t)r0 >> 8, i1 = (uint32_t)r1 >> 8;
            int32_t c0 = cho_buf[(fx.cho_w - i0) & (CHO_LEN - 1u)], c1 = cho_buf[(fx.cho_w - i0 - 1u) & (CHO_LEN - 1u)];
            int32_t d0 = cho_buf[(fx.cho_w - i1) & (CHO_LEN - 1u)], d1 = cho_buf[(fx.cho_w - i1 - 1u) & (CHO_LEN - 1u)];

            yl = (c0 + (((c1 - c0) * (r0 & 255)) >> 8)) << 1;
            yr = (d0 + (((d1 - d0) * (r1 & 255)) >> 8)) << 1;
        }
        fx.cho_w++;
        /* delay with a low-passed, soft-limited feedback loop (musical cascade past 100%) */
        x = dly_buf[(fx.dly_w - dl) & (DLY_LEN - 1u)];
        fx.dly_lp += mulq15(x - fx.dly_lp, col);
        {
            int32_t dly_sum = (dly_in[i] >> 1) + mulq15(fx.dly_lp, fb);
            if (fb > 29210)
                dly_sum = softclip(dly_sum);
            dly_buf[fx.dly_w & (DLY_LEN - 1u)] = (int16_t)clamp(dly_sum, -32768, 32767);
        }
        fx.dly_w++;
        x = mulq15(x << 1, dmix);

        yl += x;
        yr += x;
        /* reverb: two diffusers, then the four lines */
        a = mulq15(rev_in[i], 13000);
        {
            int16_t *c = rev_ap;
            for (k = 0; k < 2u; k++) {
                int32_t b = c[fx.ap_i[k]], v = a + (b >> 1);
                c[fx.ap_i[k]] = (int16_t)clamp(v, -32768, 32767);
                a = b - (v >> 1);
                if (++fx.ap_i[k] >= REV_AP[k])
                    fx.ap_i[k] = 0;
                c += REV_AP[k];
            }
        }
        {
            int16_t *c = rev_line;
            int32_t s0, s1, d0, d1, r = ma + (((mb - ma) * (int32_t)i) >> CTL_LOG2);   /* (between the two: never below 0) */
            uint32_t ri = fx.line_i[0] + ((uint32_t)r >> 8), rj;
            if (ri >= L0)                                   /* (the oldest sample is at line_i: reading */
                ri -= L0;                                   /* past it shortens line 0 by 0..2 REV_MOD) */
            rj = ri + 1u >= L0 ? 0u : ri + 1u;
            o0 = c[ri] + (((c[rj] - c[ri]) * (r & 255) + frnd) >> 8);
            o1 = c[B1 + fx.line_i[1]];
            o2 = c[B2 + fx.line_i[2]];
            o3 = c[B3 + fx.line_i[3]];
            s0 = o0 + o1, d0 = o0 - o1, s1 = o2 + o3, d1 = o2 - o3;   /* Hadamard / 2: each feeds all four */
            if (!frnd) {
                fx.line_lp[0] += mulq15(((s0 + s1) >> 1) - fx.line_lp[0], lpk);
                fx.line_lp[1] += mulq15(((d0 + d1) >> 1) - fx.line_lp[1], lpk);
                fx.line_lp[2] += mulq15(((s0 - s1) >> 1) - fx.line_lp[2], lpk);
                fx.line_lp[3] += mulq15(((d0 - d1) >> 1) - fx.line_lp[3], lpk);
                c[fx.line_i[0]] = (int16_t)clamp(mulq15(fx.line_lp[0], g) + a, -32768, 32767);
                c[B1 + fx.line_i[1]] = (int16_t)clamp(mulq15(fx.line_lp[1], g) - a, -32768, 32767);
                c[B2 + fx.line_i[2]] = (int16_t)clamp(mulq15(fx.line_lp[2], g) + a, -32768, 32767);
                c[B3 + fx.line_i[3]] = (int16_t)clamp(mulq15(fx.line_lp[3], g) - a, -32768, 32767);
            } else {                                    /* FREEZE: a loop gain of exactly 1, so no rounding that
                                                         * leans one way (a DC that would grow while held): the
                                                         * halves towards zero, no damping or gain multiply */
                fx.line_lp[0] = HALF0(s0 + s1), fx.line_lp[1] = HALF0(d0 + d1);
                fx.line_lp[2] = HALF0(s0 - s1), fx.line_lp[3] = HALF0(d0 - d1);
                c[fx.line_i[0]] = (int16_t)clamp(fx.line_lp[0] + a, -32768, 32767);
                c[B1 + fx.line_i[1]] = (int16_t)clamp(fx.line_lp[1] - a, -32768, 32767);
                c[B2 + fx.line_i[2]] = (int16_t)clamp(fx.line_lp[2] + a, -32768, 32767);
                c[B3 + fx.line_i[3]] = (int16_t)clamp(fx.line_lp[3] - a, -32768, 32767);
            }
            if (++fx.line_i[0] >= L0) fx.line_i[0] = 0;
            if (++fx.line_i[1] >= REV_LINE[1]) fx.line_i[1] = 0;
            if (++fx.line_i[2] >= REV_LINE[2]) fx.line_i[2] = 0;
            if (++fx.line_i[3] >= REV_LINE[3]) fx.line_i[3] = 0;
        }
        wet_l[i] = yl + o0 + o2;
        wet_r[i] = yr + o1 - o3;
        rev_l[i] = o0 + o2;
        rev_r[i] = o1 - o3;
    }
}
/* one block of the whole mix (shared with tests/hostsim.c): events -> each part
 * -> dist -> SLICER -> level / pan / sends -> drums (-> SLICER) -> buses -> master; out: stereo Q15. */
static void events_block(uint32_t n);                    /* seq.c */
typedef struct { int32_t l[CTL], r[CTL], sc[CTL], sd[CTL], sr[CTL]; } mixacc_t;   /* dry L, R; chorus, delay, reverb sends */
static mixacc_t acc[2] __attribute__((section(".lowram")));          /* core 0's, core 1's (.lowram: app.ld, main.c) */
static int32_t wet_l[CTL], wet_r[CTL];
static int32_t part_buf[2][CTL] __attribute__((section(".lowram")));   /* a part's scratch, one per core */
static int32_t drum_buf[4][CTL] __attribute__((section(".lowram")));   /* the drums through the drum track's FILTER: L, R,
                                                                        * reverb, delay */

/* ---- mute / solo: a track that goes silent fades out over ~6 ms (and back in) */
#define MUTE_STEP 4096                                  /* Q15 per block: 8 blocks */
static int32_t gain_next(track_t *t)                    /* the track's mute gain at the end of this block */
{
    int32_t to = trk_silent(t) ? 32767 : 0, a = t->att;
    t->att = a < to ? (to - a > MUTE_STEP ? a + MUTE_STEP : to) : (a - to > MUTE_STEP ? a - MUTE_STEP : to);
    return 32767 - t->att;
}

/* ---- DUCK: every kick (the drum track's KICK / KICK 2) dips the synth parts, which come back over an
 * eighth note: depth G_DUCK, the curve (1 - t / T)^2. drum_on sets drums.kick. */
static struct {
    uint32_t t;                                         /* units since the kick */
    int32_t g0, g1;                                     /* the parts' gain at the block start, end (Q15) */
} duck = {0xFFFFFFFFu, 32767, 32767};

static void duck_block(uint32_t adv)
{
    int32_t depth = song.g[G_DUCK] * 258, x;
    uint32_t len = BEAT_U / 2u;
    duck.g0 = duck.g1;
    if (drums.kick) {
        drums.kick = 0;
        duck.t = 0;
    }
    if (!depth || duck.t >= len) {
        duck.g1 = 32767;
        return;
    }
    x = 32767 - (int32_t)((duck.t << 10) / (len >> 5));      /* 1 - t / T, Q15 (t < len < 2^21) */
    if (x < 0)
        x = 0;
    duck.g1 = 32767 - mulq15(depth, mulq15(x, x));
    duck.t = duck.t + adv < duck.t ? 0xFFFFFFFFu : duck.t + adv;
}

/* ---- the DJ filter: v < 0 a low-pass closing, > 0 a high-pass opening, 0 off. */
typedef struct {
    int32_t cut;                                        /* now, 0..127 << 8 (CUTOFF_HZ index) */
    int8_t mode;                                        /* -1 LP, 1 HP, 0 off */
    uint8_t seed;                                       /* (2.6) bit per channel: the LP's state from its next sample */
    int32_t z[4][2];                                    /* the SVF states, per channel */
} djf_t;
static djf_t djf, tflt[NTRK];

static int32_t tflt_reso(void) { return 40 + (int32_t)(flt_reso > 10u ? 10u : flt_reso) * 7; }
static int djf_block(djf_t *f, int32_t v, tsvf_t *c, int32_t reso)
{
    int32_t to;
    if (v < 0 && f->mode >= 0) {                        /* (switching side: from open) */
        f->mode = -1;
        f->cut = 127 << 8;
        memset(f->z, 0, sizeof f->z);
        f->seed = 15;                                   /* (2.6) the low-pass starts where the signal is: from rest
                                                         * its output began at 0 under a sounding note (a click) */
    } else if (v > 0 && f->mode <= 0) {
        f->mode = 1;
        f->cut = 0;
        memset(f->z, 0, sizeof f->z);
    }
    if (!f->mode)
        return 0;
    to = f->mode < 0 ? (v < 0 ? (127 << 8) + v * 90 * 4 : 127 << 8) : (v > 0 ? v * 90 * 4 : 0);
    f->cut += clamp(to - f->cut, -384, 384);            /* ~1.5 index a block */
    if (!v && f->cut == to) {
        f->mode = 0;                                    /* fully open again: off */
        return 0;
    }
    tsvf_coef(c, f->cut, reso);
    return 1;
}
static void djf_run(djf_t *f, const tsvf_t *c, int32_t *b, uint32_t n, uint32_t ch)
{
    uint32_t i;
    int32_t *z = f->z[ch];
    if ((f->seed >> ch) & 1u) {
        f->seed &= (uint8_t)~(1u << ch);
        if (f->mode < 0 && n) {
            z[0] = 0;                                   /* at rest on this input: the output is it (DC: exact) */
            z[1] = clamp(b[0], -140000, 140000);
        }
    }
    for (i = 0; i < n; i++) {
        int32_t x = clamp(b[i], -140000, 140000), y = tsvf_lp(c, x, &z[0], &z[1]);
        b[i] = f->mode < 0 ? y : x - y;
    }
}
static void tflt_run(djf_t *f, const tsvf_t *c, int32_t *b, uint32_t n, uint32_t ch)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        b[i] >>= 2;
    djf_run(f, c, b, n, ch);
    for (i = 0; i < n; i++)
        b[i] <<= 2;
}
/* INSERT (SLOOP 2.6, after Felucca 1.5 by Leo Kuroshita): one effect per track, after DIST and before SLICER */
enum { IT_OFF, IT_SOFT, IT_HARD, IT_FOLD, IT_FUZZ, IT_CRUSH, IT_PHASER, IT_FLANGER, IT_CHORUS, IT_N };
_Static_assert(IT_N == sizeof N_ITYPE / sizeof N_ITYPE[0], "the INSERT's types as params.c names them");
#define IL_LEN 256u                     /* FLANG / CHOR: the line, samples (a power of 2: masked) */
#define PH_ST 4u                        /* PHASR: all-pass stages (two notches, the classic pedal's) */
static const uint8_t INS_HOLD[16] = {64, 48, 40, 32, 24, 20, 16, 12, 10, 8, 6, 5, 4, 3, 2, 1};   /* CRUSH RATE */
static const int16_t PH_A[33] = {
    -16038, -15998, -15955, -15906, -15853, -15793, -15727, -15653, -15572, -15481, -15381, -15270, -15147, -15012,
    -14862, -14696, -14514, -14313, -14092, -13849, -13582, -13290, -12970, -12621, -12240, -11825, -11375, -10886,
    -10356, -9784, -9166, -8500, -7783,
};
typedef struct {
    int32_t w;                          /* the wet share now, Q15 */
    uint8_t type;                       /* the type running */
    uint8_t cnt;                        /* CRUSH: samples left of the held one */
    uint16_t wp;                        /* FLANG / CHOR: the line's next write */
    uint32_t ph;                        /* the sweep's phase */
    int32_t hp, lp1, lp2, dc, hold;     /* low cut (Q8), the two low-passes, DC blocker (Q8), CRUSH's held sample */
    int32_t px[PH_ST], py[PH_ST], fb;   /* PHASR: each stage's last input and output, the fed back */
    int16_t line[IL_LEN];               /* FLANG / CHOR, at a quarter (headroom) */
} ins_t;
static ins_t ins[NPART + 4u];                    /* the parts; the drum track's L, R, REV, DLY */

static inline int32_t ins_mixq(int32_t m) { return m >= 127 ? 32768 : m <= 0 ? 0 : m * 258; }   /* MIX -> Q15 */
static inline int32_t ins_mix(int32_t x, int32_t y, int32_t w) { return w == 32768 ? y : x + mulq16(y - x, (uint32_t)w << 1); }
static inline uint32_t ins_tri(uint32_t q) { uint32_t t = q >> 16; return t < 32768u ? t << 1 : (65535u - t) << 1; }
static int32_t ins_lpk(int32_t c)
{
    uint32_t g = SVF_G[clamp(c, 0, 127)];
    return (int32_t)((g << 16) / (4096u + g));
}
static inline int32_t ins_shape(uint32_t ty, int32_t v, uint32_t sh)
{
    switch (ty) {
    case IT_SOFT:
        return softclip(v);
    case IT_HARD:
        return clamp(v, -24000, 24000);
    case IT_FOLD: {
        int32_t u = (int32_t)((uint32_t)(v + 16384) & 0xFFFFu) - 32768;
        u = 16384 - (u < 0 ? -u : u);
        return u + (u >> 1);
    }
    case IT_FUZZ:
        return v > 0 ? (v > 20000 ? 20000 : v) : softclip(v) >> 1;
    case IT_CRUSH:
        return sh ? ((v + (1 << (sh - 1u))) >> sh) << sh : v;
    default:
        return v;
    }
}
static inline uint32_t ins_bits_sh(int32_t a) { return 15u - (uint32_t)(clamp(a, 0, 127) >> 3); }

static __attribute__((noinline)) void ins_drive(ins_t *s, const int16_t *p, int32_t *b, uint32_t n, int32_t tw)
{
    uint32_t i, ty = s->type, sh = ins_bits_sh(p[P_IA]);
    uint32_t hold = INS_HOLD[clamp(p[P_IB], 0, 127) >> 3];
    int32_t a = clamp(p[P_IA], 0, 127), g = 4096 + a * a * 3;
    int32_t w = s->w, lp1 = s->lp1, lp2 = s->lp2, x, y, v;
    if (ty == IT_CRUSH) {
        uint32_t k = (uint32_t)ins_lpk(p[P_IC]), cnt = s->cnt;
        int32_t hd = s->hold;
        for (i = 0; i < n; i++) {
            x = b[i];
            w += clamp(tw - w, -SL_SLOPE, SL_SLOPE);
            if (!cnt) {
                hd = ins_shape(IT_CRUSH, clamp(x, -262144, 262143), sh);
                cnt = hold;
            }
            cnt--;
            y = hd << 4;
            v = mulq16(y - lp1, k);
            y = v + lp1;
            lp1 = y + v;
            v = mulq16(y - lp2, k);
            y = v + lp2;
            lp2 = y + v;
            b[i] = ins_mix(x, (y + 8) >> 4, w);
        }
        s->cnt = (uint8_t)cnt;
        s->hold = hd;
    } else {
        uint32_t k = (uint32_t)ins_lpk(p[P_IB]);
        int32_t lvl = LEVEL_Q12[p[P_IC] & 127], hp = s->hp, dc = s->dc;
        for (i = 0; i < n; i++) {
            x = b[i];
            w += clamp(tw - w, -SL_SLOPE, SL_SLOPE);
            hp += ((x << 8) - hp) >> 8;
            v = clamp(x - (hp >> 8), -230000, 230000);
            y = ins_shape(ty, ((v >> 3) * g) >> 9, 0) << 4;
            v = mulq16(y - lp1, k);
            y = v + lp1;
            lp1 = y + v;
            v = mulq16(y - lp2, k);
            y = v + lp2;
            lp2 = y + v;
            y = (((y + 8) >> 4) * lvl) >> 12;
            dc += ((y << 8) - dc) >> 9;
            b[i] = ins_mix(x, y - (dc >> 8), w);
        }
        s->hp = hp;
        s->dc = dc;
    }
    s->w = w;
    s->lp1 = lp1;
    s->lp2 = lp2;
}
static __attribute__((noinline)) void ins_swept(ins_t *s, const int16_t *p, int32_t *b, uint32_t n, int32_t tw)
{
    uint32_t i, inc = LFO_INC[clamp(p[P_IA], 0, 127)] / CTL, dep = (uint32_t)clamp(p[P_IB], 0, 127);
    int32_t c = clamp(p[P_IC], 0, 127), w = s->w, dc = s->dc;
    if (s->type == IT_PHASER) {
        uint32_t tr = ins_tri(s->ph + (n >> 1) * inc), q = 32768u + (uint32_t)(((int32_t)tr - 32768) * (int32_t)dep / 127);
        int32_t a = PH_A[q >> 11] + (((PH_A[(q >> 11) + 1u] - PH_A[q >> 11]) * (int32_t)(q & 2047u)) >> 11);
        int32_t fbk = c * 180, fb = s->fb;
        int32_t x0 = s->px[0], x1 = s->px[1], x2 = s->px[2], x3 = s->px[3];
        int32_t y0 = s->py[0], y1 = s->py[1], y2 = s->py[2], y3 = s->py[3];
        _Static_assert(PH_ST == 4u, "ins_swept: four all-pass stages");
        for (i = 0; i < n; i++) {
            int32_t x = b[i], u = clamp((x + 4) >> 3, -16383, 16383) + ((fb * fbk + 16384) >> 15), y;
            w += clamp(tw - w, -SL_SLOPE, SL_SLOPE);
            y = clamp(x0 + ((a * (u - y0) + 8192) >> 14), -65535, 65535), x0 = u, y0 = y, u = y;
            y = clamp(x1 + ((a * (u - y1) + 8192) >> 14), -65535, 65535), x1 = u, y1 = y, u = y;
            y = clamp(x2 + ((a * (u - y2) + 8192) >> 14), -65535, 65535), x2 = u, y2 = y, u = y;
            y = clamp(x3 + ((a * (u - y3) + 8192) >> 14), -65535, 65535), x3 = u, y3 = y;
            fb = clamp(y, -32767, 32767);
            y <<= 2;
            dc += ((y << 8) - dc) >> 9;
            b[i] = ins_mix(x, (x >> 1) + y - (dc >> 8), w);
        }
        s->px[0] = x0, s->px[1] = x1, s->px[2] = x2, s->px[3] = x3;
        s->py[0] = y0, s->py[1] = y1, s->py[2] = y2, s->py[3] = y3;
        s->fb = fb;
        s->ph += n * inc;
    } else {
        int32_t chorus = s->type == IT_CHORUS, fbk = c * (chorus ? 103 : 219);
        uint32_t dq = dep * 516u, ph = s->ph, wp = s->wp;
        int16_t *line = s->line;
        for (i = 0; i < n; i++, ph += inc) {
            int32_t x = b[i], r0, r1, v, fr;
            uint32_t d;
            w += clamp(tw - w, -SL_SLOPE, SL_SLOPE);
            if (chorus) {
                d = (uint32_t)((140 << 16) + ((osc_sine(ph) * (int32_t)dep * 50) >> 5));
            } else {
                uint32_t tr = ins_tri(ph), sw = (tr * tr) >> 16;
                d = (11u << 16) + 209u * (((sw >> 1) * dq) >> 15);
            }
            r0 = (int32_t)(wp - (d >> 16)) & (int32_t)(IL_LEN - 1u);
            r1 = (r0 - 1) & (int32_t)(IL_LEN - 1u);
            fr = (int32_t)((d >> 1) & 0x7FFFu);
            v = line[r0] + (((line[r1] - line[r0]) * fr) >> 15);
            line[wp] = (int16_t)clamp((x >> 2) + softclip(mulq15(v, fbk)), -32767, 32767);
            wp = (wp + 1u) & (IL_LEN - 1u);
            v <<= 1;
            dc += ((v << 8) - dc) >> 9;
            b[i] = ins_mix(x, (x >> 1) + v - (dc >> 8), w);
        }
        s->ph = ph;
        s->wp = (uint16_t)wp;
    }
    s->w = w;
    s->dc = dc;
}

static __attribute__((noinline)) int ins_run(ins_t *s, const int16_t *p, int32_t *b, uint32_t n)
{
    int32_t ty = clamp(p[P_ITYPE], 0, IT_N - 1), tw;
    if (s->type != ty && !s->w)
        s->type = (uint8_t)ty;
    tw = s->type == ty && ty ? ins_mixq(p[P_IMIX]) : 0;
    if (!s->w) {
        if (!tw)
            return 0;
        memset(s, 0, sizeof *s);
        s->type = (uint8_t)ty;
    }
    if (s->type >= IT_PHASER)
        ins_swept(s, p, b, n, tw);
    else
        ins_drive(s, p, b, n, tw);
    return s->w != 0;
}
static void track_insert(track_t *t, int32_t *b, uint32_t n)
{
    t->ins_run = (uint8_t)ins_run(&ins[(uint32_t)(t - trk) % NPART], t->p, b, n);
}
static __attribute__((noinline)) void ins_rest(track_t *t)
{
    memset(&ins[(uint32_t)(t - trk) % NPART], 0, sizeof(ins_t));
    t->ins_run = 0;
}

static void mix_part_run(track_t *t, uint32_t n, mixacc_t *mx, int32_t *b);
static void mix_part(track_t *t, uint32_t n, mixacc_t *mx, int32_t *b)
{
    static int16_t pnew[2][P_COUNT] __attribute__((section(".lowram")));
    int16_t *pn = pnew[mx != &acc[0]];
    if (!(t->pold && t->xf_on && t->xf)) {
        mix_part_run(t, n, mx, b);
        return;
    }
    memcpy(pn, t->p, sizeof pnew[0]);
    memcpy(t->p, t->p_old, sizeof t->p);
    mix_part_run(t, n, mx, b);
    memcpy(t->p, pn, sizeof pnew[0]);
}
static void mix_part_run(track_t *t, uint32_t n, mixacc_t *mx, int32_t *b)
{
    uint32_t i;
    int32_t g0 = 32767 - t->att, g1 = gain_next(t);
    if (track_render(t, b, n))
        t->tail = t->p[P_ITYPE] ? 64 : 16, t->ins_xt = 0;
    else if ((!t->tail || !(t->p[P_DIST] | t->p[P_ITYPE]) || !--t->tail) && !slicer_busy(t) &&
             !(t->ins_run && t->ins_res > 16u && t->ins_xt < 255u && ++t->ins_xt)) {
        slicer_track(t, 0, n);
        if (t->ins_run && !t->p[P_ITYPE])
            ins_rest(t);
        return;
    }
    if (!g0 && !g1) {
        slicer_track(t, 0, n);
        return;
    }
    {
        int32_t lvl = expr_gain(LEVEL_Q12[t->p[P_LEVEL] ? clamp(t->p[P_LEVEL] + t->p[P_ED_FX], 1, 127) : 0], (uint32_t)(t - trk)), pan = t->p[P_PAN];
        int32_t gl = 4096 - (pan > 0 ? pan * 64 : 0), gr = 4096 + (pan < 0 ? pan * 64 : 0);
        int32_t c = clamp(t->p[P_CHOR] * 258, 0, 32767), d = send_thr(clamp(t->p[P_DLY] * 258, 0, 32767), thr_dly), r = send_thr(clamp(t->p[P_REV] * 258, 0, 32767), thr_rev);
        int32_t pk = t->peak;
        int32_t xmax = c > d ? c : d;
        int32_t ga = mulq15(g0, duck.g0), gb = mulq15(g1, duck.g1);
        xmax = 0x7FFFFFFF / ((xmax > r ? xmax : r) | 1);
        track_dist(t, b, n);
        if (t->p[P_ITYPE] | t->ins_run) {
            track_insert(t, b, n);
            t->ins_res = (uint16_t)(b[n - 1u] < 0 ? (-b[n - 1u] > 65535 ? 65535 : -b[n - 1u])
                                                  : (b[n - 1u] > 65535 ? 65535 : b[n - 1u]));
        }
        slicer_track(t, b, n);
        {
            tsvf_t fc;
            djf_t *f = &tflt[(uint32_t)(t - trk) % NTRK];
            if (djf_block(f, t->p[P_TFLT], &fc, tflt_reso()))
                tflt_run(f, &fc, b, n, 0);

        }
        {   /* AUDILUNE Priority #1: 3D Spatial Orbit + 32-Sample ITD Haas Micro-Delay + Head-Shadow (.pool) */
            static int16_t al_itd_ring[NPART][32] __attribute__((section(".pool")));
            static int32_t al_hs_lp[NPART] __attribute__((section(".pool")));
            static uint8_t al_itd_w[NPART] __attribute__((section(".pool")));
            uint32_t ti = (uint32_t)(t - trk) % NPART;
            static int8_t al_syntropy[NTRK] __attribute__((section(".pool")));
            static uint8_t al_syntropy_scope __attribute__((section(".pool")));
            static uint16_t al_syntropy_decay[NTRK] __attribute__((section(".pool")));
            static uint32_t al_syn_ph[NPART] __attribute__((section(".pool")));
            static int32_t al_coal_lp[NPART] __attribute__((section(".pool")));
            int32_t syn = clamp((int32_t)al_syntropy[ti], -100, 100);
            int poly_stage = (smart_voice_mode(t) == V_POLY && t->p[P_TRANS] > -12);
            int32_t orb_pan = poly_stage ? ((int32_t)al_trk_orbit_pan[ti] >> 2) : 0;
            int32_t eff_pan = clamp((int32_t)t->p[P_PAN] + orb_pan, -64, 63);
            int32_t lvl0, dl, gl0, dgl, gr0, dgr, abs_p, syn_rev_add = 0;
            uint32_t itd_d;
            if (syn < 0) {
                int32_t dis = -syn;
                uint32_t dec = al_syntropy_decay[ti] + (uint32_t)(dis >> 2) + 1u;
                al_syntropy_decay[ti] = (uint16_t)(dec > 32767u ? 32767u : dec);
                int32_t scat = (((int32_t)((al_syn_ph[ti] >> 12) & 127u) - 64) * dis) / 100;
                eff_pan = clamp(eff_pan + scat, -64, 63);
                syn_rev_add = (dis * 240);
            } else {
                if (al_syntropy_decay[ti] > 0u) {
                    uint32_t step_back = 128u + (uint32_t)(syn * 8);
                    al_syntropy_decay[ti] = al_syntropy_decay[ti] > step_back ? (uint16_t)(al_syntropy_decay[ti] - step_back) : 0u;
                }
                if (syn > 0) {
                    eff_pan = (eff_pan * (100 - syn)) / 100;
                    lvl = (lvl * (4096 - syn * 6)) >> 12;
                }
            }
            abs_p = eff_pan < 0 ? -eff_pan : eff_pan;
            itd_d = poly_stage && abs_p > 4 ? (uint32_t)((abs_p * 28) >> 6) : 0u;
            gl = 4096 - (eff_pan > 0 ? eff_pan * 64 : 0);
            gr = 4096 + (eff_pan < 0 ? eff_pan * 64 : 0);
            if (!t->mix_on)
                t->lvl = lvl, t->gpl = gl, t->gpr = gr, t->mix_on = 1;
            lvl0 = t->lvl, gl0 = t->gpl, gr0 = t->gpr;
            t->lvl = mix_glide(lvl0, lvl), t->gpl = mix_glide(gl0, gl), t->gpr = mix_glide(gr0, gr);
            dl = (t->lvl - lvl0) >> CTL_LOG2, dgl = t->gpl - gl0, dgr = t->gpr - gr0;
            for (i = 0; i < n; i++) {
                int32_t lv = lvl0 + dl * (int32_t)i, bi = b[i], x = MULQ12R(bi, lv, 2), a;
                int32_t xs, g = ga + (((gb - ga) * (int32_t)i) >> CTL_LOG2), xl = x, xr = x;
                if (syn < 0) {
                    int32_t dis = -syn;
                    uint32_t rate = 380000u + (uint32_t)dis * 18000u;
                    uint32_t ph = al_syn_ph[ti] += rate;
                    uint32_t duty = (uint32_t)(65535 - dis * 520);
                    uint32_t win = (ph >> 16) < duty ? 32767u : 0u;
                    int32_t survive = 32767 - (int32_t)al_syntropy_decay[ti];
                    int32_t gg = (int32_t)((win * (uint32_t)(survive > 0 ? survive : 0)) >> 15);
                    if (syn_rev_add)
                        mx->sr[i] += mulq15(clamp(x, -32767, 32767), syn_rev_add);
                    x = mulq15(x, gg);
                    xl = xr = x;
                } else if (syn > 0) {
                    al_coal_lp[ti] += (x - al_coal_lp[ti]) >> 5;
                    int32_t core = al_coal_lp[ti] + ((x - al_coal_lp[ti]) >> 1);
                    x += mulq15(core - x, syn * 180);
                    xl = xr = x;
                }
                if (g < 32767) {
                    g >>= 3;
                    x = MULQ12R(x, g, 4);
                    xl = xr = x;
                }
                a = x < 0 ? -x : x;
                xs = clamp(x, -xmax, xmax);
                if (a > pk)
                    pk = a;
                if (c)
                    mx->sc[i] += mulq15(xs, c);
                if (d)
                    mx->sd[i] += mulq15(xs, d);
                if (r)
                    mx->sr[i] += mulq15(xs, r);
                if (itd_d) {
                    uint8_t w = al_itd_w[ti];
                    int32_t del = (int32_t)al_itd_ring[ti][(w - itd_d) & 31u] << 2;
                    al_itd_ring[ti][w & 31u] = (int16_t)clamp(x >> 2, -32767, 32767);
                    al_itd_w[ti] = (uint8_t)((w + 1u) & 31u);
                    al_hs_lp[ti] += (del - al_hs_lp[ti]) >> 1;
                    del = al_hs_lp[ti] + ((del - al_hs_lp[ti]) >> 1);
                    if (eff_pan < 0)
                        xr = del;
                    else
                        xl = del;
                }
                gl = gl0 + ((dgl * (int32_t)i) >> CTL_LOG2), gr = gr0 + ((dgr * (int32_t)i) >> CTL_LOG2);
                mx->l[i] += (xl * (gl >> 4) + 128) >> 8;
                mx->r[i] += (xr * (gr >> 4) + 128) >> 8;
            }
        }

        t->peak = pk;
    }
}
/* ---- DUST: the master through an old sampler and a record. */

static struct {
    int32_t hl, hr, hn;
    int32_t ll, lr;
    int32_t rnd, click;
    int32_t bed;
} dust = {0, 0, 0, 0, 0, 0x2545F491, 0, 0};

static int32_t crush_bits(int32_t v, int32_t shift)
{
    return v >= 0 ? (v >> shift) << shift : -((-v >> shift) << shift);
}

static void dust_process(int32_t *l, int32_t *r, uint32_t n)
{
    int32_t d = song.g[G_DUST], hold, shift, a, drive, hiss, i, bed0, bed1;
    uint32_t pc;
    if (!d) {
        dust.bed = 0;
        return;
    }
    hold = 1 + d * 3 / 127;
    shift = d / 18;
    a = 32767 - d * 165;
    drive = 4096 + d * 24;
    hiss = d * 2;
    pc = (uint32_t)d * 7u;
    bed0 = dust.bed;
    bed1 = dust.bed = clamp(dust.bed + (song.playing ? 238 : -238), 0, 32767);
    for (i = 0; i < (int32_t)n; i++) {
        int32_t x = l[i], y = r[i];
        int32_t bed = bed0 + (((bed1 - bed0) * i) >> CTL_LOG2);
        uint32_t nz = noise32(&dust.rnd);
        if (--dust.hn <= 0) {
            dust.hn = hold;
            dust.hl = softclip(((x >> 2) * drive) >> 10);
            dust.hr = softclip(((y >> 2) * drive) >> 10);
            if (shift) {
                dust.hl = crush_bits(dust.hl, shift);
                dust.hr = crush_bits(dust.hr, shift);
            }
        }
        dust.ll += mulq15(dust.hl - dust.ll, a);
        dust.lr += mulq15(dust.hr - dust.lr, a);
        if ((nz >> 10) < pc)
            dust.click = mulq15(((int32_t)(nz & 0x3FFu) - 512) * d / 8, bed);
        x = dust.ll + dust.click + mulq15(((int32_t)(nz >> 16) - 32768) * hiss >> 15, bed);
        y = dust.lr + dust.click + mulq15(((int32_t)(nz & 0xFFFFu) - 32768) * hiss >> 15, bed);
        dust.click -= dust.click >> 2;
        l[i] = x;
        r[i] = y;
    }
}
static void djf_process(int32_t *l, int32_t *r, uint32_t n)
{
    tsvf_t c;
    if (!djf_block(&djf, song.g[G_FILT], &c, 40))
        return;
    djf_run(&djf, &c, l, n, 0);
    djf_run(&djf, &c, r, n, 1);
}

#include "punch.c"
#define U_DRUMS (1u << NPART)
#define U_ALL ((2u << NPART) - 1u)

static void drum_insert(int32_t *l, int32_t *r, int32_t *v, int32_t *d, uint32_t n)
{
    track_t *t = TDRUM;
    int run;
    if (!(t->p[P_ITYPE] | t->ins_run))
        return;
    run = ins_run(&ins[NPART], t->p, l, n);
    run |= ins_run(&ins[NPART + 1u], t->p, r, n);
    run |= ins_run(&ins[NPART + 2u], t->p, v, n);
    run |= ins_run(&ins[NPART + 3u], t->p, d, n);
    t->ins_run = (uint8_t)run;
}
static void mix_drums(uint32_t n, mixacc_t *mx)
{
    uint32_t i;
    tsvf_t fc;
    djf_t *f = &tflt[TRK_DRUM];
    int flt;
    drums.a0 = TDRUM->att;
    drums.a1 = 32767 - gain_next(TDRUM);
    flt = n <= CTL && djf_block(f, TDRUM->p[P_TFLT], &fc, tflt_reso());
    if (flt || (n <= CTL && (TDRUM->p[P_ITYPE] | TDRUM->ins_run))) {
        int32_t *dl = drum_buf[0], *dr = drum_buf[1], *dv = drum_buf[2], *dd = drum_buf[3];
        for (i = 0; i < n; i++)
            dl[i] = dr[i] = dv[i] = dd[i] = 0;
        slicer_drums(dl, dr, dv, dd, n);
        drum_insert(dl, dr, dv, dd, n);
        if (flt) {
            tflt_run(f, &fc, dl, n, 0);
            tflt_run(f, &fc, dr, n, 1);
            tflt_run(f, &fc, dv, n, 2);
            tflt_run(f, &fc, dd, n, 3);
        }
        for (i = 0; i < n; i++) {
            mx->l[i] += dl[i];
            mx->r[i] += dr[i];
            mx->sr[i] += dv[i];
            mx->sd[i] += dd[i];
        }
    } else {
        slicer_drums(mx->l, mx->r, mx->sr, mx->sd, n);
    }
    click_render(mx->l, mx->r, n);
}
static void mix_units(uint32_t mask, uint32_t n, mixacc_t *mx, int32_t *scratch)
{
    uint32_t k;
    for (k = 0; k < NPART; k++)
        if (mask & (1u << k))
            mix_part(&trk[k], n, mx, scratch);
    if (mask & U_DRUMS)
        mix_drums(n, mx);
}

static uint32_t cpu1_job_ticks, cpu1_wait_ticks, cpu1_busy_q8, cpu1_wait_q8;
static void cpu1_load_half(uint32_t half_us)
{
    uint32_t per = half_us * CPU1_TICKS_PER_US / 256u;
    if (!per)
        per = 1;
    cpu1_busy_q8 = (cpu1_busy_q8 * 15u + cpu1_job_ticks / per) / 16u;
    cpu1_wait_q8 = (cpu1_wait_q8 * 15u + cpu1_wait_ticks / per) / 16u;
    cpu1_job_ticks = cpu1_wait_ticks = 0;
}

static void cpu1_units_job(uint32_t arg)
{
    uint32_t t0 = cpu1_ticks();
    mix_units(arg & 0xFFu, arg >> 8, &acc[1], part_buf[1]);
    cpu1_job_ticks += cpu1_ticks() - t0;
}

static uint32_t units_split(void)
{
    uint32_t mask = cpu1_units < 0 ? (1u << (NPART - 1u)) | U_DRUMS : (uint32_t)cpu1_units & U_ALL, k, nform = 0;
    if (!mask)
        return 0;
    for (k = 0; k < NPART; k++)
        nform += ENGINES[trk[k].engine] == &ENG_FORMANT;
    for (k = 0; k < NPART; k++) {
        const track_t *t = &trk[k];
        const engine_t *e = ENGINES[t->engine];
        uint32_t fade = t->xf_on && t->xf, inc;
        const int16_t *p = fade && t->pold ? t->p_old : t->p;
          if (!(mask & (1u << k)))
            continue;
        inc = p[P_LSYNC] ? lfo_sync_inc(p) : LFO_INC[p[P_LRATE] & 127];
        if (t->lfo_ph + inc < t->lfo_ph || (e == &ENG_PHYS && phys_render_draws(t, fade ? t->pe_old[0] : p[P_E0])) ||
            (e == &ENG_FORMANT && nform > 1u))
            mask &= ~(1u << k);
    }
    return mask;
}
static int32_t master_cur = -1;
enum { TX_OFF, TX_VINYL, TX_TAPE, TX_RAIN, TX_WIND, TX_RADIO, TX_ROOM, TX_USR1, TX_N = TX_USR1 + 6 };
static struct {
    int32_t rnd;

    int32_t g;
    uint8_t src, play;
    uint32_t ph;
    int32_t a[2], b[2], c[2], d[2], e[2];
    int32_t lpm[2];
    int32_t t0, t1, t2;
    tsvf_t wc[2];
    int32_t wz[2][2];
    uint32_t upos, ufrac;
    int32_t up, ui, us0, us1;
    uint32_t ugen;
    int32_t ulast, uoff;
    uint8_t uwrap;
    djf_t f;
} tx = {.rnd = 0x2545F491};
static inline int32_t tx_n16(void) { return (int32_t)(noise32(&tx.rnd) >> 16) - 32768; }
static void texture_reset(uint32_t src)
{
    int32_t rnd = tx.rnd, g = tx.g;
    uint32_t ph = tx.ph;
    uint8_t play = tx.play;
    memset(&tx, 0, sizeof tx);
    tx.rnd = rnd ? rnd : 0x2545F491;
    tx.g = g, tx.ph = ph, tx.play = play;
    tx.src = (uint8_t)src;
    tx.t0 = 50 << 8, tx.t1 = 60 << 8;
    tx.ugen = smp_user_gen - 1u;
}
static inline int32_t tx_usr_next(const smp_zone_t *z)
{
    uint32_t pos = tx.upos, b = SMP_DATA[z->off + (pos >> 1)];
    uint32_t code = (pos & 1u) ? (b >> 4) : (b & 15u);
    int32_t step = IMA_STEP[(uint32_t)tx.ui <= 88u ? tx.ui : 88], vd = step >> 3;
    if (code & 4u) vd += step;
    if (code & 2u) vd += step >> 1;
    if (code & 1u) vd += step >> 2;
    tx.up = clamp(tx.up + ((code & 8u) ? -vd : vd), -32768, 32767);
    tx.ui = clamp(tx.ui + IMA_IDX[code & 7u], 0, 88);
    if (++pos > (z->looped ? z->le : z->n - 1u)) {
        pos = z->looped ? z->ls : 0u;
        tx.up = z->looped ? z->pred : 0;
        tx.ui = z->looped ? z->idx : 0;
        tx.uwrap = 1;
    }
    tx.upos = pos;
    return tx.up;
}
static void texture_gen(uint32_t src, int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t i, k;
    if (src >= TX_USR1) {
        uint32_t s = src - TX_USR1;
        const smp_zone_t *z = usr_nz[s] ? &usr_zone[s][0] : 0;
        if (!z || !z->n) {
            for (i = 0; i < n; i++)
                l[i] = r[i] = 0;
            return;
        }
        if (tx.ugen != smp_user_gen) {
            tx.ugen = smp_user_gen;
            tx.upos = 0, tx.ufrac = 0, tx.up = tx.ui = 0;
            tx.us0 = 0, tx.us1 = tx_usr_next(z);
            tx.uwrap = 1;
        }
        for (i = 0; i < n; i++) {
            int32_t v;
            tx.ufrac += z->rate;
            while (tx.ufrac >= 65536u) {
                tx.ufrac -= 65536u;
                tx.us0 = tx.us1;
                tx.us1 = tx_usr_next(z);
            }
            v = (tx.us0 + (((tx.us1 - tx.us0) * (int32_t)(tx.ufrac >> 1)) >> 15)) >> 1;
            if (tx.uwrap) {
                tx.uwrap = 0;
                tx.uoff = tx.ulast - v;
            }
            tx.uoff -= (tx.uoff >> 6) + (tx.uoff > 0) - (tx.uoff < 0);
            l[i] = r[i] = tx.ulast = v + tx.uoff;
        }
        return;
    }
    switch (src) {
    case TX_VINYL:
        for (i = 0; i < n; i++) {
            uint32_t z = noise32(&tx.rnd);
            int32_t w = (int32_t)(z >> 16) - 32768, h;
            tx.a[0] += (w >> 5) - (tx.a[0] >> 7);
            if ((z & 0xFFFFu) < 60u) {
                int32_t amp = 6000 + (int32_t)((z >> 8) & 0x3FFFu) + ((z & 1u) ? 0 : 12000 * ((z & 0x30u) == 0u));
                int32_t pan = (int32_t)((z >> 20) & 0xFFu);
                amp = (z & 0x100u) ? -amp : amp;
                tx.b[0] = (amp * (256 - pan)) >> 8;
                tx.b[1] = (amp * pan) >> 8;
            }
            for (k = 0; k < 2u; k++) {
                h = (k ? (int32_t)(noise32(&tx.rnd) >> 16) - 32768 : w);
                tx.d[k] += (h - tx.d[k]) >> 2;
                (k ? r : l)[i] = (tx.d[k] >> 4) + (tx.a[0] >> 2) + tx.b[k];
                tx.b[k] -= tx.b[k] >> 2;
            }
        }
        break;
    case TX_TAPE:
        if (!(tx.t1++ & 31))
            tx.t2 = (24 << 8) + (int32_t)((noise32(&tx.rnd) >> 20) & 0x7FFu) * 4;
        tx.t0 += (tx.t2 - tx.t0) >> 6;
        for (i = 0; i < n; i++)
            for (k = 0; k < 2u; k++) {
                int32_t w = tx_n16();
                tx.a[k] += (w - tx.a[k]) >> 2;
                (k ? r : l)[i] = (((tx.a[k] >> 1) + (w >> 3)) * (tx.t0 >> 8)) >> 5;
            }
        break;
    case TX_RAIN:
        for (i = 0; i < n; i++)
            for (k = 0; k < 2u; k++) {
                uint32_t z = noise32(&tx.rnd);
                int32_t w = (int32_t)(z >> 16) - 32768;
                tx.a[k] += (w - tx.a[k]) >> 4;
                if ((z & 0xFFFFu) < 420u)
                    tx.b[k] = 4000 + (int32_t)((z >> 4) & 0x3FFFu);
                tx.c[k] += (w - tx.c[k]) >> 1;
                (k ? r : l)[i] = (tx.a[k] >> 1) + (((w - tx.c[k]) * tx.b[k]) >> 14);
                tx.b[k] -= tx.b[k] >> 5;
            }
        break;
    case TX_WIND: {
        if (!(noise32(&tx.rnd) & 31u))
            tx.t2 = (int32_t)(noise32(&tx.rnd) & 0xFFFFu);
        tx.t0 += (((34 << 8) + (tx.t2 & 0xFF) * 44) - tx.t0) >> 7;
        tx.t1 += (((34 << 8) + (tx.t2 >> 8) * 44) - tx.t1) >> 7;
        tsvf_coef(&tx.wc[0], tx.t0, 95);
        tsvf_coef(&tx.wc[1], tx.t1, 95);
        for (i = 0; i < n; i++)
            for (k = 0; k < 2u; k++) {
                int32_t y = tsvf_lp(&tx.wc[k], tx_n16() >> 2, &tx.wz[k][0], &tx.wz[k][1]);
                (k ? r : l)[i] = clamp(y, -32767, 32767);
            }
        break;
    }
    case TX_RADIO:
        if ((tx.t1 -= (int32_t)n) <= 0) {
            uint32_t z = noise32(&tx.rnd);
            tx.t1 = 1500 + (int32_t)((z >> 8) & 0x3FFFu);
            tx.t2 = (z & 3u) == 0u ? 40 << 8 : (110 + (int32_t)((z >> 24) & 0x7Fu)) << 8;
        }
        tx.t0 += (tx.t2 - tx.t0) >> 2;
        for (i = 0; i < n; i++) {
            int32_t w = tx_n16(), y;
            if (!(i & 3u))
                tx.c[0] = w;
            tx.a[0] += (tx.c[0] - tx.a[0]) >> 1;
            tx.b[0] += (tx.a[0] - tx.b[0]) >> 4;
            y = (((tx.a[0] - tx.b[0]) >> 1) * (tx.t0 >> 8)) >> 6;
            l[i] = y;
            r[i] = y - (y >> 2);
        }
        break;
    case TX_ROOM:
        for (i = 0; i < n; i++)
            for (k = 0; k < 2u; k++) {
                int32_t w = tx_n16();
                tx.a[k] += (w >> 4) - (tx.a[k] >> 8);
                tx.b[k] += (tx.a[k] - tx.b[k]) >> 5;
                (k ? r : l)[i] = (tx.b[k] >> 1) + (w >> 6);
            }
        break;
    default:
        for (i = 0; i < n; i++)
            l[i] = r[i] = 0;
        break;
    }
}
static void texture_mix(int32_t *l, int32_t *r, uint32_t n)
{
    static int32_t tl[CTL], tr[CTL];
    static const uint8_t BARS[5] = {1, 2, 4, 8, 16};
    uint32_t src = (uint32_t)clamp(song.g[G_TXSRC], 0, TX_N - 1), i, bar;
    static const int16_t TX_SCALE[TX_USR1 + 1] = {0, 800, 301, 693, 1918, 149, 343, 256};
    int32_t lvl = clamp(song.g[G_TXLVL], 0, 127), on, g0, g1, mv, depth, u, lfo, k0, k1, sc;
    tsvf_t fc;
    if (song.playing && !tx.play)
        tx.ph = 0;
    tx.play = (uint8_t)song.playing;
    on = src && src == tx.src && (song.playing || song.g[G_TXWHEN]) && lvl;
    if (!tx.g && !on) {
        if (src != tx.src)
            texture_reset(src);
        return;
    }
    if (n > CTL)
        return;
    g0 = tx.g;
    g1 = tx.g = clamp(tx.g + (on ? 238 : -238), 0, 32767);
    bar = (uint32_t)FS * 240u / (uint32_t)clamp(song.g[G_BPM], 20, 300);
    tx.ph += (uint32_t)((0xFFFFFFFFu / (bar * BARS[clamp(song.g[G_TXRATE], 0, 4)])) * n);
    lfo = sine_i(tx.ph - 0x40000000u);
    depth = clamp(song.g[G_TXMOVE], 0, 127) * 258;
    u = mulq15(depth, (32767 - lfo) >> 1);
    mv = 32767 - mulq15(u, 21000);
    k0 = 32767 - mulq15(u, 28000);
    texture_gen(tx.src, tl, tr, n);
    sc = TX_SCALE[tx.src < TX_USR1 ? tx.src : TX_USR1];
    for (i = 0; i < n; i++) {
        tx.lpm[0] += ((((tl[i] * sc) >> 8) - tx.lpm[0]) >> 2) * (k0 >> 2) >> 11;
        tx.lpm[1] += ((((tr[i] * sc) >> 8) - tx.lpm[1]) >> 2) * (k0 >> 2) >> 11;
        tl[i] = tx.lpm[0], tr[i] = tx.lpm[1];
    }
    if (djf_block(&tx.f, clamp(song.g[G_TXTONE], -64, 63), &fc, 40)) {
        djf_run(&tx.f, &fc, tl, n, 0);
        djf_run(&tx.f, &fc, tr, n, 1);
    }
    k1 = mulq15(lvl * lvl * 2, mv);
    for (i = 0; i < n; i++) {
        int32_t g = g0 + (((g1 - g0) * (int32_t)i) >> CTL_LOG2), gk = mulq15(g, k1);
        if (song.g[G_TXDUCK])
            gk = mulq15(gk, duck.g0 + (((duck.g1 - duck.g0) * (int32_t)i) >> CTL_LOG2));
        l[i] += (tl[i] * (gk >> 4)) >> 11;
        r[i] += (tr[i] * (gk >> 4)) >> 11;
    }
    if (!g1 && src != tx.src)
        texture_reset(src);
}

/* AUDILUNE v3.7: Utilitarian Discrete Frequency Layer Engine (<0.2% CPU, 0 B extra SRAM) */
static volatile uint8_t audilune_freq_layer;   /* 0=AUTO, 1=OFF, 2=LOW, 3=MED */
static volatile uint8_t audilune_session_mode; /* 0=STUDIO, 1=SLEEP, 2=MEDITATE, 3=STUDY, 4=CHILL, 5=DANCE */
static volatile uint8_t audilune_root_note;    /* 0..11 (C..B) */
static volatile uint8_t audilune_genre;        /* 0..5 */

static void freq_layer_process(int32_t *l, int32_t *r, uint32_t n)
{
    static uint32_t ph_l, ph_r, ph_2, ph_3, ph_mod, rnd = 0x6D2B79F5u;
    static int32_t pink, lp_l, lp_r, sub_env;
    uint32_t mode = audilune_freq_layer, sm = audilune_session_mode, g = audilune_genre % 6u, i;
    int32_t lvl = mode == 1u ? 0 : mode == 2u ? 140 : mode == 3u ? 280 : (sm == 0u ? 0 : 200);
    uint32_t root_inc = pitch_inc((36 + (int32_t)(audilune_root_note % 12u)) * 16);
    uint32_t mod_hz = sm == 1u ? 2u : sm == 2u ? 6u : sm == 3u ? 16u : sm == 4u ? 10u : (g == 0u ? 4u : 8u);
    uint32_t mod_inc = (uint32_t)(((uint64_t)mod_hz << 32) / FS);
    if (!lvl)
        return;
    for (i = 0; i < n; i++) {
        int32_t xl = l[i], xr = r[i], add_l = 0, add_r = 0, m_s, nz;
        int32_t mono = (xl + xr) >> 1, abs_m = mono < 0 ? -mono : mono;
        sub_env += (abs_m - sub_env) >> (abs_m > sub_env ? 4 : 9);
        rnd = rnd * 1664525u + 1013904223u;
        nz = (int32_t)(rnd >> 17) - 16384;
        pink += (nz - pink) >> 4;
        m_s = sine_i(ph_mod);
        ph_mod += mod_inc;
        if (sm == 1u) {                                 /* SLEEP: -6dB/oct Brownian slope + 2Hz delta pacing */
            int32_t g_mod = 24576 + (m_s >> 2);
            add_l = add_r = mulq15(pink >> 1, g_mod);
        } else if (sm == 2u) {                          /* MEDITATE: root + oct + 5th + 6Hz theta binaural */
            int32_t h_l = (sine_i(ph_l) + (sine_i(ph_2) >> 1) + (sine_i(ph_3) >> 2)) >> 4;
            int32_t h_r = (sine_i(ph_r) + (sine_i(ph_2) >> 1) + (sine_i(ph_3) >> 2)) >> 4;
            ph_l += root_inc;
            ph_r += root_inc + mod_inc;
            ph_2 += root_inc << 1;
            ph_3 += root_inc * 3u;
            add_l = h_l;
            add_r = h_r;
        } else if (sm == 3u) {                          /* STUDY FOCUS: 300Hz-3kHz speech bed + 16Hz beta */
            int32_t band = (pink - (pink >> 2)) >> 1;
            int32_t iso = 26000 + (m_s >> 3);
            add_l = add_r = mulq15(band, iso);
        } else if (sm == 4u) {                          /* CHILL: >8kHz roll-off + 9.5Hz alpha stereo drift */
            lp_l += (xl - lp_l) >> 1;
            lp_r += (xr - lp_r) >> 1;
            xl = lp_l + ((xl - lp_l) >> 1);
            xr = lp_r + ((xr - lp_r) >> 1);
            add_l = (m_s * lvl) >> 15;
            add_r = (-m_s * lvl) >> 15;
        } else if (sm == 5u) {                          /* DANCE: missing-fundamental 50Hz + 100Hz sub */
            uint32_t f50 = (uint32_t)(((uint64_t)50u << 32) / FS);
            int32_t sub = (sine_i(ph_l) + (sine_i(ph_l << 1) >> 1)) >> 3;
            ph_l += f50;
            add_l = add_r = mulq15(sub, clamp(sub_env, 0, 32767));
        }
        /* Genre-tuned acoustic utility shaping (0..5) */
        if (g == 0u) {                                  /* 0 Ambient: wide stereo 10-14kHz silky air */
            int32_t air = (nz - pink) >> 3;
            add_l += mulq15(air, 16384 + (m_s >> 2));
            add_r += mulq15(air, 16384 - (m_s >> 2));
        } else if (g == 1u) {                           /* 1 Downtempo: 110-160Hz even-order harmonic glue */
            int32_t warm = mulq15(mono >> 2, mono >> 2) >> 3;
            add_l += warm;
            add_r += warm;
        } else if (g == 2u) {                           /* 2 Glitch Hop: transient-ducked micro-noise floor */
            int32_t duck_n = sub_env > 4000 ? 0 : (nz >> 4);
            add_l += duck_n;
            add_r -= duck_n;
        } else if (g == 3u) {                           /* 3 Punchy: phase-aligned mono 50Hz + 100Hz anchor */
            uint32_t f50 = (uint32_t)(((uint64_t)50u << 32) / FS);
            int32_t anc = (sine_i(ph_r) + (sine_i(ph_r << 1) >> 1)) >> 4;
            ph_r += f50;
            add_l += mulq15(anc, clamp(sub_env, 0, 32767));
            add_r += mulq15(anc, clamp(sub_env, 0, 32767));
        } else if (g == 4u) {                           /* 4 Experimental: 1.414x / 1.618x partials */
            uint32_t f_phi = (uint32_t)((uint64_t)root_inc * 1618u / 1000u);
            int32_t ov = sine_i(ph_3) >> 4;
            ph_3 += f_phi;
            add_l += ov;
            add_r -= ov;
        } else {                                        /* 5 Industrial: 40Hz physical pressure + 120Hz res */
            uint32_t f40 = (uint32_t)(((uint64_t)40u << 32) / FS);
            int32_t ind = (sine_i(ph_l) + (sine_i(ph_l * 3u) >> 1)) >> 4;
            ph_l += f40;
            add_l += ind;
            add_r += ind;
        }
        l[i] = xl + ((add_l * lvl) >> 8);
        r[i] = xr + ((add_r * lvl) >> 8);
    }
}

static uint16_t al_land_on;
static uint8_t al_land_sub[5 + 5], al_land_vol[5 + 5];

static void mix_block(int32_t *out, uint32_t n)
{
    uint32_t i, c1;
    int32_t m0, m1, *mix_l = (acc + 0)->l, *mix_r = (acc + 0)->r;
    for (i = 0; i < n; i++)
        *((acc + 0)->l + i) = *((acc + 0)->r + i) = *((acc + 0)->sc + i) = *((acc + 0)->sd + i) = *((acc + 0)->sr + i) = 0;
    {
        uint32_t q = thr_req | thr_ed;

        thr_rev = (q & 1u) ? (thr_rev + 1928 > 32767 ? 32767 : thr_rev + 1928) : (thr_rev > 1928 ? thr_rev - 1928 : 0);
        thr_dly = (q & 2u) ? (thr_dly + 1928 > 32767 ? 32767 : thr_dly + 1928) : (thr_dly > 1928 ? thr_dly - 1928 : 0);
    }
    events_block(n);
    duck_block(n * (uint32_t)song.g[G_BPM]);
    c1 = cpu1_on() ? units_split() : 0;
    if (c1) {
        for (i = 0; i < n; i++)
            *((acc + 1)->l + i) = *((acc + 1)->r + i) = *((acc + 1)->sc + i) = *((acc + 1)->sd + i) = *((acc + 1)->sr + i) = 0;
        if (!cpu1_run(cpu1_units_job, c1 | n << 8))
            c1 = 0;
    }
    mix_units(U_ALL & ~c1, n, acc + 0, *(part_buf + 0));
    if (c1) {
        uint32_t w0 = cpu1_ticks();
        int late = cpu1_wait();
        cpu1_wait_ticks += cpu1_ticks() - w0;
        if (late) {
            for (i = 0; i < n; i++)
                *((acc + 1)->l + i) = *((acc + 1)->r + i) = *((acc + 1)->sc + i) = *((acc + 1)->sd + i) = *((acc + 1)->sr + i) = 0;
            cpu1_units_job(c1 | n << 8);
        }
        for (i = 0; i < n; i++) {
            *((acc + 0)->l + i) += *((acc + 1)->l + i);
            *((acc + 0)->r + i) += *((acc + 1)->r + i);
            *((acc + 0)->sc + i) += *((acc + 1)->sc + i);
            *((acc + 0)->sd + i) += *((acc + 1)->sd + i);
            *((acc + 0)->sr + i) += *((acc + 1)->sr + i);
        }
    }
    texture_mix(mix_l, mix_r, n);
    if (al_land_on) {
        static uint32_t lp0, lp1, lp2;
        static int32_t lrnd = 0x3A71C9E5, llp, lbp, lenv;
        uint32_t ri = pitch_inc((36 + (int32_t)(audilune_root_note % 12u)) * 16), k;
        for (i = 0; i < n; i++) {
            int32_t sl = 0, sr = 0, nz = (int32_t)(noise32(&lrnd) >> 17) - 16384;
            llp += (nz - llp) >> 4;
            lbp += ((nz - llp) - lbp) >> 2;
            lp0 += ri;

            lp1 += (ri * 3u) >> 1;
            lp2 += 340000u;
            lenv = (sine_i(lp2) + 32768) >> 1;
            for (k = 0u; k < 10u; k++) {
                int32_t v = *(al_land_vol + k), s = 0, d = 0;
                if (!(((al_land_on >> k) & 1u) && v > 0)) continue;
                if (k == 0u && *(al_land_sub + 0) > 0u) s = (sine_i(lp0) + (sine_i(lp1) >> 1)) >> 3;
                else if (k <= 3u) s = mulq15(llp, 16384 + (lenv >> 1));
                else if (k <= 5u) s = mulq15(sine_i(lp1 << 2) >> 3, lenv);
                else if (k <= 8u) s = lbp >> 1;
                else { d = mulq15(llp >> 2, sine_i(lp2)); }
                s = (s * v) >> 7;
                d = (d * v) >> 7;
                sl += s + d;
                sr += s - d;
            }
            *(mix_l + i) += sl;
            *(mix_r + i) += sr;
        }
    }
    punch_freeze_feed((acc + 0)->sr, mix_l, mix_r, n);
    fx_buses((acc + 0)->sc, (acc + 0)->sd, (acc + 0)->sr, wet_l, wet_r, n);
    for (i = 0; i < n; i++) {
        *(mix_l + i) += *(wet_l + i);

        mix_r[i] += wet_r[i];
    }
    dust_process(mix_l, mix_r, n);
    punch_process(mix_l, mix_r, n);
    djf_process(mix_l, mix_r, n);
    freq_layer_process(mix_l, mix_r, n);
    m1 = (int32_t)song.master_q12;
    m0 = master_cur < 0 ? m1 : master_cur;
    master_cur = m1;
    if (n > CTL)
        n = CTL;
    {
        static int32_t ol[CTL], orr[CTL], ul[CTL], ur[CTL], side_lp;
        static int32_t xfmr_lp __attribute__((section(".pool"))), xfmr_hp __attribute__((section(".pool")));
        static uint8_t usb_was;
        int32_t wide_ovf = al_layer_mix > 100u ? (int32_t)(al_layer_mix - 100u) : 0;
        for (i = 0; i < n; i++) {
            int32_t mid = (mix_l[i] + mix_r[i]) >> 1, side = (mix_l[i] - mix_r[i]) >> 1, m, sub80, mx_s, mx_2;
            xfmr_lp += (mid - xfmr_lp) >> 6;            /* ~110 Hz LP */
            xfmr_hp += (xfmr_lp - xfmr_hp) >> 7;        /* ~55 Hz HP -> ~80 Hz resonant transformer weight */
            sub80 = (xfmr_lp - xfmr_hp) >> 3;
            mx_s = clamp(mid >> 2, -32767, 32767);
            mx_2 = (mx_s * mx_s) >> 18;                 /* subtle Class-A even-harmonic warmth */
            mid += sub80 + (mid >= 0 ? mx_2 : -mx_2);
            side_lp += (side - side_lp) >> 6;
            side -= side_lp;
            if (wide_ovf)
                side += (side * wide_ovf) >> 5;
            mix_l[i] = mid + side;
            mix_r[i] = mid - side;
            m = m0 + (((m1 - m0) * (int32_t)i) >> CTL_LOG2);
            ol[i] = MULQ12R(mix_l[i], m, 2);
            orr[i] = MULQ12R(mix_r[i], m, 2);
            master_pre(&ol[i], &orr[i]);
            if ((i & 1u)) {
                vis_tap[2u * i] = (int16_t)knee(mix_l[i]);
                vis_tap[2u * i + 1u] = (int16_t)knee(mix_r[i]);
            }
            if (usb_full_now) {
                ul[i] = mix_l[i], ur[i] = mix_r[i];
                master_pre_usb(&ul[i], &ur[i]);
            }
        }
        if (usb_full_now && !usb_was) {
            memset(&lim_usb, 0, sizeof lim_usb);
            lim_usb.env = LIM_T, lim_usb.g = 32768;
        }
        usb_was = usb_full_now;
        for (uint32_t ch = 0; ch <= (uint32_t)(usb_full_now != 0); ch++) {
            int32_t *pl = ch ? ul : ol, *pr = ch ? ur : orr, *dst = ch ? usb_out : out;
            lim_block(ch ? &lim_usb : &lim_dac, pl, pr, n);
            for (i = 0; i < n; i++) {
                dst[2u * i] = knee(pl[i]);
                dst[2u * i + 1u] = knee(pr[i]);
            }
        }
    }
}
