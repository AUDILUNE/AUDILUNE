/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* VOICE: formant oscillator, sung vowels from the keyboard. 4 voices.
 * After D. H. Klatt, "Software for a cascade/parallel formant synthesizer",
 * JASA 67 (1980): a glottal source (derivative of a Rosenberg (1971) pulse,
 * no DC) into four two-pole resonators in cascade, F1 -> F2 -> F3 -> F4.
 * Design follows klattsch by Tony Gies (MIT, design reference only, no code
 * copied). Formant data from Klatt (1980) and Hillenbrand et al. (1995).
 *
 *   source  rising half-sine while opening (Tp), falling quarter-sine while
 *           closing (Tn), 0 while closed; the closure step goes through blep()
 *   BUZZ    open quotient 0.9 .. 0.35 and a one-pole spectral tilt
 *   BREATH  noise, louder while the glottis is open, mixed before the
 *           resonators; at the top the voicing fades out (whisper)
 *   F1..F4  y = A x + B y1 + C y2, C = -r^2, B = 2 r cos(2 pi F / FS),
 *           A = 1 - B - C (unity gain at DC), Q30 coefficients per block;
 *           F4 is fixed at 3300 Hz and moves only with SHIFT / key tracking
 *   PRES    out = y + k (y - y[-1]), a first-difference shelf; k follows BUZZ
 *
 * Voice state: ph[0] glottal phase, ph[1..2] F4 y1 / y2, s[0..5] F1..F3
 * y1 / y2, s[6] tilt, s[7] TALK progress (Q24, bits 0..24) and the note's
 * random vowel (bits 25..31). Formant key tracking is fixed at 30 / 127. */
#define FMT_FOLOW 30
#define FMT_TALK_MASK 0x01FFFFFF

/* formants (Hz) of A E I O U: adult male means, Hillenbrand et al., JASA 97
 * (1995) (/a/ hod, /E/ head, /i/ heed, /o/ hoed, /u/ who'd); bandwidths (Hz)
 * in the range of Klatt (1980) and of measured vowel bandwidths */
static const uint16_t VOWEL_F[5][3] = {
    {768, 1333, 2522}, {580, 1799, 2605}, {342, 2322, 3000}, {497, 910, 2459}, {378, 997, 2343},
};
static const uint8_t VOWEL_B[5][3] = {
    {90, 100, 160}, {70, 100, 170}, {50, 120, 200}, {70, 80, 150}, {60, 80, 150},
};
/* 2^(k/12), Q16, k = 0..12 */
static const uint32_t SEMI_Q16[13] = {
    65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218, 116772, 123715, 131072,
};

/* 2^(st16 / 192) in Q16 (st16 in 1/16 semitone), clamped to 1/16 .. 16.
 * Divisions by constants are written as multiplies: -Os keeps them as divides */
static uint32_t fmt_ratio(int32_t st16)
{
    int32_t o, k;
    uint32_t r;
    st16 = clamp(st16, -4 * 192, 4 * 192 - 1) + 4 * 192;   /* 0 .. 8 octaves */
    o = (st16 * 2731) >> 19;                            /* / 192, exact for 0 .. 1535 */
    k = st16 - o * 192;
    r = SEMI_Q16[k >> 4] + (((SEMI_Q16[(k >> 4) + 1] - SEMI_Q16[k >> 4]) * (uint32_t)(k & 15)) >> 4);
    return o >= 4 ? r << (o - 4) : r >> (4 - o);
}

/* the three formants of vowel position pos (0..127 << 8, A E I O U), Hz * 16 */
static void vowel_at(int32_t pos, int32_t *f, int32_t *b)
{
    int32_t x = (clamp(pos, 0, 127 << 8) * 33027) >> 20, i = x >> 8, fr = x & 255, j;   /* * 4 / 127 */
    if (i >= 4) {
        i = 3;
        fr = 256;
    }
    for (j = 0; j < 3; j++) {
        f[j] = (VOWEL_F[i][j] << 4) + (((VOWEL_F[i + 1][j] - VOWEL_F[i][j]) * fr) >> 4);
        b[j] = (VOWEL_B[i][j] << 4) + (((VOWEL_B[i + 1][j] - VOWEL_B[i][j]) * fr) >> 4);
    }
}
typedef struct { int32_t a, b, c; } fres_t;
static int32_t formant_nz = 0x2545F491;                 /* breath noise, one xorshift for all voices */

/* AUDILUNE DECtalk / Klatt Phoneme Table (F1, F2, F3 Hz; voice 0..127; noise 0..127; flags: 1=burst, 2=pause) */
typedef struct { uint16_t f1, f2, f3; uint8_t vox, nz, flg; } vox_ph_t;
static const vox_ph_t VOX_PH[24] = {
    {0, 0, 0, 0, 0, 2},          /* 0: _ pause */
    {750, 1250, 2500, 127, 8, 0},/* 1: AA (dark/star) */
    {660, 1720, 2410, 127, 8, 0},/* 2: AE (static/matter) */
    {530, 1840, 2480, 127, 8, 0},/* 3: EH (echo/event) */
    {390, 1990, 2550, 127, 8, 0},/* 4: IH (signal/drift) */
    {270, 2290, 3010, 127, 8, 0},/* 5: IY (deep/field) */
    {570, 840, 2410, 127, 8, 0}, /* 6: AO (orbit/void) */
    {440, 1020, 2240, 127, 8, 0},/* 7: UH (lunar/pulse) */
    {300, 870, 2240, 127, 8, 0}, /* 8: UW (lumen/tube) */
    {520, 1190, 2390, 127, 8, 0},/* 9: AX (schwa/a) */
    {490, 1350, 1690, 127, 8, 0},/* 10: ER (earth/thermal) */
    {310, 1050, 2880, 120, 4, 0},/* 11: L (light/low) */
    {350, 1300, 1600, 120, 4, 0},/* 12: R (ray/ring) */
    {280, 900, 2200, 115, 0, 0}, /* 13: M (mass/moon) */
    {280, 1700, 2600, 115, 0, 0},/* 14: N (node/null) */
    {320, 4800, 5600, 0, 110, 0},/* 15: S (solar/space) */
    {320, 2600, 3400, 0, 115, 0},/* 16: SH (shadow/shift) */
    {350, 1500, 2400, 0, 90, 0}, /* 17: F (flux/phase) */
    {350, 1800, 2700, 0, 80, 0}, /* 18: TH (threshold/ether) */
    {500, 1500, 2500, 20, 95, 0},/* 19: HH (horizon/hollow) */
    {250, 2100, 2900, 70, 75, 0},/* 20: Z (zero/zone) */
    {250, 900, 2100, 40, 60, 1}, /* 21: P/B plosive */
    {300, 1800, 2600, 40, 85, 1},/* 22: T/D plosive */
    {300, 1400, 2200, 40, 80, 1} /* 23: K/G plosive */
};
static uint8_t vox_seq[16 + 16], vox_len, vox_idx, vox_tmr;

UI_SMALL static const uint8_t *vox_slot_xip(uint32_t slot)
{
    static const uint8_t DEF_PH[4 + 4][6 + 6] = {
        {11,8,14,10,0,15,4,23,14,9,11,0}, /* LUNAR SIGNAL */
        {20,5,12,6,0,19,6,12,1,20,14,0},  /* ZERO HORIZON */
        {16,2,22,6,0,15,2,22,4,23,0,0},   /* SHADOW STATIC */
        {23,6,14,22,9,13,0,17,5,11,22,0}, /* QUANTUM FIELD */
        {2,15,22,12,6,0,22,12,4,17,22,0}, /* ASTRO DRIFT */
        {15,6,11,10,0,5,23,11,4,21,15,0}, /* SOLAR ECLIPSE */
        {22,10,13,9,11,0,21,7,11,15,0,0}, /* THERMAL PULSE */
        {14,7,11,0,17,5,11,22,0,0,0,0}    /* NULL FIELD */
    };
#if defined(ST_XIP_PTR) && FELUCCA_FLASH
    uint32_t len = 0;
    const uint8_t *xp = flash_ok ? (const uint8_t *)st_xip(OBJ_VOXBANK, &len) : 0;
    if (xp && len >= 512u && xp[(slot & 7u) * 64u]) return xp + (slot & 7u) * 64u;
#endif
    return DEF_PH[slot & 7u];
}

/* Genre-locked combinatorial poetic/scientific phrase builder (4,000+ combinations, 0 B SRAM) */
static void vox_build_phrase(uint32_t vi, uint32_t slot, uint32_t custom, uint32_t genre, uint32_t seed)
{
    static const uint8_t ADJ[3 + 3][2 + 2][4 + 4] = {
        {{11,8,14,10,0},{19,6,11,6,0},{15,4,11,3,14,22,0},{2,15,22,12,6,0}},   /* AMBIENT: lunar, hollow, silent, astro */
        {{16,2,22,6,0},{3,23,6,0},{2,13,21,10,0},{14,6,23,22,10,14,0}},       /* DOWNTMP: shadow, echo, amber, nocturn */
        {{15,2,22,4,23,0},{17,12,2,23,22,9,11,0},{20,5,12,6,0},{21,4,14,12,5,0}},/* GLITCH: static, fractal, zero, binary */
        {{23,6,14,22,9,13,0},{22,10,13,9,11,0},{15,6,11,10,0},{13,2,23,14,3,22,0}},/* PUNCHY: quantum, thermal, solar, magnet */
        {{18,12,3,16,6,11,22,0},{3,14,22,12,9,21,5,0},{11,8,13,9,14,0},{17,11,7,23,15,0}},/* EXPERIM: threshold, entropy, lumen, flux */
        {{23,1,12,21,9,14,0},{13,3,22,9,11,0},{14,7,11,0},{21,12,6,22,6,14,0}} /* INDUST: carbon, metal, null, proton */
    };
    static const uint8_t NOUN[3 + 3][2 + 2][4 + 4] = {
        {{19,6,12,1,20,14,0},{18,10,0},{22,1,5,22,0},{12,4,14,0}},            /* horizon, ether, tide, ring */
        {{22,12,4,17,22,0},{14,6,22,0},{6,12,21,4,22,0},{17,6,11,22,0}},       /* drift, node, orbit, fault */
        {{15,4,23,14,9,11,0},{21,7,11,15,0},{23,6,22,0},{17,3,20,0}},         /* signal, pulse, code, phase */
        {{17,5,11,22,0},{23,6,12,0},{13,2,15,0},{1,12,23,0}},                 /* field, core, mass, arc */
        {{15,21,3,23,22,12,9,0},{17,12,3,23,0},{1,15,18,9,12,0},{14,3,21,7,11,9,0}},/* spectra, freq, aether, nebula */
        {{12,5,2,23,22,10,0},{22,10,21,4,14,0},{15,22,5,11,0},{23,12,4,22,0}} /* reactor, turbine, steel, grid */
    };
    static const uint8_t VERB[2 + 2][4 + 4] = {
        {22,12,4,17,22,15,0},{15,16,4,17,22,15,0},{21,7,11,15,4,20,0},{17,3,20,4,20,0} /* drifts, shifts, pulses, phases */
    };
    uint32_t g = genre % 6u, a = seed & 3u, n = (seed >> 2) & 3u, vb = (seed >> 4) & 3u, p = 0, j;
    const uint8_t *w;
    (void)vi;
    if (custom) {
        w = vox_slot_xip(slot);
        for (j = 0; j < 31u && w[j]; j++) vox_seq[p++] = w[j];
        vox_len = (uint8_t)p;
        return;
    }
    w = ADJ[g][a]; for (j = 0; w[j] && p < 30u; j++) vox_seq[p++] = w[j];
    vox_seq[p++] = 0;
    w = NOUN[g][n]; for (j = 0; w[j] && p < 30u; j++) vox_seq[p++] = w[j];
    vox_seq[p++] = 0;
    w = VERB[vb]; for (j = 0; w[j] && p < 30u; j++) vox_seq[p++] = w[j];
    vox_len = (uint8_t)p;
}

/* Klatt resonator coefficients (Q30) for F, BW in Hz * 16 */

static void fres_coef(fres_t *r, uint32_t f16, uint32_t bw16)
{
    int32_t s = sine_i(f16 * 3044u);                    /* sin(pi F / FS), Q15 (2^32 / (2 FS) / 16) */
    int32_t x = (int32_t)(bw16 * 4781u);                /* pi BW / FS, Q30 (<= 0.2) */
    int32_t x2 = (int32_t)(((int64_t)x * x) >> 30), x3 = (int32_t)(((int64_t)x2 * x) >> 30);
    int32_t x4 = (int32_t)(((int64_t)x3 * x) >> 30);
    int32_t e = (1 << 30) - x + (x2 >> 1) - (int32_t)(((int64_t)x3 * 178956971) >> 30)   /* exp(-x): x^3 / 6, */
                + (int32_t)(((int64_t)x4 * 44739243) >> 30);                            /* x^4 / 24 */
    int32_t cs = (1 << 30) - 2 * s * s;                 /* cos(2 pi F / FS) = 1 - 2 sin^2, Q30 */
    r->b = (int32_t)(((int64_t)e * cs) >> 29);          /* 2 r cos */
    r->c = -(int32_t)(((int64_t)e * e) >> 30);          /* -r^2 */
    r->a = (1 << 30) - r->b - r->c;

}

static uint8_t audilune_vox_genre[NTRK];
static void formant_note_on(track_t *t, voice_t *v)
{
    uint32_t vi = (uint32_t)(v - t->v) & 3u;
    formant_nz ^= formant_nz << 13;                     /* a random vowel for this note (RAND) */
    formant_nz ^= (int32_t)((uint32_t)formant_nz >> 17);
    formant_nz ^= formant_nz << 5;
    v->s[7] = (int32_t)(((uint32_t)formant_nz >> 25) << 25);   /* TALK starts over, random vowel kept */
    if (t->preset >= 7u) {
        uint32_t ti = (uint32_t)(t - trk);
        uint32_t g = ti < NTRK ? audilune_vox_genre[ti] : audilune_vox_genre[song.sel % NTRK];
        uint32_t slot = t->preset >= 10u ? (uint32_t)(t->preset - 10u) : 0u;
        vox_build_phrase(vi, slot, t->preset >= 10u, g, (uint32_t)formant_nz + (uint32_t)v->note * 17u);
        vox_idx[vi] = 0;
        vox_tmr[vi] = 0;
    } else {
        vox_len[vi] = 0;
    }
    if (!v->env && !v->env_out) {                       /* a fresh voice (not a retrigger): from rest */
        uint32_t i;
        v->ph[0] = 0;                                   /* the opening starts at zero: no click */
        v->ph[1] = v->ph[2] = 0;


        for (i = 0; i < 7u; i++)
            v->s[i] = 0;
    }
}

static void formant_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    fres_t r1, r2, r3, r4;
    int32_t fa[3], ba[3], fb[3], bb[3], k, j, buzz, oq, tilt, gv, gn, og, ogk;
    uint32_t inc = m->inc, i, te, tp, rp, rn, ratio, bws, f0;
    uint32_t ph = v->ph[0];
    int32_t y1 = v->s[0], y2 = v->s[1], y3 = v->s[2], y4 = v->s[3], y5 = v->s[4], y6 = v->s[5];
    int32_t y7 = (int32_t)v->ph[1], y8 = (int32_t)v->ph[2], lp = v->s[6], nst = formant_nz;

    /* vowel: VOWEL -> VOWL2 over TALK after note-on; ENV / LFO -> FLT move both */
    k = 0;
    if (p[P_E2]) {
        uint32_t g = ((uint32_t)v->s[7] & FMT_TALK_MASK) + ENV_LIN[p[P_E2] & 127];
        g = g > (1u << 24) ? 1u << 24 : g;
        v->s[7] = (int32_t)(((uint32_t)v->s[7] & ~(uint32_t)FMT_TALK_MASK) | g);
        k = (int32_t)(g >> 9);                          /* Q15 */
        k = (k * (65536 - k)) >> 15;                    /* ease out: the mouth opens fast, then settles */
    }
    uint32_t vi = (uint32_t)(v - t->v) & 3u, vox_on = (t->preset >= 7u && vox_len[vi] > 0u);
    int32_t vox_gv = 32767, vox_gn = 0, vox_burst = 0;
    if (vox_on) {
        uint32_t step_b = (uint32_t)(18u - (p[P_E2] >> 3));
        uint32_t cur_i = vox_idx[vi] < vox_len[vi] ? vox_idx[vi] : 0u;
        uint32_t nxt_i = (cur_i + 1u) < vox_len[vi] ? (cur_i + 1u) : cur_i;
        const vox_ph_t *pa = &VOX_PH[vox_seq[vi][cur_i] % 24u], *pb = &VOX_PH[vox_seq[vi][nxt_i] % 24u];
        int32_t frac = step_b ? (int32_t)(((uint32_t)vox_tmr[vi] << 15) / step_b) : 0;
        if (frac > 32767) frac = 32767;
        fa[0] = ((int32_t)pa->f1 + (((int32_t)pb->f1 - (int32_t)pa->f1) * frac >> 15)) << 4;
        fa[1] = ((int32_t)pa->f2 + (((int32_t)pb->f2 - (int32_t)pa->f2) * frac >> 15)) << 4;
        fa[2] = ((int32_t)pa->f3 + (((int32_t)pb->f3 - (int32_t)pa->f3) * frac >> 15)) << 4;
        fb[0] = fa[0]; fb[1] = fa[1]; fb[2] = fa[2];
        ba[0] = 85 << 4; ba[1] = 105 << 4; ba[2] = 165 << 4;
        bb[0] = ba[0]; bb[1] = ba[1]; bb[2] = ba[2];
        vox_gv = (pa->flg & 2u) ? 0 : (int32_t)pa->vox * 258;
        vox_gn = (pa->flg & 2u) ? 0 : (int32_t)pa->nz * 258;
        if ((pa->flg & 1u) && vox_tmr[vi] < 2u) vox_burst = 14000;
        if (++vox_tmr[vi] >= step_b) {
            vox_tmr[vi] = 0;
            vox_idx[vi] = (uint8_t)((cur_i + 1u) % vox_len[vi]);
        }
    } else {   /* RAND: both vowels move towards this note's random vowel (0 = off, 127 = fully random) */
        int32_t rv = (int32_t)(((uint32_t)v->s[7] >> 25) << 8), v0 = p[P_E0] << 8, v1 = p[P_E1] << 8;
        v0 += ((rv - v0) * p[P_E7] * 258) >> 15;
        v1 += ((rv - v1) * p[P_E7] * 258) >> 15;
        vowel_at(v0 + m->cutoff, fa, ba);
        vowel_at(v1 + m->cutoff, fb, bb);
    }
    /* SHIFT (semitones) and FOLOW (key tracking around C4, fixed), one ratio for all four */
    ratio = fmt_ratio(p[P_E3] * 16 + ((FMT_FOLOW * (v->pitch_cur - 60 * 16) * 516) >> 16));   /* FOLOW / 127 */
    bws = fmt_ratio((64 - p[P_E6]) * 4);                /* Q: bandwidth x2.5 .. x0.4 */
    f0 = (uint32_t)(((uint64_t)inc * 705600u) >> 32);   /* F0 in Hz * 16 */
    for (j = 0; j < 3; j++) {
        int32_t f = fa[j] + (((fb[j] - fa[j]) * k) >> 15), b = ba[j] + (((bb[j] - ba[j]) * k) >> 15);
        f = (int32_t)(((int64_t)f * ratio) >> 16);
        b = (int32_t)(((((int64_t)b * ratio) >> 16) * bws) >> 16);
        if (j == 0 && (uint32_t)f < f0 + (f0 >> 4) && f > 0) {   /* sung high: F1 follows F0 (sopranos do), */
            b = (int32_t)(((int64_t)b * (int32_t)(f0 + (f0 >> 4))) / f);   /* same Q: no boom on the fundamental */
            f = (int32_t)(f0 + (f0 >> 4));
        }
        fa[j] = clamp(f, 60 * 16, 7000 * 16);
        ba[j] = clamp(b, 20 * 16, 1500 * 16);
    }
    fres_coef(&r1, (uint32_t)fa[0], (uint32_t)ba[0]);
    fres_coef(&r2, (uint32_t)fa[1], (uint32_t)ba[1]);
    fres_coef(&r3, (uint32_t)fa[2], (uint32_t)ba[2]);
    fres_coef(&r4, (uint32_t)clamp((int32_t)((3300u * 16u * (uint64_t)ratio) >> 16), 60 * 16, 7000 * 16),
              (uint32_t)clamp((int32_t)((((250u * 16u * (uint64_t)ratio) >> 16) * bws) >> 16), 20 * 16, 1500 * 16));

    /* source: BUZZ (+ SHP modulation, + velocity) -> open quotient and tilt */
    buzz = clamp((p[P_E4] << 8) + m->shape - (64 << 8) + (v->vel - 96) * 40, 0, 127 << 8);
    oq = 58982 - ((buzz * 284) >> 8);                   /* Q16: 0.90 .. 0.35 */
    tilt = 2500 + ((buzz * 109) >> 8);                  /* one-pole coefficient Q14: dark .. open */
    te = (uint32_t)oq << 16;                            /* closure (end of the open phase) */
    tp = (te >> 8) * 166u;                              /* opening: 65 % of it, closing 35 % */
    rp = (1u << 30) / ((tp >> 16) | 1u);
    rn = (1u << 30) / (((te - tp) >> 16) | 1u);
    gv = vox_on ? vox_gv : (32767 - p[P_E5] * p[P_E5] * 2);   /* BREATH: voicing fades at the top */
    gn = vox_on ? clamp(vox_gn + (p[P_E5] * 128), 0, 32767) : (p[P_E5] * 258);   /* noise: Q15, up to 1.0 */
    og = 13000 - ((p[P_E6] - 64) * 50);                 /* output: narrower bands ring louder */
    og = (int32_t)(((int64_t)og * fmt_ratio((60 * 16 - v->pitch_cur) / 4)) >> 16);   /* 1.5 dB / oct: low notes are sparse pulses */
    ogk = (og >> 2) * (4 + (buzz >> 10));               /* presence k = 1 .. 4.9 */

    for (i = 0; i < n; i++) {
        int32_t e, x, a, s;
        if (ph < tp)                                    /* opening: + sin(pi u) * Tn / Tp */
            e = (sine_i(((ph >> 16) * rp) << 1) * 17644) >> 15;
        else if (ph < te)                               /* closing: - sin(pi / 2 u) */
            e = -sine_i(((ph - tp) >> 16) * rn);
        else
            e = 0;
        if (vox_burst && i < 4u) e += vox_burst;

        e += blep(ph - te, inc) >> 1;                   /* the closure step, band-limited */
        lp += ((e - lp) * tilt) >> 14;
        x = (lp * gv) >> 15;
        if (gn) {
            int32_t nz = (((int32_t)(noise32(&nst) >> 16) - 32768) * gn) >> 15;
            x += ph < te ? nz : nz >> 1;                /* aspiration follows the glottal opening */
        }
        /* F1 -> F2 -> F3 -> F4, Q30, rounded; the signal x 64 inside: the rounding
         * dead band of F1 (+-0.5 / A, ~340 at 270 Hz) stays below 6 LSB */
        a = (int32_t)(((int64_t)r1.a * (x << 6) + (int64_t)r1.b * y1 + (int64_t)r1.c * y2 + (1 << 29)) >> 30);
        y2 = y1;
        y1 = a;
        a = (int32_t)(((int64_t)r2.a * a + (int64_t)r2.b * y3 + (int64_t)r2.c * y4 + (1 << 29)) >> 30);
        y4 = y3;
        y3 = a;
        a = (int32_t)(((int64_t)r3.a * a + (int64_t)r3.b * y5 + (int64_t)r3.c * y6 + (1 << 29)) >> 30);
        y6 = y5;
        y5 = a;
        a = (int32_t)(((int64_t)r4.a * a + (int64_t)r4.b * y7 + (int64_t)r4.c * y8 + (1 << 29)) >> 30);
        y8 = y7;
        y7 = clamp(a, -(1 << 28), 1 << 28);             /* int32 state stays far from overflow */
        s = clamp((int32_t)(((int64_t)y7 * og + (int64_t)(y7 - y8) * ogk) >> 21), -200000, 200000);
        a = s < 0 ? -s : s;
        if (a > 24000) {                                /* soft knee: only peaks saturate */
            a = 24000 + (softclip((a - 24000) * 2) >> 1);
            s = s < 0 ? -a : a;
        }
        ph += inc;
        out[i] += mulq15(mulq15(s, amp_at(m, i)), VOICE_FS) << 1;
    }
    v->ph[0] = ph;
    v->ph[1] = (uint32_t)y7;
    v->ph[2] = (uint32_t)y8;
    formant_nz = nst;
    v->s[0] = y1;
    v->s[1] = y2;
    v->s[2] = y3;
    v->s[3] = y4;
    v->s[4] = y5;
    v->s[5] = y6;
    v->s[6] = lp;
}

static const preset_t FORMANT_PRESETS[] = {
    /* VOWEL VOWL2 TALK SHIFT | BUZZ BREATH Q RAND ; vowels A E I O U over 0..127 */
    {"CHOIR AAH", {0, 0, 0, 0, 40, 22, 60, 0}, {85, 90, 115, 95}, 0, 0, FX(0, 60, 10, 70)},
    {"SOUL OOH", {110, 0, 0, 0, 30, 30, 55, 0}, {70, 90, 115, 90}, 0, 0, FX(0, 60, 10, 70)},
    {"TALKBOX", {32, 95, 60, 0, 100, 4, 75, 0}, {4, 70, 110, 40}, 0, 1, FX(10, 15, 20, 25),
     XP(P_GLIDE + 1, 60, P_LD_PIT + 1, 2, P_LRATE + 1, 89)},
    {"WOW BASS", {95, 0, 68, 0, 100, 0, 80, 0}, {0, 70, 70, 30}, 0, 1, FX(10, 0, 0, 6), XP(P_TRANS + 1, -12)},
    /* SLOOP 2.5: a reedy harmonica, a hand tremolo that comes in on long notes */
    {"HARMONICA", {64, 0, 0, 0, 127, 20, 70, 0}, {4, 70, 115, 25}, 0, 1, FX(10, 10, 10, 22),
     XP(P_LD_AMP + 1, 18, P_LRATE + 1, 80, P_LFADE + 1, 60)},
    {"ROBOT", {40, 40, 0, 0, 127, 0, 110, 0}, {0, 60, 110, 20}, 0, 1, FX(10, 0, 20, 15), XP(P_GLIDE + 1, 20)},
    {"VOX PAD", {20, 100, 110, 0, 50, 30, 60, 10}, {80, 90, 118, 90}, 0, 0, FX(0, 60, 15, 70)},
};

static const engine_t ENG_FORMANT = {
    "VOICE", {"VOWL", "TONE"},
    {
        {"VOWL", F_INT, 0, 127, 0, 0, 0},
        {"VOWL2", F_INT, 0, 127, 64, 0, 0},
        {"TALK", F_TIME, 0, 127, 0, 0, 0},
        {"SHIFT", F_SEMI, -12, 12, 0, 0, 0},
        {"BUZZ", F_PCT, 0, 127, 64, 0, 0},
        {"BRTH", F_PCT, 0, 127, 10, 0, 0},
        {"Q", F_PCT, 0, 127, 64, 0, 0},
        {"RAND", F_PCT, 0, 127, 0, 0, 0},
    },
    FORMANT_PRESETS, sizeof(FORMANT_PRESETS) / sizeof(FORMANT_PRESETS[0]), 0, formant_note_on, formant_render,
    0xF81F, {P_E0, P_E4, P_E5, P_E2}, 4,
};
