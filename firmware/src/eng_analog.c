/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* ANALOG + Classic Architectural Emulations (MINIMOOG D & EMS VCS3 — zero extra SRAM) */
static const char *const N_ANALOG_WAVE[4 + 3] = {"SAW", "SQR", "TRI", "SIN", "PWM", "MOOG", "VCS3"};

static void analog_note_on(track_t *t, voice_t *v)
{
    uint32_t w = (uint32_t)t->p[P_E0];
    if (!v->env && !v->env_out) {
        *(v->ph + 0) = 0u;
        *(v->ph + 2) = 0u;
        *(v->ph + 3) = 0u;
    }
    *(v->ph + 1) = *(v->ph + 0) + 0x40000000u;
    *(v->s + 0) = *(v->s + 1) = *(v->s + 3) = *(v->s + 4) = *(v->s + 5) = *(v->s + 6) = *(v->s + 7) = 0;
    if (!*(v->s + 2) || w >= 5u)
        *(v->s + 2) = 0x1234567 + (int32_t)v->age;
}

/* Shared 2D Pad / EMS VCS3 Joystick X/Y coordinates (0..127, center 64) */

static uint8_t al_pad_latch, al_pad_idx;
static uint8_t al_pad_x[4 + 4] = {64, 64, 64, 64, 64, 64, 64, 64};
static uint8_t al_pad_y[4 + 4] = {64, 64, 64, 64, 64, 64, 64, 64};

/* Shared Minimoog Model D (vcs3=0) & EMS VCS 3 / Synthi AKS (vcs3=1) Architectural Renderer */
UI_SMALL static void analog_classic_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m, int vcs3)
{
    const int16_t *p = t->p;
    int32_t e1 = p[P_E1], mix = p[P_E2] * 258, e3 = p[P_E3], res = p[P_E5], drv = p[P_E6], e7 = p[P_E7];
    int32_t jx = (vcs3 && (al_pad_latch & 127u)) ? (int32_t)*(al_pad_x + (al_pad_idx & 7u)) : 64;
    int32_t jy = (vcs3 && (al_pad_latch & 127u)) ? (int32_t)*(al_pad_y + (al_pad_idx & 7u)) : 64;
    int32_t cut_base = (p[P_E4] << 8) + m->cutoff + (vcs3 ? ((jx - 64) << 7) : ((e7 * (v->pitch16 - 960)) >> 4));
    int32_t oct_sh = clamp((e1 / 22) - 2, -2, 2);
    uint32_t inc1 = vcs3 ? m->inc : (oct_sh >= 0 ? (m->inc << (uint32_t)oct_sh) : (m->inc >> (uint32_t)(-oct_sh)));
    uint32_t inc2 = pitch_inc(clamp(m->pitch16 + (vcs3 ? (((jy - 64) >> 1) - 112) : ((e1 % 22) - 11)), 0, 2047));
    uint32_t inc3 = vcs3 ? (uint32_t)(14000u + (uint32_t)e7 * 26000u) : ((e3 > 64) ? (uint32_t)(18000u + (uint32_t)(e3 - 64) * 9500u) : (inc1 >> 1));
    uint32_t ph0 = *(v->ph + 0), ph1 = *(v->ph + 1), ph2 = *(v->ph + 2), tph = *(v->ph + 3);
    int32_t st0 = *(v->s + 0), st1 = *(v->s + 1), st3 = *(v->s + 3), st4 = *(v->s + 4), nst = *(v->s + 2);
    int32_t fb_q12 = clamp((res + (vcs3 ? ((jy - 64) >> 2) : 0)) * 35, 0, 4400);
    int32_t morph = clamp((e1 << 8) + (m->shape - (64 << 8)), 0, 127 << 8) >> 1;
    uint32_t tinc = (vcs3 && e7 > 64) ? (uint32_t)(e7 - 64) * 32000u : 0u, i;
    for (i = 0; i < n; i++) {
        int32_t o1 = vcs3 ? (osc_sine(ph0) + mulq15(osc_saw(ph0, inc1) - osc_sine(ph0), morph)) : ((osc_saw(ph0, inc1) * 3 + osc_tri(ph0)) >> 2);
        int32_t o2 = vcs3 ? (osc_tri(ph1) + mulq15(osc_pulse(ph1, inc2, 0x60000000u) - osc_tri(ph1), morph)) : osc_saw(ph1, inc2);
        int32_t o3 = (vcs3 || e3 > 64) ? osc_tri(ph2) : osc_pulse(ph2, inc3, 0x80000000u);
        int32_t trap = tinc ? ((tph < 0x40000000u) ? (int32_t)(tph >> 15) : (tph < 0x80000000u) ? 32767 : (tph < 0xC0000000u) ? (int32_t)((0xC0000000u - tph) >> 15) : 0) : 32767;
        int32_t s = vcs3 ? (mulq15((o1 + o2) >> 1, 32767 - mix) + mulq15((o1 * o2) >> 15, mix)) : (o1 + mulq15((o2 + ((e3 > 64) ? 0 : o3)) >> 1, mix));
        int32_t cmod = vcs3 ? (((o3 * 48) >> 5) + ((trap - 16384) >> 1)) : ((e3 > 64) ? ((o3 * (e3 - 64)) >> 4) : 0);
        int32_t g_q15 = 1300 + ((clamp(cut_base + cmod, 0, 127 << 8) * 28300) >> 15), x;
        if (e3 && (vcs3 || e3 <= 64))
            s += mulq15((int32_t)(noise32(&nst) >> 17) - 16384, e3 * 240);
        s = softclip(((s >> 1) * (32768 + drv * 580)) >> 14);
        x = softclip(s - ((fb_q12 * (vcs3 ? st3 : (st4 - (s >> 3)))) >> 12));
        st0 += mulq15(x - st0, g_q15);
        st1 += mulq15((vcs3 ? softclip(st0) : st0) - st1, g_q15);
        st3 += mulq15(st1 - st3, g_q15);
        if (vcs3) {
            s = st3;
            ph0 += inc1 + (uint32_t)((o3 * (jx - 64)) << 3);
        } else {
            st4 += mulq15(st3 - st4, g_q15);
            s = st4 + mulq15(s, res * 120);
            ph0 += inc1;
        }
        ph1 += inc2; ph2 += inc3; tph += tinc;
        *(out + i) += mulq15(mulq15(softclip(s), mulq15(amp_at(m, i), (int16_t)trap)), VOICE_FS) << 1;
    }
    *(v->ph + 0) = ph0; *(v->ph + 1) = ph1; *(v->ph + 2) = ph2; *(v->ph + 3) = tph;
    *(v->s + 0) = st0; *(v->s + 1) = st1; *(v->s + 3) = st3; *(v->s + 4) = st4; *(v->s + 2) = nst;
}

/* the filter's TYPE (SLOOP 2.6, after Felucca 1.5 by Leo Kuroshita, #104: track parameter P_FTYPE, EDIT > FILTER):
 * the SVF's outputs (dsp.c tsvf_lp, written out here with the taps: Simper's trapezoidal state-variable filter), 0 LP
 * the low-pass as always (tsvf_lp itself: the same steps, bit for bit), 1 BP its band-pass v1, 2 HP in - k v1 - v2. At

 * the cutoff the three peak alike (1/k, k the damping RES sets: 2 at RES 0, ~0.15 at 127), so the soft knee after them
 * and the level stay as LP's */
static inline int32_t analog_tap(const tsvf_t *c, int32_t k, int32_t in, int32_t *ic1, int32_t *ic2, uint32_t ft)
{
    int32_t v3 = in - *ic2;
    int32_t v1 = (c->a1 * *ic1 + c->a2 * v3) >> 13;
    int32_t v2 = *ic2 + ((c->a2 * *ic1 + c->a3 * v3) >> 13);
    *ic1 = clamp(2 * v1 - *ic1, -150000, 150000);
    *ic2 = clamp(2 * v2 - *ic2, -150000, 150000);
    return ft == 1u ? v1 : in - ((k * v1) >> 12) - v2;
}
#define ANALOG_K(res) (8192 - (res) * 7600 / 127)     /* tsvf_coef's damping, Q12 */

/* the voice through the filter TYPE ft: analog_render's loop with ft 0 (a constant: LP, the code as it always was),
 * analog_render_f's with BP / HP (its own copy: the LP loop gains nothing) */

static inline __attribute__((always_inline)) void analog_body(track_t *t, voice_t *v, int32_t *out, uint32_t n,
                                                              const vmod_t *m, uint32_t ft)
{
    const int16_t *p = t->p;
    uint32_t wave = (uint32_t)p[P_E0], i;
    int32_t det = p[P_E1], mix = p[P_E2], noise = p[P_E3];
    int32_t cut = (p[P_E4] << 8) + m->cutoff + (p[P_E7] * (v->pitch16 - 60 * 16) >> 4);
    tsvf_t flt;
    int32_t drive = 32768 + p[P_E6] * 768;                       /* 1x .. 4x */
    int32_t dw = p[P_E6] * 258;                                  /* DRV: dry -> driven (a clean range low) */
    uint32_t inc1 = m->inc;
    /* DTN in cents: whole 1/16 semitones from the table, the rest as a fine factor */
    int32_t d16 = det * 16 / 100, rem = det * 16 - d16 * 100;            /* rem: 1/1600 semitone */
    uint32_t inc2 = pitch_inc(clamp(m->pitch16 + d16, 0, 2047));
    inc2 += (uint32_t)((int32_t)(inc2 >> 12) * (rem * 2367 / 16000));
    uint32_t pw = 0x80000000u + (uint32_t)((m->shape - (64 << 8)) << 15);
    int32_t m2 = mix * 258, m1 = 32767 - m2;                    /* osc mix Q15 */
    int32_t nz = noise * 200, drv = p[P_E6];
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1];                  /* state in locals: out[] may alias v->s[] */
    int32_t ic1 = v->s[0], ic2 = v->s[1], nst = v->s[2];
    int32_t fk = ft ? ANALOG_K(p[P_E5]) : 0;
    tsvf_coef(&flt, cut, p[P_E5]);
    if (det == 0)
        inc2 = inc1;
    for (i = 0; i < n; i++) {
        int32_t a, b = 0, s;
        switch (wave) {
        case 1:
            a = osc_pulse(ph0, inc1, 0x80000000u);
            if (m2)
                b = osc_pulse(ph1, inc2, 0x80000000u);
            break;
        case 2:
            a = osc_tri(ph0);
            if (m2)
                b = osc_tri(ph1);
            break;
        case 3:
            a = osc_sine(ph0);
            if (m2)
                b = osc_sine(ph1);
            break;
        case 4:
            a = osc_pulse(ph0, inc1, pw);
            if (m2)
                b = osc_pulse(ph1, inc2, pw);
            break;
        default:
            a = osc_saw(ph0, inc1);
            if (m2)
                b = osc_saw(ph1, inc2);
            break;
        }
        ph0 += inc1;
        ph1 += inc2;
        s = m2 ? mulq15(a, m1) + mulq15(b, m2) : a;  /* (MIX 0, the 808s and subs: one oscillator) */
        if (nz)
            s += mulq15((int32_t)(noise32(&nst) >> 17) - 16384, nz);
        if (drv)                                      /* pre-shifts: drive is up to 4x, no overflow */
            s += mulq15(softclip(((s >> 2) * (drive >> 2)) >> 11) - s, dw);
        {   /* filter: linear up to half scale, then a soft knee (only resonance peaks saturate) */
            int32_t y = ft ? analog_tap(&flt, fk, s >> 1, &ic1, &ic2, ft) : tsvf_lp(&flt, s >> 1, &ic1, &ic2), a = y < 0 ? -y : y;
            if (a > 16000) {
                a = 16000 + (softclip((a - 16000) * 2) >> 1);
                y = y < 0 ? -a : a;
            }
            s = y << 1;
        }
        *(out + i) += mulq15(mulq15(s, amp_at(m, i)), VOICE_FS) << 1;
    }
    *(v->ph + 0) = ph0;
    *(v->ph + 1) = ph1;
    *(v->s + 0) = ic1;
    *(v->s + 1) = ic2;
    *(v->s + 2) = nst;
}
static __attribute__((noinline)) void analog_render_f(track_t *t, voice_t *v, int32_t *out, uint32_t n,
                                                      const vmod_t *m)
{
    analog_body(t, v, out, n, m, t->p[P_FTYPE] == 1 ? 1u : 2u);
}
static void analog_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    if (t->p[P_E0] == 5 || t->p[P_E0] == 6) {
        analog_classic_render(t, v, out, n, m, t->p[P_E0] == 6);
        return;
    }
    if (t->p[P_FTYPE])                                  /* BP / HP (2.6) */
        analog_render_f(t, v, out, n, m);
    else
        analog_body(t, v, out, n, m, 0);
}

static const preset_t ANALOG_PRESETS[] = {

    /* SLOOP hip-hop / drum & bass bank. {WAVE, DTN, MIX, NOIS, CUT, RES, DRV, KTR}, {A D S R}, fenv, mono */
    /* 808s: a sine that decays (SUS 0) with a little pitch drop (ENV -> PITCH) and slides (GLIDE, legato) */
    {"808 BOOM", {3, 0, 0, 0, 127, 0, 48, 0}, {0, 112, 0, 52}, 0, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 59, P_GLMODE + 1, 1, P_ED_PIT + 1, 18, P_TRANS + 1, -24)},
    {"808 DIRTY", {3, 0, 0, 0, 96, 12, 118, 30}, {0, 108, 0, 46}, 0, 1, FX(24, 0, 0, 0), XP(P_GLIDE + 1, 56, P_GLMODE + 1, 1, P_ED_PIT + 1, 24, P_TRANS + 1, -24)},
    {"SUB BASS", {3, 0, 0, 0, 127, 0, 10, 0}, {0, 60, 118, 24}, 0, 1, FX(0, 0, 0, 0), XP(P_GLIDE + 1, 40, P_TRANS + 1, -24)},
    {"808 SLIDE", {3, 0, 0, 0, 127, 0, 72, 0}, {0, 118, 0, 60}, 0, 1, FX(30, 0, 0, 0), XP(P_GLIDE + 1, 82, P_GLMODE + 1, 1, P_ED_PIT + 1, 12, P_TRANS + 1, -24)},
    /* acid: one saw, high resonance, the envelope on the filter, slides where notes overlap */

    {"ACID 303", {0, 0, 0, 0, 36, 112, 44, 64}, {0, 56, 24, 20}, 52, 1, FX(28, 0, 22, 10), XP(P_GLIDE + 1, 34, P_GLMODE + 1, 1, P_TRANS + 1, -12)},
    /* plugg / soft trap: a round triangle bass */
    {"PLUGG BASS", {2, 0, 0, 0, 92, 0, 30, 0}, {0, 96, 92, 34}, 0, 1, FX(0, 0, 0, 6), XP(P_GLIDE + 1, 52, P_GLMODE + 1, 1, P_TRANS + 1, -24)},
    /* drum & bass: two detuned saws, slowly moving filter */
    {"REESE", {0, 22, 64, 0, 56, 22, 36, 40}, {0, 70, 118, 28}, 0, 1, FX(0, 22, 0, 6), XP(P_LRATE + 1, 22, P_LD_FLT + 1, 10, P_TRANS + 1, -24)},
    {"WOBBLE", {0, 9, 64, 0, 38, 72, 62, 30}, {0, 64, 127, 22}, 0, 1, FX(10, 0, 0, 4), XP(P_LRATE + 1, 74, P_LD_FLT + 1, 40, P_TRANS + 1, -24)},
    /* west coast: a square bass that slides, the high sine-ish lead with a delayed vibrato */
    {"FUNK BASS", {1, 6, 50, 0, 50, 30, 22, 64}, {0, 55, 62, 20}, 30, 1, FX(0, 0, 0, 4), XP(P_GLIDE + 1, 50, P_GLMODE + 1, 1, P_TRANS + 1, -24)},
    {"G-FUNK LD", {2, 0, 0, 0, 92, 0, 10, 64}, {6, 70, 112, 46}, 0, 1, FX(0, 0, 28, 30),
     XP(P_GLIDE + 1, 74, P_LD_PIT + 1, 2, P_LRATE + 1, 89, P_LFADE + 1, 50)},
    {"TRAP PLUCK", {0, 8, 64, 0, 52, 35, 20, 64}, {0, 80, 24, 45}, 48, 0, FX(0, 15, 34, 26)},
    {"SYN BRASS", {0, 10, 64, 0, 45, 20, 16, 64}, {12, 70, 96, 35}, 34, 0, FX(0, 15, 12, 24)},
    /* trance / EDM: eight detuned saw voices on one note */
    {"SUPERSAW", {0, 14, 64, 0, 96, 12, 10, 64}, {4, 70, 112, 46}, 10, 1, FX(0, 20, 30, 36), XP(P_VOICE + 1, 3, P_DETUNE + 1, 64, P_GLIDE + 1, 30)},
    {"WARM PAD", {4, 12, 64, 6, 52, 8, 0, 32}, {75, 90, 115, 90}, 0, 0, FX(0, 55, 20, 55), XP(P_LRATE + 1, 30, P_LD_SHP + 1, 20)},
    {"DARK STR", {0, 18, 64, 0, 48, 6, 0, 32}, {60, 90, 118, 85}, 0, 0, FX(0, 50, 18, 60)},
    {"ATMOS PAD", {4, 25, 64, 20, 60, 15, 0, 32}, {90, 100, 120, 100}, 0, 0, FX(0, 60, 30, 75), XP(P_LRATE + 1, 18, P_LD_PIT + 1, 1)},
    /* SLOOP 2.5: a breathy pan flute, a fat mono lead, a classic mono bass */
    {"PAN FLUTE", {2, 0, 0, 30, 70, 10, 0, 64}, {14, 70, 110, 40}, 0, 1, FX(0, 10, 24, 45),
     XP(P_LD_PIT + 1, 1, P_LRATE + 1, 80, P_LFADE + 1, 70)},
    {"FAT LEAD", {0, 8, 64, 0, 70, 30, 30, 64}, {2, 70, 110, 30}, 30, 1, FX(0, 0, 26, 20), XP(P_GLIDE + 1, 40)},
    {"MONO BASS", {0, 4, 64, 0, 48, 36, 30, 64}, {0, 60, 70, 20}, 44, 1, FX(10, 0, 0, 4), XP(P_TRANS + 1, -24)},
    {"PWM LEAD", {4, 0, 0, 0, 80, 20, 10, 64}, {2, 70, 110, 30}, 0, 1, FX(0, 10, 26, 22),
     XP(P_LD_PIT + 1, 1, P_LRATE + 1, 80, P_LFADE + 1, 60, P_GLIDE + 1, 30)},
    {"SAW BASS", {0, 6, 64, 0, 52, 30, 40, 64}, {0, 60, 60, 20}, 30, 1, FX(8, 0, 0, 4), XP(P_TRANS + 1, -24)},
    {"SINE PAD", {3, 10, 64, 4, 90, 0, 0, 32}, {80, 90, 118, 90}, 0, 0, FX(0, 60, 20, 65)},
    /* AUDILUNE Classic Synth Architectural Emulations: Minimoog Model D & EMS VCS 3 / Synthi AKS */
    {"MINIMOOG D", {5, 52, 108, 0, 64, 46, 56, 64}, {2, 62, 92, 24}, 42, 1, FX(14, 0, 18, 14), XP(P_GLIDE + 1, 42, P_GLMODE + 1, 1)},
    {"MOOG BASS", {5, 28, 116, 0, 46, 54, 72, 64}, {0, 54, 68, 18}, 48, 1, FX(18, 0, 0, 6), XP(P_GLIDE + 1, 36, P_GLMODE + 1, 1, P_TRANS + 1, -24)},
    {"MOOG LEAD", {5, 74, 104, 84, 76, 42, 48, 80}, {4, 68, 108, 32}, 36, 1, FX(10, 12, 28, 22), XP(P_GLIDE + 1, 48)},
    {"MOOG BRASS", {5, 50, 112, 0, 54, 28, 38, 64}, {14, 74, 98, 34}, 38, 0, FX(8, 18, 16, 26)},
    {"EMS VCS3", {6, 48, 72, 16, 62, 68, 44, 82}, {6, 76, 94, 44}, 32, 1, FX(12, 24, 34, 38), XP(P_GLIDE + 1, 34)},
    {"SYNTHI AKS", {6, 78, 86, 24, 56, 78, 52, 96}, {10, 82, 88, 52}, 40, 1, FX(16, 30, 42, 46)},
    {"VCS3 MATRIX", {6, 96, 104, 32, 70, 84, 60, 112}, {4, 68, 72, 48}, 48, 1, FX(20, 36, 48, 54)},
    {"VCS3 DRONE", {6, 34, 58, 28, 52, 64, 36, 88}, {68, 104, 118, 86}, 24, 0, FX(8, 44, 52, 64)},
};

/* (after Clipper 1.2) UNISON (voice.c unison_spread): both oscillators moved by r (Q32) of a turn of the note,
 * each by its share: as if started that much sooner (osc 2 took its phase from ph[0] before the spread: in phase) */
static void analog_spread(track_t *t, voice_t *v, uint32_t r)
{
    *(v->ph + 0) += r;
    *(v->ph + 1) += uni_turns(r, v->pitch16, t->p[P_E1] * 16 / 100);
}

static const engine_t ENG_ANALOG = {
    "ANALOG", {"OSC", "FLT"},
    {
        {"WAVE", F_ENUM, 0, 6, 0, N_ANALOG_WAVE, 0},
        {"DTN", F_INT, 0, 127, 10, 0, "ct"},
        {"MIX", F_PCT, 0, 127, 64, 0, 0},
        {"NOIS", F_PCT, 0, 127, 0, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 90, 0, 0},
        {"RES", F_PCT, 0, 127, 30, 0, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},
        {"KTR", F_PCT, 0, 127, 64, 0, 0},
    },
    ANALOG_PRESETS, sizeof(ANALOG_PRESETS) / sizeof(*ANALOG_PRESETS), 1, analog_note_on, analog_render,
    0xF986, {P_E4, P_E5, P_ATK, P_REL},
};
