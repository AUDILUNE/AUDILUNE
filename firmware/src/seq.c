/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Adapted for AUDILUNE Firmware (Block 4: 6 Tracks x 128 Steps & Generative Engine) */
/* Keyboard and its layers, scale and chords, arpeggiator, note repeat, sequencer, recording and
 * transport. Runs in the audio ISR, once per CTL-sample block (events_block), and ends in
 * trk_note_on / trk_note_off / drum_on: engines never see where a note came from. */
static const uint16_t SCALE_MASK[] = {
    0xFFF,                                   /* CHR */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),   /* MAJ */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),   /* MIN */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),   /* DOR */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),   /* MIX */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 7) | (1 << 9),                          /* PEN */
    (1 << 0) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 10),                         /* MPEN */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 11),   /* HARM */
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),   /* PHRY */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 7) | (1 << 9) | (1 << 11),   /* LYD */
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 8) | (1 << 10),   /* LOC */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),   /* MEL (ascending) */
    (1 << 0) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 10),              /* BLUES (minor) */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 8) | (1 << 10),              /* WHOLE */
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 4) | (1 << 6) | (1 << 7) | (1 << 9) | (1 << 10), /* DIMHW */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 8) | (1 << 9) | (1 << 11), /* DIMWH */
};
#define NSCALES (sizeof SCALE_MASK / sizeof SCALE_MASK[0])
#define SEQ_NONE 0xFFFFFFFFu

#define KB_SILENT 255u
static uint32_t kb_prev;
/* per key: what its press started, so its release ends the same (whatever the layer or track is now) */
enum { KS_NONE, KS_NOTE, KS_DRUM, KS_ROLL, KS_ERASE, KS_FX, KS_UI, KS_MOD };
static uint8_t kb_kind[27], kb_trk[27], kb_n[27], kb_nt[27][4], kb_root[27];
static int8_t kb_oct[27];
static uint8_t last_note = 60;
static uint8_t pen_n = 1, pen_note[4] = {60};
static uint8_t pen_lane;
static uint32_t mi_pen[4];
#define STEP_IN_Q 32u
static volatile uint16_t step_in_q[STEP_IN_Q];
static volatile uint32_t step_in_w;
static volatile uint8_t step_in_lost;
static uint32_t step_in_r;
static volatile uint8_t pc_req[NTRK];
static volatile uint8_t pc_bank[NTRK];
static void step_in_push(uint32_t ev)
{
    if (step_in_w - step_in_r >= STEP_IN_Q) {
        step_in_lost = 1;
        return;
    }
    step_in_q[step_in_w % STEP_IN_Q] = (uint16_t)ev;
    step_in_w++;
}
static volatile uint8_t transport_req;
static volatile uint8_t panic_req;
static volatile uint8_t arp_keep_req;

static uint32_t trk_index(const track_t *t) { return (uint32_t)(t - trk); }

static uint32_t trk_midi_ch(uint32_t i)
{
    int32_t c = i < NPART ? song.g[G_CH1 + i] : song.g[G_DRCH];
    return c >= 1 && c <= 16 ? (uint32_t)c - 1u : 0xFFu;
}
static void midi_out_note(uint32_t ch, uint32_t note, uint32_t vel)
{
    if (ch > 15u)
        return;
    midi_out_event(vel ? 0x09u | (0x90u | ch) << 8 | (note & 127u) << 16 | (vel & 127u) << 24
                       : 0x08u | (0x80u | ch) << 8 | (note & 127u) << 16);
}
static uint32_t mo_set[NTRK][4];
static uint8_t mo_ch[NTRK];
static void seq_out_off(const track_t *t, uint32_t note)
{
    uint32_t i = trk_index(t) % NTRK;
    if (note > 127u || !(mo_set[i][note >> 5] & (1u << (note & 31u))))
        return;
    mo_set[i][note >> 5] &= ~(1u << (note & 31u));
    midi_out_note(mo_ch[i], note, 0);
}
static void seq_out_track_off(const track_t *t);
static uint8_t mo_any;
static void seq_out_on(const track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t i = trk_index(t) % NTRK;
    if (!song.g[G_MIDI] || note > 127u)
        return;
    seq_out_off(t, note);
    if (trk_midi_ch(i) != mo_ch[i]) {
        seq_out_track_off(t);
        mo_ch[i] = (uint8_t)trk_midi_ch(i);
    }
    if (mo_ch[i] > 15u)
        return;
    mo_set[i][note >> 5] |= 1u << (note & 31u);
    mo_any = 1;
    midi_out_note(mo_ch[i], note, (vel ? vel & 127u : 1u));
}
static void seq_out_track_off(const track_t *t)
{
    uint32_t i = trk_index(t) % NTRK, w, b;
    for (w = 0; w < 4u; w++)
        for (b = 0; mo_set[i][w]; b++)
            if (mo_set[i][w] & (1u << b)) {
                mo_set[i][w] &= ~(1u << b);
                midi_out_note(mo_ch[i], (w * 32u + b), 0);
            }
}
static void seq_out_all_off(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++)
        seq_out_track_off(&trk[i]);
}

static uint32_t scale_mask(const track_t *t)
{
    return SCALE_MASK[clamp(t->p[P_SCALE], 0, NSCALES - 1)];
}

/* ---------------------------------------------------------- layers --- */
enum { LY_PLAY, LY_FX, LY_ERASE, LY_ROLL, LY_STEP, LY_SCALE, LY_MIX, LY_SONG, LY_COUNT };
static uint32_t ly_bit[LY_COUNT];
static uint32_t dyn_bit[2];
static volatile uint8_t ly_lock = LY_PLAY;
static uint32_t layer_buttons(void) { return fm1_in.buttons | (ly_lock != LY_PLAY ? ly_bit[ly_lock % LY_COUNT] : 0u); }
static uint32_t layer_now(void)
{
    uint32_t b = layer_buttons(), l;
    for (l = LY_FX; l < LY_COUNT; l++)
        if (b & ly_bit[l])
            return l;
    return LY_PLAY;
}
#define KB_GRID LY_COUNT
static volatile uint8_t kb_grid;
#define LKQ 16u
static volatile uint16_t lk_q[LKQ];
static volatile uint32_t lk_w, lk_r;
static void lk_push(uint32_t layer, uint32_t k, uint32_t down)
{
    if (lk_w - lk_r < LKQ) {
        lk_q[lk_w % LKQ] = (uint16_t)(layer << 8 | down << 7 | k);
        RING_PUBLISH();
        lk_w++;
    }
}
/* ------------------------------------------------------------- keys --- */
static int kb_scaled(const track_t *t)
{
    if (is_drum(t))
        return 0;
    if (ENGINES[t->eng_req % NENGINES] == &ENG_SAMPLE && drum_set() >= 0 &&
        (uint32_t)t->p[P_E0] % SMP_NSETS == (uint32_t)drum_set())
        return 0;
#if FELUCCA_SLICE
    if (ENGINES[t->eng_req % NENGINES] == &ENG_SLICE)
        return 0;
#endif
    return 1;
}
static uint32_t kb_pitch(const track_t *t, uint32_t k, int32_t oct)
{
    static const int8_t DEGREE[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};
    int32_t n = 53 + (int32_t)k;
    if (t->p[P_QUANT] == 1 && !t->p[P_CHORD]) {
        uint32_t mask = scale_mask(t), guard = 12;
        n += 12 * oct + t->p[P_TRANS];
        while (guard-- && !((mask >> (uint32_t)((n - t->p[P_ROOT] + 120) % 12)) & 1u))
            n--;
        return (uint32_t)clamp(n, 0, 127);
    }
    if (t->p[P_QUANT] == 2 || t->p[P_CHORD]) {
        uint32_t mask = t->p[P_CHORD] && !t->p[P_SCALE] ? SCALE_MASK[2] : scale_mask(t), i;
        int32_t count = 0, degree = DEGREE[n % 12], wo;
        if (degree < 0)
            return KB_SILENT;
        degree += (n / 12 - 5) * 7;
        for (i = 0; i < 12u; i++)
            count += (mask >> i) & 1u;
        wo = degree / count;
        degree %= count;
        if (degree < 0) {
            degree += count;
            wo--;
        }
        for (i = 0; i < 12u; i++)
            if ((mask >> i) & 1u) {
                if (!degree)
                    break;
                degree--;
            }
        n = 60 + t->p[P_ROOT] + 12 * wo + (int32_t)i;
    }
    return (uint32_t)clamp(n + 12 * oct + t->p[P_TRANS], 0, 127);
}
static uint32_t kb_map(const track_t *t, uint32_t k)
{
    if (is_drum(t))
        return LANE_NOTE[lane_of_key(k)];
    if (ENGINES[t->eng_req % NENGINES] == &ENG_SAMPLE && drum_set() >= 0 &&
        (uint32_t)t->p[P_E0] % SMP_NSETS == (uint32_t)drum_set())
        return (uint32_t)clamp(36 + 12 * song.octave + (int32_t)k, 0, 127);
#if FELUCCA_SLICE
    if (ENGINES[t->eng_req % NENGINES] == &ENG_SLICE)
        return (uint32_t)clamp(SLC_BASE + t->p[P_ROOT] + 12 * song.octave + (int32_t)k, 0, 127);
#endif
    return kb_pitch(t, k, song.octave);
}

/* ------------------------------------------- AUDILUNE forward declarations --- */
#ifndef CHAIN_MAX
#define CHAIN_MAX 16u
#endif
static int8_t live_sec = -1;
static int8_t live_req = -1;
static uint8_t srec;
static uint8_t srec_done;
static uint8_t srec_n;
static struct { uint8_t scene, bars; } srec_e[CHAIN_MAX];
static uint8_t chain_n;
static uint8_t chain_i;
static uint8_t chain_bars;
static uint8_t chain_sec[CHAIN_MAX];

static int live_has(const track_t *t, uint32_t note);
static uint32_t section_bars(uint32_t s);
static void seq_release(track_t *t);
static uint32_t step_vel(const step_t *s, uint32_t i);
static void seq_stop(void);
static void seq_reset_tracks(uint32_t phase);
static void live_block(void);
static void click_tick(void);
static void srec_stop(void);

static const int8_t CHORD_DEG[3 + 3][2 + 2] = {
    {0, -1, -1, -1},                     /* OFF */
    {0, 2, 4, -1},                       /* TRIAD: 1 3 5 */
    {0, 2, 4, 6},                        /* 7TH: 1 3 5 7 */
    {0, 2, 6, 8},                        /* 9TH: 1 3 7 9 */

    {0, 3, 4, -1},                       /* SUS4: 1 4 5 */
    {0, -1, -1, -1},                     /* POWER: 1 5 8 */
};
static uint32_t chord_notes(const track_t *t, uint32_t n, uint8_t *c)
{
    uint32_t type = (uint32_t)clamp(t->p[P_CHORD], 0, 5), mask = t->p[P_SCALE] ? scale_mask(t) : SCALE_MASK[2];
    uint32_t k = 0, j;
    if (type == 5u) {
        static const uint8_t PW[3] = {0, 7, 12};

        for (j = 0; j < 3u; j++)
            if (n + PW[j] < 128u)
                c[k++] = (uint8_t)(n + PW[j]);
        return k;
    }
    for (j = 0; j < 4u && CHORD_DEG[type][j] >= 0; j++) {
        int32_t m = (int32_t)n, d = CHORD_DEG[type][j], guard = 48;
        while (d > 0 && guard--) {
            m++;
            if ((mask >> (uint32_t)((m - t->p[P_ROOT] + 120) % 12)) & 1u)
                d--;
        }
        if (m < 128)
            c[k++] = (uint8_t)m;
    }
    return k;
}

enum { CM_MINOR = 1, CM_SEVEN = 2, CM_SUS4 = 4, CM_NINE = 8, CM_INV = 16 };
static uint32_t chord_mod_of_key(uint32_t k)
{
    switch ((53u + k) % 12u) {
    case 6: return CM_MINOR;
    case 8: return CM_SEVEN;
    case 10: return CM_SUS4;
    case 1: return CM_NINE;
    case 3: return CM_INV;
    default: return 0;
    }
}
static uint8_t vl_prev[NPART][4], vl_n[NPART];
static uint32_t scale_up(const track_t *t, uint32_t n, uint32_t deg)
{
    uint32_t mask = t->p[P_SCALE] ? scale_mask(t) : SCALE_MASK[2];
    int32_t m = (int32_t)n, guard = 48;
    while (deg > 0 && guard--) {
        m++;
        if ((mask >> (uint32_t)((m - t->p[P_ROOT] + 120) % 12)) & 1u)
            deg--;
    }
    return (uint32_t)m;
}
static void sort_notes(uint8_t *c, uint32_t n)
{
    uint32_t a, b;
    for (a = 1; a < n; a++)
        for (b = a; b > 0 && c[b - 1u] > c[b]; b--) {
            uint8_t x = c[b]; c[b] = c[b - 1u]; c[b - 1u] = x;
        }
}
static uint32_t chord_play_notes(track_t *t, uint32_t n, uint32_t mods, uint8_t *c)
{
    uint32_t k = chord_notes(t, n, c), j, type = (uint32_t)clamp(t->p[P_CHORD], 0, 5), part = trk_index(t);
    if (type != 5u && k >= 2u && mods) {
        uint32_t third = scale_up(t, n, 2u);
        for (j = 0; j < k; j++) {
            if (c[j] == third && (mods & CM_SUS4))
                c[j] = (uint8_t)scale_up(t, n, 3u);
            else if (c[j] == third && (mods & CM_MINOR))
                c[j] = (uint8_t)(c[j] - n == 4u ? c[j] - 1u : c[j] - n == 3u ? c[j] + 1u : c[j]);
        }
        if ((mods & CM_SEVEN) && k < 4u) {
            uint32_t s7 = scale_up(t, n, 6u);
            for (j = 0; j < k && c[j] != s7; j++)
                ;
            if (j == k && s7 < 128u)
                c[k++] = (uint8_t)s7;
        }
        if (mods & CM_NINE) {
            uint32_t s9 = scale_up(t, n, 8u), s5 = scale_up(t, n, 4u);
            for (j = 0; j < k && c[j] != s9; j++)
                ;
            if (j == k && s9 < 128u) {
                if (k < 4u) {
                    c[k++] = (uint8_t)s9;
                } else {
                    for (j = 0; j < k && c[j] != s5; j++)
                        ;
                    c[j < k ? j : k - 1u] = (uint8_t)s9;
                }
            }
        }
    }
    sort_notes(c, k);
    if (t->p[P_VLEAD] && part < NPART && vl_n[part] && k) {
        uint8_t best[4], cand[4];
        int32_t bcost = 0x7FFFFFFF, inv, oct;
        for (inv = 0; inv < (int32_t)k; inv++)
            for (oct = -1; oct <= 1; oct++) {
                int32_t cost = 0, ok = 1;
                for (j = 0; j < k; j++) {
                    int32_t v = c[j] + 12 * oct + (j < (uint32_t)inv ? 12 : 0);
                    if (v < 24 || v > 108)
                        ok = 0;
                    cand[j] = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
                }
                if (!ok)
                    continue;
                sort_notes(cand, k);
                for (j = 0; j < k; j++) {
                    int32_t d = (int32_t)cand[j] - vl_prev[part][j < vl_n[part] ? j : vl_n[part] - 1u];
                    cost += d < 0 ? -d : d;
                }
                if (cost < bcost) {
                    bcost = cost;
                    memcpy(best, cand, k);
                }
            }
        if (bcost != 0x7FFFFFFF)
            memcpy(c, best, k);
    }
    if ((mods & CM_INV) && k >= 2u && c[0] + 12u < 128u) {
        c[0] = (uint8_t)(c[0] + 12u);
        sort_notes(c, k);
    }
    if (part < NPART) {
        memcpy(vl_prev[part], c, k);
        vl_n[part] = (uint8_t)k;
    }
    return k;
}
/* ------------------------------------------------------------- grid --- */
static uint32_t swing_units(int32_t pct, uint32_t u)
{
    return (uint32_t)clamp(pct, 0, 100) * u / 200u;
}
static uint32_t grid_swing(uint32_t abs, uint32_t frac, uint32_t u, uint32_t sw, uint32_t *into, uint32_t *len)
{
    if (abs & 1u) {
        if (frac < sw) {
            abs--;
            frac += u;
            *len = u + sw;
        } else {
            frac -= sw;
            *len = u - sw;
        }
    } else {
        *len = u + sw;
    }
    *into = frac;
    return abs;
}
static uint32_t grid_den(uint32_t den, uint32_t sw, uint32_t *into, uint32_t *len)
{
    uint32_t u = BEAT_U / den;
    return grid_swing(clk_beat * den + clk_pos / u, clk_pos % u, u, sw, into, len);
}
static uint32_t grid_at(uint32_t div, uint32_t sw, uint32_t *into, uint32_t *len)
{
    uint32_t m;
    if (div < NDIV_SHORT)
        return grid_den(DIV_DEN[div], sw, into, len);
    m = DIV_BEATS[(div - NDIV_SHORT) % 3u];
    return grid_swing(clk_beat / m, (clk_beat % m) * BEAT_U + clk_pos, BEAT_U * m, sw, into, len);
}
static uint32_t swings(uint32_t div) { return div < 4u; }
static uint32_t trk_div(const track_t *t) { return (uint32_t)t->p[P_SDIV] % NDIV_STEP; }
static uint32_t trk_grid(const track_t *t, uint32_t *into, uint32_t *len)
{
    uint32_t div = trk_div(t);
    return grid_at(div, swings(div) ? swing_units(t->p[P_SSWING] + song.g[G_SWING], div_units(div)) : 0u, into, len);
}
static uint32_t trk_len(const track_t *t) { return t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u; }

/* ------------------------------------------------------------- undo --- */
static struct {
    uint8_t valid, undone, trk;
    int16_t len;
    uint32_t sess;
    step_t st[NSTEP];
    int8_t micro[NSTEP];
} undo;
static uint32_t undo_sess = 1;
static void undo_mark(const track_t *t, uint32_t sess)
{
    uint32_t i = trk_index(t);
    if (undo.valid && !undo.undone && undo.trk == i && undo.sess == sess)
        return;
    memcpy(undo.st, t->step, sizeof undo.st);
    memcpy(undo.micro, t->micro, sizeof undo.micro);
    undo.len = t->p[P_SLEN];
    undo.trk = (uint8_t)i;
    undo.sess = sess;
    undo.valid = 1;
    undo.undone = 0;
}
#define UNDO_REC(t) (((t)->pass << 2) | 1u)
static uint32_t undo_erase_sess;
/* ------------------------------------------------------- recording --- */
#define REC_LAT 512u
static int8_t rec_m;
static uint32_t rec_target(const track_t *t, uint32_t *later)
{
    uint32_t into, slen, abs = trk_grid(t, &into, &slen), half = slen / 2u, lat = REC_LAT * (uint32_t)song.g[G_BPM];
    int32_t off = (int32_t)into - (int32_t)lat, m;
    if (into > half + (lat < half ? lat : half)) {
        abs++;
        off -= (int32_t)slen;
    }
    if (off < -(int32_t)slen)
        off = -(int32_t)slen;
    m = slen ? (off * 64 + (off >= 0 ? (int32_t)(slen / 2u) : -(int32_t)(slen / 2u))) / (int32_t)slen : 0;
    rec_m = (int8_t)(m < MICRO_MIN ? MICRO_MIN : m > MICRO_MAX ? MICRO_MAX : m);
    *later = abs != t->seq_abs;
    return abs;
}
static uint8_t rec_quant_off;
static void rec_micro_set(track_t *t, uint32_t idx, int was_empty)
{
    if (rec_quant_off && was_empty && idx < NSTEP)
        t->micro[idx] = rec_m;
}

static void step_add(track_t *t, uint32_t idx, uint32_t note, uint32_t vel, uint32_t lvl, uint32_t rat)
{
    step_t *s = &t->step[idx];
    uint32_t k;
    if (s->time != ST_NOTE || !s->n || t->p[P_VOICE] != V_POLY) {
        s->n = 0;
        s->flags = 0;
        s->vel = 0;
        s->lvl = 0;
        s->rat = 0;
    }
    for (k = 0; k < s->n && s->note[k] != note; k++)
        ;
    if (k == s->n) {
        if (s->n < 4u)
            s->n++;
        k = s->n - 1u;
        s->note[k] = (uint8_t)note;
    }
    s->lvl = (uint8_t)((s->lvl & ~(3u << (2u * k))) | (lvl & 3u) << (2u * k));
    s->rat = (uint8_t)((s->rat & ~(3u << (2u * k))) | (rat & 3u) << (2u * k));
    s->time = ST_NOTE;
    if (vel > s->vel)
        s->vel = (uint8_t)vel;
}

#ifndef UI_SMALL
#define UI_SMALL
#endif
UI_SMALL static uint32_t chain_notes(const track_t *t, uint32_t idx, uint8_t *out)
{
    uint32_t len = trk_len(t), k;
    for (k = 1; k < len; k++) {
        const step_t *s = &t->step[(idx + len - k) % len];
        if (s->time == ST_TIE)
            continue;
        if (s->time == ST_NOTE && s->n && s->n <= 4u) {
            memcpy(out, s->note, s->n);
            return s->n;
        }
        return 0;
    }
    return 0;
}
UI_SMALL static int step_pure_cont(const step_t *s)
{
    return s->time == ST_NOTE && s->n && s->n <= 4u && ((s->flags & SF_CONT_ALL) / SF_CONT0) == (1u << s->n) - 1u;
}
UI_SMALL static void chain_end(track_t *t, uint32_t j, const uint8_t *keep, uint32_t n)
{
    step_t *s = &t->step[j % NSTEP];
    uint32_t i;
    if (s->time != ST_TIE || !n)
        return;
    memset(s, 0, sizeof *s);
    for (i = 0; i < n && i < 4u; i++) {
        s->note[i] = keep[i];
        s->flags |= (uint8_t)SF_CONT(i);
    }
    s->n = (uint8_t)i;
    s->time = ST_NOTE;
}
UI_SMALL static uint32_t cont_notes(const step_t *s, uint32_t except, uint8_t *out)
{
    uint32_t i, n = 0;
    for (i = 0; i < s->n && i < 4u; i++)
        if ((s->flags & SF_CONT(i)) && s->note[i] != except)
            out[n++] = s->note[i];
    return n;
}
static uint8_t span_why;
static int sp_on(const step_t *st) { return st->time == ST_NOTE && st->n; }
UI_SMALL static uint32_t span_notes(const step_t *st, uint8_t *s)
{
    uint32_t k, n = 0;
    for (k = 0; k < st->n && k < 4u; k++)
        if (!(st->flags & SF_CONT(k)))
            s[n++] = st->note[k];
    return n;
}
UI_SMALL static int span_in(const uint8_t *c, uint32_t nc, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < nc; i++)
        if (c[i] == note)
            return 1;
    return 0;
}
UI_SMALL static int span_chain(const track_t *t, uint32_t j, const uint8_t *s, uint32_t ns, int *other, uint8_t *oc, uint32_t *no)
{
    uint8_t c[4];
    uint32_t n = chain_notes(t, j, c), i, all = 1;
    *other = 0;
    if (no)
        *no = 0;
    for (i = 0; i < ns; i++)
        all &= (uint32_t)span_in(c, n, s[i]);
    for (i = 0; i < n; i++)
        if (!span_in(s, ns, c[i])) {
            *other = 1;
            if (oc)
                oc[(*no)++] = c[i];
        }
    return n && all;
}
UI_SMALL static int span_cont_all(const step_t *st, const uint8_t *s, uint32_t ns)
{
    uint32_t i, k;
    if (!sp_on(st))
        return 0;
    for (i = 0; i < ns; i++) {
        for (k = 0; k < st->n && !(st->note[k] == s[i] && (st->flags & SF_CONT(k))); k++)
            ;
        if (k == st->n)
            return 0;
    }
    return 1;
}
UI_SMALL static uint32_t span_len(const track_t *t, uint32_t i, const uint8_t *s, uint32_t ns)
{
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), n = 1;
    int other;
    while (i + n < len) {
        const step_t *st = &t->step[i + n];
        if (st->time == ST_TIE ? !span_chain(t, i + n, s, ns, &other, 0, 0) : !span_cont_all(st, s, ns))
            break;
        n++;
    }
    return n;
}
UI_SMALL static void span_drop(step_t *st, const uint8_t *s, uint32_t ns)
{
    uint32_t k = 0;
    while (k < st->n) {
        if ((st->flags & SF_CONT(k)) && span_in(s, ns, st->note[k])) {
            uint32_t j, c;
            for (j = k; j + 1u < st->n; j++)
                st->note[j] = st->note[j + 1u];
            st->lvl = (uint8_t)((st->lvl & ((1u << (2u * k)) - 1u)) | ((st->lvl >> (2u * k + 2u)) << (2u * k)));
            st->rat = (uint8_t)((st->rat & ((1u << (2u * k)) - 1u)) | ((st->rat >> (2u * k + 2u)) << (2u * k)));
            c = (st->flags & SF_CONT_ALL) / SF_CONT0;
            c = (c & ((1u << k) - 1u)) | ((c >> (k + 1u)) << k);
            st->flags = (uint8_t)((st->flags & ~SF_CONT_ALL) | c * SF_CONT0);
            st->n--;
        } else {
            k++;
        }
    }
    if (!st->n) {
        memset(st, 0, sizeof *st);
        st->time = ST_REST;
    }
}
UI_SMALL static uint32_t span_set(track_t *t, uint32_t i, const uint8_t *s, uint32_t ns, uint32_t want)
{
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), cur, j, n, no, k;
    uint8_t oc[4];
    int other;
    span_why = 3;
    cur = span_len(t, i, s, ns);
    if (want == cur)
        return cur;
    {
        uint8_t act[NSTEP];
        for (j = i + 1u; j < i + cur; j++)
            act[j] = t->step[j].time == ST_TIE ? (uint8_t)(span_chain(t, j, s, ns, &other, 0, 0) && !other) : 2u;
        for (j = i + 1u; j < i + cur; j++) {
            step_t *st = &t->step[j];
            if (act[j] == 1u) {
                if (!st->n)
                    memset(st, 0, sizeof *st);
                st->time = ST_REST;
            } else if (act[j] == 2u) {
                span_drop(st, s, ns);
            }
        }
        for (j = i + 1u; j <= i + cur && j < len; j++) {
            step_t *st = &t->step[j];
            uint8_t c[4];
            uint32_t nc, m, same;
            if (!step_pure_cont(st) || j == i)
                continue;
            nc = chain_notes(t, j, c);
            same = nc == st->n;
            for (m = 0; m < st->n && same; m++)
                same = (uint32_t)span_in(c, nc, st->note[m]);
            if (same) {
                memset(st, 0, sizeof *st);
                st->time = ST_TIE;
            }
        }
    }
    for (n = 1, j = i + 1u; n < want && j < len; n++, j++) {
        step_t *st = &t->step[j];
        if (st->time == ST_TIE) {
            if (!span_chain(t, j, s, ns, &other, 0, 0)) {
                span_why = 0;
                break;
            }
            continue;
        }
        if (sp_on(st)) {
            uint32_t add = 0;
            if (t->p[P_VOICE] != V_POLY) {
                span_why = 2;
                break;
            }
            for (k = 0; k < ns; k++)
                add += !span_in(st->note, st->n, s[k]);
            if (add < ns || st->rat || (st->flags & SF_SLIDE)) {
                span_why = 0;
                break;
            }
            if (st->n + add > 4u) {
                span_why = 1;
                break;
            }
            for (k = 0; k < ns; k++) {
                st->flags |= (uint8_t)SF_CONT(st->n);
                st->note[st->n++] = s[k];
            }
            continue;
        }
        span_chain(t, j, s, ns, &other, 0, 0);
        memset(st, 0, sizeof *st);
        if (other) {
            for (k = 0; k < ns; k++) {
                st->note[k] = s[k];
                st->flags |= (uint8_t)SF_CONT(k);
            }
            st->n = (uint8_t)ns;
            st->time = ST_NOTE;
        } else {
            st->time = ST_TIE;
        }
    }
    if (j < len && t->step[j].time == ST_TIE && span_chain(t, j, s, ns, &other, oc, &no) && other)
        chain_end(t, j, oc, no);
    return n;
}
UI_SMALL static int rh_has(const track_t *t, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < t->rh_n; i++)
        if (t->rh_note[i] == note)
            return 1;
    for (i = 0; i < t->rh_offn; i++)
        if (t->rh_off[i] == note)
            return 1;
    return 0;
}
UI_SMALL static int rh_held(const track_t *t, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < t->rh_n; i++)
        if (t->rh_note[i] == note)
            return 1;
    return 0;
}
UI_SMALL static void hold_marker_clear(track_t *t, uint32_t j, uint32_t also)
{
    step_t *s = &t->step[j % NSTEP];
    uint32_t i;
    if (!step_pure_cont(s))
        return;
    for (i = 0; i < s->n; i++)
        if (!rh_has(t, s->note[i]) && s->note[i] != also)
            return;
    memset(s, 0, sizeof *s);
    s->time = ST_REST;
}UI_SMALL static void hold_restart(track_t *t, uint32_t j, uint32_t len, uint32_t abs)
{
    step_t *s = &t->step[j % NSTEP];
    uint8_t c[4];
    uint32_t n = chain_notes(t, j, c), i, k, m = 0;
    for (i = 0; i < n; i++) {
        for (k = 0; k < t->rh_offn && t->rh_off[k] != c[i]; k++)
            ;
        if (k == t->rh_offn)
            c[m++] = c[i];
    }
    hold_marker_clear(t, (j + 1u) % len, 0x100u);
    memset(s, 0, sizeof *s);
    for (i = 0; i < m; i++) {
        s->note[i] = c[i];
        s->flags |= (uint8_t)SF_CONT(i);
    }
    s->n = (uint8_t)m;
    s->time = m ? ST_NOTE : ST_REST;
    t->rh_start = (uint8_t)j;
    t->rh_last = (uint8_t)j;
    t->rh_last_abs = abs;
    t->rh_ties = 0;
    t->rh_rs = 1;
    t->rh_end = 0;
    t->rh_offn = 0;
}
UI_SMALL static void rec_note(track_t *t, uint32_t note, uint32_t vel, uint32_t rat, int hold)
{
    uint32_t len = trk_len(t), later, abs = rec_target(t, &later), idx = abs % len, k;
    int was_empty = !(t->step[idx % NSTEP].time == ST_NOTE && t->step[idx % NSTEP].n);
    step_t *st = &t->step[idx % NSTEP];
    uint8_t ch[4], keep[4];
    uint32_t nc = 0, nk = 0, merged = 0;
    undo_mark(t, UNDO_REC(t));
    if (t->p[P_VOICE] == V_POLY && !rat && hold && t->rh_n && st->time != ST_TIE && !(st->time == ST_NOTE && st->n) &&
        idx == ((t->rh_ties || t->rh_rs ? t->rh_last : t->rh_start) + 1u) % len) {
        nc = t->rh_n;
        memcpy(ch, t->rh_note, nc);
        st->time = ST_TIE;
    } else if (t->p[P_VOICE] == V_POLY && !rat && st->time == ST_TIE) {
        nc = chain_notes(t, idx, ch);
    }
    if (nc) {
        merged = hold != 0;
        for (k = 0; k < nc; k++)
            if (ch[k] != note && !(t->rh_n && rh_has(t, ch[k]) && !rh_held(t, ch[k])))
                keep[nk++] = ch[k];
        while (nk > 3u) {
            for (k = 0; k + 1u < nk; k++)
                keep[k] = keep[k + 1u];
            nk--;
        }
        memset(st, 0, sizeof *st);
        for (k = 0; k < nk; k++) {
            st->note[k] = keep[k];
            st->flags |= (uint8_t)SF_CONT(k);
        }
        st->note[nk] = (uint8_t)note;
        st->n = (uint8_t)(nk + 1u);
        st->time = ST_NOTE;
        st->vel = (uint8_t)vel;
        st->lvl = (uint8_t)((vel_lvl(vel) & 3u) << (2u * nk));
        if ((idx + 1u) % len != idx)
            chain_end(t, (idx + 1u) % len, keep, nk);
    } else {
        uint32_t last = t->rh_ties || t->rh_rs ? t->rh_last : t->rh_start;
        int leg = t->p[P_VOICE] != V_POLY && !rat && hold && t->rh_n && !(t->rh_n == 1u && t->rh_note[0] == note) &&
                  idx != t->rh_start && (idx == (last + 1u) % len || (t->rh_ties && idx == last && st->time == ST_TIE));
        step_add(t, idx, note, vel, vel_lvl(vel), rat);
        if (leg)
            st->flags |= (uint8_t)SF_LEG;
        for (k = 0; k < st->n; k++)
            if (st->note[k] == note)
                st->flags &= (uint8_t)~SF_CONT(k);
        if ((st->flags & SF_CONT_ALL) && (nk = cont_notes(st, note, keep)) != 0 && (idx + 1u) % len != idx)
            chain_end(t, (idx + 1u) % len, keep, nk);
    }
    if (hold)
        rec_micro_set(t, idx, was_empty);
    t->seq_active = 1;
    if (later) {
        if (t->rskip_abs != abs)
            t->rskip_n = 0;
        t->rskip_abs = abs;
        if (t->rskip_n < 4u)
            t->rskip[t->rskip_n++] = (uint8_t)note;
    }
    if (!hold)
        return;
    if (merged) {
        t->rh_start = (uint8_t)idx;
        t->rh_ties = 0;
        t->rh_rs = t->rh_end = t->rh_offn = 0;
    } else if (!t->rh_n || t->rh_start != idx) {
        t->rh_n = 0;
        t->rh_start = (uint8_t)idx;
        t->rh_ties = 0;
        t->rh_rs = t->rh_end = t->rh_offn = 0;
    }
    for (k = 0; k < t->rh_n && t->rh_note[k] != note; k++)
        ;
    if (k == t->rh_n && t->rh_n < 4u)
        t->rh_note[t->rh_n++] = (uint8_t)note;
}
static uint8_t roll_rec;
static void rec_hit(track_t *t, uint32_t lane, uint32_t lvl, uint32_t rat)
{
    uint32_t later, abs = rec_target(t, &later), idx = abs % trk_len(t);
    int was_empty = !(t->dstep[idx % NSTEP].on[0] | t->dstep[idx % NSTEP].on[1]);
    undo_mark(t, UNDO_REC(t));
    dstep_set(&t->dstep[idx], lane, lvl, rat);
    if (!roll_rec)
        rec_micro_set(t, idx, was_empty);
    t->seq_active = 1;
    if (later) {
        if (t->rskip_abs != abs)
            t->rskip_lanes = 0;
        t->rskip_abs = abs;
        t->rskip_lanes |= (uint16_t)(1u << lane);
    }
}

UI_SMALL static int hold_merge(track_t *t, step_t *s)
{
    uint32_t i, k, add = 0;
    for (i = 0; i < t->rh_n; i++) {
        for (k = 0; k < s->n && s->note[k] != t->rh_note[i]; k++)
            ;
        add += k == s->n;
    }
    if (t->p[P_VOICE] != V_POLY || s->n > 4u || s->n + add > 4u || (s->flags & SF_SLIDE) || s->rat)
        return 0;
    for (i = 0; i < t->rh_n; i++) {
        for (k = 0; k < s->n && s->note[k] != t->rh_note[i]; k++)
            ;
        if (k == s->n)
            s->note[s->n++] = t->rh_note[i];
        s->flags |= (uint8_t)SF_CONT(k);
    }
    return 1;
}
UI_SMALL static int hold_only(track_t *t, step_t *s, uint32_t idx)
{
    uint8_t c[4];
    uint32_t n = chain_notes(t, idx, c), i, other = 0;
    if (s->time == ST_TIE)
        return 0;
    for (i = 0; i < n; i++)
        other |= !rh_held(t, c[i]);
    if (!other || t->p[P_VOICE] != V_POLY || !t->rh_n)
        return 0;
    memset(s, 0, sizeof *s);
    for (i = 0; i < t->rh_n && i < 4u; i++) {
        s->note[i] = t->rh_note[i];
        s->flags |= (uint8_t)SF_CONT(i);
    }
    s->n = (uint8_t)i;
    s->time = ST_NOTE;
    return 1;
}
UI_SMALL static void rec_hold(track_t *t, uint32_t idx, uint32_t len, uint32_t abs)
{
    step_t *s;
    if (!t->rh_n)
        return;
    if (!((song.rec >> trk_index(t)) & 1u) || t->rh_ties + 1u >= len) {
        t->rh_n = 0;
        return;
    }
    if (idx == t->rh_start)
        return;
    s = &t->step[idx];
    if (t->rh_end && (s->time == ST_TIE || s->time == ST_REST || !s->n || step_pure_cont(s))) {
        hold_restart(t, idx, len, abs);
        return;
    }
    if (step_pure_cont(s)) {
        uint8_t keep[4];
        uint32_t nk = cont_notes(s, 0x100u, keep);
        t->rh_bak = *s;
        t->rh_last = (uint8_t)idx;
        t->rh_last_abs = abs;
        t->rh_ties++;
        memset(s, 0, sizeof *s);
        s->time = ST_TIE;
        if ((idx + 1u) % len != t->rh_start)
            chain_end(t, (idx + 1u) % len, keep, nk);
        return;
    }
    if (s->time == ST_NOTE && s->n) {
        step_t bak = *s;
        if (hold_merge(t, s)) {
            t->rh_bak = bak;
            t->rh_put = *s;
            t->rh_last = (uint8_t)idx;
            t->rh_last_abs = abs;
            t->rh_ties++;
            return;
        }
        t->rh_n = 0;
        return;
    }
    t->rh_bak = *s;
    t->rh_last = (uint8_t)idx;
    t->rh_last_abs = abs;
    t->rh_ties++;
    if (!hold_only(t, s, idx)) {
        memset(s, 0, sizeof *s);
        s->time = ST_TIE;
    }
    t->rh_put = *s;
}
UI_SMALL static void rec_release(track_t *t, uint32_t note)
{
    uint32_t i, k = 0, into, slen, half, lat, early, len = trk_len(t);
    for (i = 0; i < t->rh_n; i++)
        if (t->rh_note[i] != note)
            t->rh_note[k++] = t->rh_note[i];
    if (k == t->rh_n)
        return;
    early = trk_grid(t, &into, &slen) == t->rh_last_abs;
    half = slen / 2u;
    lat = REC_LAT * (uint32_t)song.g[G_BPM];
    early = early && into <= half + (lat < half ? lat : half);
    if (k) {
        t->rh_n = (uint8_t)k;
        if (t->p[P_VOICE] != V_POLY)
            return;
        if (t->rh_offn < 4u)
            t->rh_off[t->rh_offn++] = (uint8_t)note;
        if (t->rh_ties && early && t->step[t->rh_last].time == ST_TIE)
            hold_restart(t, t->rh_last, len, t->rh_last_abs);
        else
            t->rh_end = 1;
        return;
    }
    t->rh_n = 0;
    hold_marker_clear(t, ((t->rh_ties || t->rh_rs ? t->rh_last : t->rh_start) + 1u) % len, note);
    if (t->rh_rs && !t->rh_ties && early && step_pure_cont(&t->step[t->rh_last])) {
        memset(&t->step[t->rh_last], 0, sizeof t->step[t->rh_last]);
        t->step[t->rh_last].time = ST_REST;
    }
    t->rh_end = t->rh_rs = t->rh_offn = 0;
    if (t->rh_ties && early && (t->step[t->rh_last].time == ST_TIE || !memcmp(&t->step[t->rh_last], &t->rh_put, sizeof t->rh_put)))
        t->step[t->rh_last] = t->rh_bak;
    {
        uint32_t j = t->rh_ties && early ? t->rh_last : (uint32_t)(t->rh_ties || t->rh_rs ? t->rh_last : t->rh_start) + 1u;
        uint8_t c[4], keep[4];
        uint32_t n, i, nk = 0, has = 0;
        j %= len;
        if (t->step[j].time != ST_TIE || j == t->rh_start)
            return;
        n = chain_notes(t, j, c);
        for (i = 0; i < n; i++) {
            if (c[i] == note)
                has = 1;
            else
                keep[nk++] = c[i];
        }
        if (has && nk)
            chain_end(t, j, keep, nk);
    }
}


static volatile uint8_t rec_wait;
static uint8_t rec_tempo;
static uint8_t rec_count;
static volatile uint8_t ci_on;
static volatile uint8_t ci_beat;
static uint32_t ci_u;
static volatile uint8_t rec_go;
static void seq_start(void);
static void rec_begin(void)
{
    song.rec = (uint8_t)(1u << (song.sel % NTRK));
    rec_wait = 0;
    rec_go = 1;
}

#define FT_MAX 192u
#define FT_BLOCKS (24u * FS / CTL)
#define FT_OPEN 0xFFFFu
typedef struct { uint16_t t, d; uint8_t note, vel; } ft_ev_t;
static ft_ev_t ft_ev[FT_MAX];
static uint32_t ft_n;
static volatile uint8_t ft_on;
static uint8_t ft_trk;
static volatile uint32_t ft_t;
static volatile uint32_t ft_btn_mask;
static volatile uint32_t ft_drop_mask;
static uint32_t ft_btn_prev;
static volatile uint8_t ft_bars;
static volatile uint32_t ft_close_ms;
static volatile uint8_t ft_closed;
static int track_empty(const track_t *t)
{
    uint32_t k;
    for (k = 0; k < NSTEP; k++)
        if (is_drum(t) ? dstep_mask(&t->dstep[k]) != 0u : t->step[k].time == ST_NOTE && t->step[k].n)
            return 0;
    return 1;
}
static int project_empty(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++)
        if (!track_empty(&trk[i]))
            return 0;
    return 1;
}

static int p_lockable(uint32_t id)
{
    return id <= P_LD_AMP || id == P_SGATE || (id >= P_DIST && id <= P_REV) || id == P_GLIDE || id == P_PAN ||
           id == P_DETUNE || (id >= P_SLCR && id <= P_SLDEPTH) || (id >= P_E0 && id <= P_E7) || id == P_TFLT ||
           (id >= P_IA && id <= P_IMIX);
}
static const param_desc_t *lock_desc(const track_t *t, uint32_t id)
{
    if (is_drum(t) && id == P_E0)
        return &DRUM_KIT_DESC;
    if (id >= P_E0 && id <= P_E7)
        return &ENGINES[t->engine % NENGINES]->edit[id - P_E0];
    return &TP[id % P_COUNT];
}
static void lock_write(track_t *t, uint32_t id, int32_t v)
{
    const param_desc_t *d = lock_desc(t, id);
    t->p[id % P_COUNT] = (int16_t)clamp(v, d->min, d->max);
}
static void locks_restore(track_t *t)
{
    uint32_t i;
    for (i = 0; i < t->lk_n && i < NLOCK; i++)
        if (t->p[t->lk_param[i] % P_COUNT] == t->lk_set[i])
            lock_write(t, t->lk_param[i], t->lk_base[i]);
    t->lk_n = 0;
}
static void locks_clear(track_t *t)
{
    uint32_t i;
    locks_restore(t);
    memset(t->micro, 0, sizeof t->micro);
    for (i = 0; i < NLOCK; i++) {
        t->lock[i].step = LOCK_FREE;
        t->lock[i].param = 0;
        t->lock[i].val = 0;
    }
}
static void lock_step(track_t *t, uint32_t idx)
{
    uint32_t i, k, n = 0;
    for (i = 0; i < t->lk_n && i < NLOCK; i++) {
        uint32_t p = t->lk_param[i] % P_COUNT, has = 0;
        for (k = 0; k < NLOCK; k++)
            if (t->lock[k].step == idx && t->lock[k].param == p)
                has = 1;
        if (has) {
            t->lk_param[n] = (uint8_t)p;
            t->lk_base[n] = t->lk_base[i];
            t->lk_set[n] = t->lk_set[i];
            n++;
        } else if (t->p[p] == t->lk_set[i]) {
            lock_write(t, p, t->lk_base[i]);
        }
    }
    t->lk_n = (uint8_t)n;
    for (k = 0; k < NLOCK; k++) {
        const plock_t *l = &t->lock[k];
        uint32_t p = l->param;
        if (l->step != idx || p >= P_COUNT || !p_lockable(p))
            continue;
        for (i = 0; i < t->lk_n && t->lk_param[i] != p; i++)
            ;
        if (i == t->lk_n) {
            if (i >= NLOCK)
                continue;
            t->lk_param[i] = (uint8_t)p;
            t->lk_base[i] = t->p[p];
            t->lk_n++;
        } else if (t->p[p] != t->lk_set[i]) {
            t->lk_base[i] = t->p[p];
        }
        lock_write(t, p, l->val);
        t->lk_set[i] = t->p[p];
    }
}
static int lock_find(const track_t *t, uint32_t step, uint32_t param, int make)
{
    uint32_t k;
    int fr = -1;
    for (k = 0; k < NLOCK; k++) {
        if (t->lock[k].step == step && t->lock[k].param == param)
            return (int)k;
        if (fr < 0 && t->lock[k].step == LOCK_FREE)
            fr = (int)k;
    }
    return make ? fr : -1;
}
static int lock_set(track_t *t, uint32_t step, uint32_t param, int32_t v)
{
    int k;
    const param_desc_t *d;
    if (step >= NSTEP || param >= P_COUNT || !p_lockable(param) || (k = lock_find(t, step, param, 1)) < 0)
        return 0;
    d = lock_desc(t, param);
    t->lock[k].step = (uint8_t)step;
    t->lock[k].param = (uint8_t)param;
    t->lock[k].val = (int16_t)clamp(v, d->min, d->max);
    return 1;
}
static void lock_del(track_t *t, uint32_t step, uint32_t param)
{
    uint32_t k;
    for (k = 0; k < NLOCK; k++)
        if (t->lock[k].step == step && (param >= P_COUNT || t->lock[k].param == param))
            t->lock[k].step = LOCK_FREE;
}
static int step_locked(const track_t *t, uint32_t step)
{
    uint32_t k;
    if (step < NSTEP && t->micro[step])
        return 1;
    for (k = 0; k < NLOCK; k++)
        if (t->lock[k].step == step)
            return 1;
    return 0;
}

static uint32_t step_fill(const track_t *t, uint32_t idx)
{
    idx %= NSTEP;
    return (uint32_t)(t->fill[idx / 4u] >> (2u * (idx % 4u))) & 3u;
}
static void step_fill_set(track_t *t, uint32_t idx, uint32_t v)
{
    uint32_t sh;
    idx %= NSTEP;
    sh = 2u * (idx % 4u);
    t->fill[idx / 4u] = (uint8_t)((t->fill[idx / 4u] & ~(3u << sh)) | (v & 3u) << sh);
}
static volatile uint8_t fill_held;
static volatile uint8_t fill_arm;
static uint8_t fill_bar_on;
static uint8_t fill_now;
static uint32_t fill_last_bar = 0xFFFFFFFFu;
static uint32_t step_plays(const track_t *t, uint32_t idx)
{
    uint32_t c = step_fill(t, idx);
    return c == FC_FILL ? fill_now : c == FC_NOFILL ? !fill_now : 1u;
}

static void steps_clear(track_t *t)
{
    uint32_t k;
    memset(t->step, 0, sizeof t->step);
    if (!is_drum(t))
        for (k = 0; k < NSTEP; k++)
            t->step[k].time = ST_REST;
    locks_clear(t);
    memset(t->fill, 0, sizeof t->fill);
}
static uint32_t ft_bpm10(uint32_t T, uint32_t n)
{
    uint32_t den = T * CTL;
    return den ? (n * 2400u * FS + den / 2u) / den : 0u;
}

static uint32_t ft_fit(uint32_t T, uint32_t *bpm)
{
    static const uint8_t BARS[] = {1, 2, 4};
    uint32_t i, best = 0, cur = (uint32_t)song.g[G_BPM] * 10u, err = 0xFFFFFFFFu;
    for (i = 0; i < 3u; i++) {
        uint32_t b = ft_bpm10(T, BARS[i]), e;
        if (b < 400u || b > 2400u)
            continue;
        e = b > cur ? b * 1000u / cur : cur * 1000u / b;
        if (e < err) {
            err = e;
            best = BARS[i];
            *bpm = (b + 5u) / 10u;
            if (e <= 1030u)
                *bpm = cur / 10u;
        }
    }

    return best;
}

static void ft_start(track_t *t)
{
    ft_on = 1;
    ft_trk = (uint8_t)trk_index(t);
    ft_t = 0;
    ft_n = 0;
    rec_wait = 0;
}

static void ft_note_on(uint32_t note, uint32_t vel)
{
    if (ft_n < FT_MAX && ft_t < FT_BLOCKS) {
        ft_ev[ft_n].t = (uint16_t)ft_t;
        ft_ev[ft_n].d = FT_OPEN;
        ft_ev[ft_n].note = (uint8_t)note;
        ft_ev[ft_n].vel = (uint8_t)vel;
        ft_n++;
    }
}

static void ft_note_off(uint32_t note)
{
    uint32_t i = ft_n;
    while (i--)
        if (ft_ev[i].note == note && ft_ev[i].d == FT_OPEN) {
            ft_ev[i].d = (uint16_t)(ft_t - ft_ev[i].t);
            return;
        }
}
static void ft_close(void)
{
    track_t *t = &trk[ft_trk % NTRK];
    uint32_t T = ft_t, bars, bpm = 0, len, i, k;
    ft_on = 0;
    ft_close_ms = fm1_ms;
    ft_closed = 1;
    bars = ft_n && T >= FS / CTL / 2u ? ft_fit(T, &bpm) : 0u;
    if (!bars) {
        ft_bars = 0xFF;
        return;
    }    len = 16u * bars;
    undo_mark(t, (undo_sess += 4u) | 3u);
    steps_clear(t);
    for (i = 0; i < NTRK; i++) {
        trk[i].p[P_SLEN] = (int16_t)len;
        trk[i].p[P_SDIV] = 2;
    }
    for (i = 0; i < ft_n; i++) {
        uint32_t idx = (ft_ev[i].t * len * 2u + T) / (2u * T) % len;
        int was_empty = is_drum(t) ? !(t->dstep[idx].on[0] | t->dstep[idx].on[1])
                                   : !(t->step[idx].time == ST_NOTE && t->step[idx].n);
        if (is_drum(t))
            dstep_set(&t->dstep[idx], ft_ev[i].note & 15u, ft_ev[i].vel & 3u, 0);
        else
            step_add(t, idx, ft_ev[i].note, ft_ev[i].vel, vel_lvl(ft_ev[i].vel), 0);
        if (rec_quant_off && was_empty) {
            uint32_t u = (ft_ev[i].t * len * 128u + T) / (2u * T);
            int32_t m = (int32_t)u - (int32_t)((ft_ev[i].t * len * 2u + T) / (2u * T)) * 64;
            t->micro[idx] = (int8_t)(m < MICRO_MIN ? MICRO_MIN : m > MICRO_MAX ? MICRO_MAX : m);
        }
    }
    if (!is_drum(t))
        for (i = 0; i < ft_n; i++) {
            uint32_t d = ft_ev[i].d == FT_OPEN || ft_ev[i].t + ft_ev[i].d > T ? T - ft_ev[i].t : ft_ev[i].d;
            uint32_t idx = (ft_ev[i].t * len * 2u + T) / (2u * T) % len;
            uint32_t steps = (d * len * 2u + T) / (2u * T);
            if (t->p[P_VOICE] == V_POLY) {
                uint8_t nn = ft_ev[i].note;
                if (steps > 1u)
                    span_set(t, idx, &nn, 1u, steps < len - idx ? steps : len - idx);
                continue;
            }
            for (k = 1; k < steps && k < len; k++) {
                step_t *s = &t->step[(idx + k) % len];
                if (s->time == ST_NOTE && s->n)
                    break;
                memset(s, 0, sizeof *s);
                s->time = ST_TIE;
            }
        }
    t->seq_active = 1;
    song.g[G_BPM] = (int16_t)bpm;
    ft_bars = (uint8_t)bars;
#if FELUCCA_ARRANGER
    arrangement_enabled = 0;
#endif
    seq_start();
}

static int ft_owns_press(void)
{
    return ft_on || (ft_closed && (uint32_t)(fm1_ms - ft_close_ms) < 300u);
}

static void ft_block(void)
{
    uint32_t b = fm1_in.buttons & (ft_btn_mask | ft_drop_mask), press = b & ~ft_btn_prev;
    ft_btn_prev = b;
    if (!ft_on)
        return;
    ft_t++;
    if (press & ft_drop_mask) {
        ft_on = 0;
        ft_bars = 0xFF;
        ft_close_ms = fm1_ms;
        ft_closed = 1;
    } else if ((press & ft_btn_mask) || ft_t >= FT_BLOCKS) {
        ft_close();
    }
}
static uint8_t er_trk;
static uint16_t er_lanes;
static volatile uint16_t drum_mute;
static volatile uint8_t er_mute;
static uint32_t er_notes[4];
static volatile uint8_t er_flash;

static int er_has(uint32_t note) { return (er_notes[(note >> 5) & 3u] >> (note & 31u)) & 1u; }
static void erase_step(track_t *t, uint32_t idx)
{
    if (is_drum(t)) {
        dstep_t *s = &t->dstep[idx];
        uint32_t l, m = dstep_mask(s) & er_lanes;
        for (l = 0; m; l++, m >>= 1)
            if (m & 1u) {
                dstep_clr(s, l);
                er_flash = 1;
            }
    } else {
        step_t *s = &t->step[idx];
        uint32_t i, k = 0, lv = 0, rt = 0, cf = 0;
        if (s->time != ST_NOTE)
            return;
        for (i = 0; i < s->n; i++)
            if (!er_has(s->note[i])) {
                lv |= ((s->lvl >> (2u * i)) & 3u) << (2u * k);
                rt |= ((s->rat >> (2u * i)) & 3u) << (2u * k);
                cf |= (s->flags & SF_CONT(i)) ? SF_CONT(k) : 0u;
                s->note[k++] = s->note[i];
            }
        if (k == s->n)
            return;
        er_flash = 1;
        s->n = (uint8_t)k;
        s->lvl = (uint8_t)lv;
        s->rat = (uint8_t)rt;
        s->flags = (uint8_t)((s->flags & ~SF_CONT_ALL) | cf);
        if (!k) {
            s->time = ST_REST;
            s->flags = 0;
            s->vel = 0;
        }
    }
}
static void erase_now(track_t *t)
{
    uint32_t i, len = trk_len(t);
    undo_mark(t, undo_erase_sess);
    if (song.playing && t->seq_abs != SEQ_NONE) {
        erase_step(t, t->seq_idx % len);
    } else {
        for (i = 0; i < len; i++)
            erase_step(t, i);
    }
}
static int erasing(const track_t *t) { return trk_index(t) == er_trk && (er_lanes || er_notes[0] || er_notes[1] || er_notes[2] || er_notes[3]); }

static uint8_t held_key[NTRK][16];
static int8_t held_dt[NTRK][16];
static uint8_t in_key;
static void arp_add(track_t *t, uint32_t note)
{
    uint32_t i;
    if (t->p[P_AHOLD] && t->arp_phys == 0u)
        t->nheld = 0;
    t->arp_phys++;
    for (i = 0; i < t->nheld; i++)
        if (t->held[i] == note)
            return;
    if (t->nheld < 16u) {
        uint32_t k = in_key - 1u, ti = trk_index(t);
        held_key[ti][t->nheld] = in_key && kb_scaled(t) ? (uint8_t)(in_key | (uint32_t)(kb_oct[k % 27u] + 3) << 5) : 0u;
        held_dt[ti][t->nheld] = (int8_t)(held_key[ti][t->nheld] ? (int32_t)note - kb_root[k % 27u] : 0);
        t->held[t->nheld++] = (uint8_t)note;
    }
    if (t->nheld == 1u) {
        t->arp_new = 1;
        t->arp_idx = 0xFFFFFFFFu;
    }
}
static void arp_remove(track_t *t, uint32_t note)
{
    uint32_t i, k = 0;
    if (t->arp_phys)
        t->arp_phys--;
    if (t->p[P_AHOLD])
        return;
    for (i = 0; i < t->nheld; i++)
        if (t->held[i] != note) {
            held_key[trk_index(t)][k] = held_key[trk_index(t)][i];
            held_dt[trk_index(t)][k] = held_dt[trk_index(t)][i];
            t->held[k++] = t->held[i];
        }
    t->nheld = (uint8_t)k;
}

enum { AM_OFF, AM_UP, AM_DN, AM_UPDN, AM_RND, AM_ORD, AM_REPEAT, AM_DNUP, AM_UP8, AM_CONV, AM_DIVG, AM_PINKY,
       AM_THUMB, AM_WALK, AM_CHORD };

static uint32_t arp_list(const track_t *t, uint32_t *list, uint32_t *cnt)
{
    uint32_t n = 0, len = 0, i, j, o, ti = trk_index(t), last = 0, m = (uint32_t)t->p[P_AMODE];
    for (i = 0; i < t->nheld; i++) {
        uint32_t x = t->held[i], s = held_key[ti][i];
        if (s) {
            x = kb_pitch(t, (s & 31u) - 1u, (int32_t)(s >> 5) - 3);
            if (x == KB_SILENT)
                continue;
            x = (uint32_t)clamp((int32_t)x + held_dt[ti][i], 0, 127);
        }
        last = x;
        for (j = 0; j < n && list[j] != x; j++)
            ;
        if (j == n)
            list[n++] = x;
    }
    *cnt = n;
    if (!n)
        return 0;
    if (m == AM_REPEAT) {
        list[0] = last;
        return 1;
    }
    if (!t->p[P_AORDER] && m != AM_ORD)
        for (i = 1; i < n; i++)
            for (j = i; j > 0 && list[j - 1] > list[j]; j--) {
                uint32_t x = list[j];
                list[j] = list[j - 1];
                list[j - 1] = x;
            }
    for (o = 0; o < (uint32_t)t->p[P_AOCT]; o++)
        for (i = 0; i < n && len < 64u; i++)
            list[len++] = (uint32_t)clamp((int32_t)list[i] + 12 * (int32_t)o, 0, 127);
    return len;
}

static uint32_t arp_pick(track_t *t, uint32_t *list, uint32_t len)
{
    uint32_t i = t->arp_idx, m = (uint32_t)t->p[P_AMODE], cyc, k, w;
    if (m == AM_UP8)
        list[len++] = (uint32_t)clamp((int32_t)list[0] + 12 * t->p[P_AOCT], 0, 127);
    cyc = len > 1u ? 2u * len - 2u : 1u;
    k = i % cyc;
    switch (m) {
    case AM_DN:
        return len - 1u - i % len;
    case AM_UPDN:
        return k < len ? k : cyc - k;
    case AM_DNUP:
        return k < len ? len - 1u - k : k - (len - 1u);
    case AM_RND:
        return rng() % len;
    case AM_CONV:
    case AM_DIVG:
        k = i % len;
        if (m == AM_DIVG)
            k = len - 1u - k;
        return (k & 1u) ? len - 1u - k / 2u : k / 2u;
    case AM_PINKY:
        return len < 2u ? 0u : (k & 1u) ? len - 1u : k / 2u;
    case AM_THUMB:
        return len < 2u ? 0u : (k & 1u) ? 1u + k / 2u : 0u;
    case AM_WALK:
        w = t->arp_walk;
        if (!i)
            w = 0;
        else if (w >= len)
            w = len - 1u;
        else if (len > 1u)
            w = !w ? 1u : w == len - 1u ? w - 1u : (rng() >> 7) & 1u ? w + 1u : w - 1u;
        t->arp_walk = (uint8_t)w;
        return w;
    default:
        return i % len;
    }
}

static void arp_end(track_t *t)
{
    uint32_t i;
    for (i = 0; i < 3u; i++)
        if (t->arp_ch[i]) {
            trk_note_off(t, t->arp_ch[i]);
            seq_out_off(t, t->arp_ch[i]);
            t->arp_ch[i] = 0;
        }
    if (t->arp_note) {
        trk_note_off(t, t->arp_note);
        seq_out_off(t, t->arp_note);
        t->arp_note = 0;
    }
}

static void arp_play(track_t *t, uint32_t n)
{
    trk_note_on(t, n, 100);
    seq_out_on(t, n, 100);
    if (((song.rec >> trk_index(t)) & 1u) && song.playing)
        rec_note(t, n, 100, 0, 0);
}

/* ------------------------------------------- AUDILUNE transport & chain --- */
static uint32_t step_vel(const step_t *s, uint32_t i)
{
    uint32_t lv = (s->lvl >> (2u * i)) & 3u;
    return lvl_vel(lv, s->vel ? s->vel : 100u);
}

static void seq_release(track_t *t)
{
    uint32_t i;
    for (i = 0; i < t->seq_n && i < 4u; i++) {
        uint32_t n = t->seq_notes[i];
        if (!live_has(t, n))
            trk_note_off(t, n);
        seq_out_off(t, n);
    }
    t->seq_n = 0;
    t->seq_hold = 0;
    t->slide_glide = 0;
}

static void click_tick(void)
{
    static uint32_t last_beat = 0xFFFFFFFFu;
    if (song.playing && clk_beat != last_beat) {
        last_beat = clk_beat;
        if (song.g[G_CLICK])
            click_on((clk_beat & 3u) == 0u);

    } else if (!song.playing) {
        last_beat = 0xFFFFFFFFu;
    }
    if (click_rem) {
        if (click_rem <= CTL) {
            click_rem = 0;
            click_env = 0;
        } else {
            click_rem -= CTL;
        }
    }
}

static void seq_reset_tracks(uint32_t phase)
{
    uint32_t i;
    (void)phase;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        if (!is_drum(t))
            seq_release(t);
        t->seq_abs = SEQ_NONE;
        t->seq_idx = 0;
        t->seq_off = 0;
        t->seq_hold = 0;
        t->seq_skip = 0;
        t->pass = 0;
        t->rskip_n = 0;
        t->rskip_lanes = 0;
        locks_restore(t);
    }
}

static void srec_stop(void)
{
    srec = 0;
    chain_n = 0;
    live_req = -1;
}

static void live_block(void)
{
    static uint32_t last_bar = 0xFFFFFFFFu;
    uint32_t bar = clk_beat >> 2;
    if (bar == last_bar)
        return;
    last_bar = bar;
    if (live_req >= 0) {
        live_sec = live_req;
        live_req = -1;
    }
}

static void seq_stop(void)
{
    uint32_t i;
    song.playing = 0;
    clk_pos = 0;
    clk_beat = 0;
    srec_stop();
    live_sec = -1;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        if (!is_drum(t))
            seq_release(t);
        t->seq_abs = SEQ_NONE;
        t->seq_idx = 0;
        t->seq_off = 0;
        t->seq_hold = 0;
        t->seq_skip = 0;
        t->rh_n = 0;
        locks_restore(t);
    }
    seq_out_all_off();
}

static int arp_step(track_t *t)
{
    uint32_t list[60 + 5], cnt, len = arp_list(t, list, &cnt), i, o, top = 0;
    if (!len)
        return 0;
    t->arp_idx++;
    if (t->p[P_AMODE] != AM_CHORD) {
        t->arp_note = (uint8_t)list[arp_pick(t, list, len)];
        arp_play(t, t->arp_note);
        return 1;
    }
    o = t->arp_idx % (uint32_t)(t->p[P_AOCT] > 0 ? t->p[P_AOCT] : 1);
    for (i = 0; i < cnt && i < 4u; i++) {
        uint32_t nt = (uint32_t)clamp((int32_t)list[i] + 12 * (int32_t)o, 0, 127);

        if (nt == 127u && top++)
            continue;
        if (!t->arp_note)
            t->arp_note = (uint8_t)nt;
        else
            t->arp_ch[i - 1u] = (uint8_t)nt;
        arp_play(t, nt);
    }
    return 1;
}

static void arp_tick(track_t *t, uint32_t adv)
{
    uint32_t div = (uint32_t)t->p[P_ARATE] % NDIV_SHORT, u = div_units(div), into, slen, abs = 0, fire = 0;
    if (t->arp_note) {
        if (t->arp_off <= adv) {
            arp_end(t);
        } else {
            t->arp_off -= adv;
        }
    }
    if (!t->p[P_AMODE] || !t->nheld) {
        if (!t->nheld && t->arp_note)
            arp_end(t);
        t->arp_new = 0;
        return;
    }
    if (song.playing) {
        abs = grid_at(div, swings(div) ? swing_units(t->p[P_ASWING], u) : 0u, &into, &slen);
        if (t->arp_new) {
            t->arp_new = 0;
            t->arp_abs = into * 4u >= slen * 3u ? abs : abs - 1u;
        } else if (div != t->arp_den) {
            t->arp_abs = abs;
        }
        t->arp_den = (uint8_t)div;
        if (abs + 1u == t->arp_abs)
            abs = t->arp_abs;
        fire = abs != t->arp_abs;
        t->arp_abs = abs;
    } else {
        uint32_t sw = swings(div) ? swing_units(t->p[P_ASWING], u) : 0u, lim;
        if (t->arp_new) {
            t->arp_new = 0;
            t->arp_odd = 0;
            t->arp_pos = u - sw;
        }
        lim = t->arp_odd ? u + sw : u - sw;
        t->arp_pos += adv;
        if (t->arp_pos >= lim) {
            t->arp_pos = t->arp_pos - lim < lim ? t->arp_pos - lim : 0;
            t->arp_odd ^= 1u;
            fire = 1;
        }
        slen = t->arp_odd ? u + sw : u - sw;
    }
    if (!fire)
        return;
    arp_end(t);
    if ((uint32_t)(rng() & 127u) <= (uint32_t)t->p[P_APROB] && arp_step(t))
        t->arp_off = slen * (uint32_t)t->p[P_AGATE] / 128u;
}
static void arm_start(track_t *t)
{
    if (!rec_wait || t != TSEL || song.playing || ci_on)
        return;
    if (project_empty() && !rec_tempo) {
        ft_start(t);
    } else if (rec_count) {
        return;
    } else {
#if FELUCCA_ARRANGER
        arrangement_enabled = 0;
#endif
        seq_start();
        if (song.playing)
            rec_begin();
    }
}

static void drum_input(uint32_t lane, uint32_t lvl, uint32_t rat, int rec);
#define REMOTE_CC0 102u
static volatile uint32_t remote_lbl, remote_up;
static void remote_cc(uint32_t k, uint32_t value)
{
    uint32_t bit = 1u << k;
    if (value >= 64u) {
        remote_lbl |= bit;
        remote_up &= ~bit;
    } else if (remote_lbl & bit) {
        remote_up |= bit;
    }
}
static uint32_t midi_drum_lane(uint32_t note)
{
    return song.g[G_DRMAP] ? (note + 128u - 36u) % 16u : lane_of_note(note);
}
static const uint8_t *in_chord;
static uint32_t in_chord_n, in_chord_i;
static uint8_t sus_on[NTRK];
static uint32_t sus_wait[NTRK][4];
static void input_off_now(track_t *t, uint32_t note);
static uint32_t kh_set[NTRK][4];
static uint32_t kh_mo[NTRK][4];
static uint8_t kh_mc[NTRK];
static uint8_t kh_phys[NTRK];
static int kh_on(const track_t *t) { return !t->p[P_AMODE] && t->p[P_AHOLD] && !is_drum(t); }
static int kh_has(const track_t *t, uint32_t note)
{
    return (kh_set[trk_index(t) % NTRK][(note >> 5) & 3u] >> (note & 31u)) & 1u;
}
static int kh_any(const track_t *t)
{
    const uint32_t *m = kh_set[trk_index(t) % NTRK];
    return (m[0] | m[1] | m[2] | m[3]) != 0u;
}
static uint32_t live_held[NTRK][4];
static uint32_t mi_trk_held[NTRK][4];
static volatile uint8_t mono_hint;
static void live_set(const track_t *t, uint32_t note, int on)
{
    uint32_t *w = &live_held[trk_index(t) % NTRK][(note >> 5) & 3u], bit = 1u << (note & 31u);
    *w = on ? *w | bit : *w & ~bit;
}

static int live_has(const track_t *t, uint32_t note)
{
    uint32_t ti = trk_index(t) % NTRK, w = (note >> 5) & 3u, bit = 1u << (note & 31u);
    return ((live_held[ti][w] | sus_wait[ti][w]) & bit) != 0u || (kh_on(t) && kh_has(t, note));
}
static int seq_has(const track_t *t, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < t->seq_n && i < 4u; i++)
        if (t->seq_notes[i] == note)
            return 1;
    return 0;
}
static void kh_release(track_t *t)
{
    uint32_t ti = trk_index(t) % NTRK, w, b;
    for (w = 0; w < 4u; w++)
        for (b = 0; b < 32u; b++)
            if ((kh_set[ti][w] >> b) & 1u) {
                uint32_t n = w * 32u + b;
                if (ft_on && t == &trk[ft_trk % NTRK])
                    ft_note_off(n);
                rec_release(t, n);
                live_set(t, n, 0);
                trk_note_off(t, n);
                if ((kh_mo[ti][w] >> b) & 1u)
                    midi_out_note(kh_mc[ti], n, 0);
            }
    memset(kh_set[ti], 0, sizeof kh_set[ti]);
    memset(kh_mo[ti], 0, sizeof kh_mo[ti]);
    kh_phys[ti] = 0;
}
static void input_on(track_t *t, uint32_t note, uint32_t vel)
{
    if (is_drum(t)) {
        drum_input(midi_drum_lane(note), vel_lvl(vel), 0, 1);
        return;
    }
    last_note = (uint8_t)note;
    {
        uint32_t *w = &sus_wait[trk_index(t) % NTRK][(note >> 5) & 3u], bit = 1u << (note & 31u);
        if (*w & bit) {
            *w &= ~bit;
            input_off_now(t, note);
        }
    }
    arm_start(t);
    if (kh_on(t)) {
        uint32_t ti = trk_index(t) % NTRK;
        if (!kh_phys[ti])
            kh_release(t);
        if (kh_phys[ti] < 255u)
            kh_phys[ti]++;
        if (kh_has(t, note))
            return;
        kh_set[ti][(note >> 5) & 3u] |= 1u << (note & 31u);
    }
    if (ft_on && t == &trk[ft_trk % NTRK])
        ft_note_on(note, vel);
    if (t->p[P_AMODE]) {
        arp_add(t, note);
        return;
    }
    if (t->p[P_VOICE] != V_POLY && (((song.rec >> trk_index(t)) & 1u) || (ft_on && ft_trk == trk_index(t)))) {
        const uint32_t *h = live_held[trk_index(t) % NTRK];
        if (h[0] | h[1] | h[2] | h[3])
            mono_hint = 1;
    }
    live_set(t, note, 1);
    if (((song.rec >> trk_index(t)) & 1u) && song.playing)
        rec_note(t, note, vel, 0, 1);
    if (in_chord)
        trk_note_chord(t, in_chord, in_chord_n, in_chord_i, vel);
    else
        trk_note_on(t, note, vel);
}
static void input_off(track_t *t, uint32_t note)
{
    if (is_drum(t)) {
        if (ft_on && ft_trk == TRK_DRUM)
            ft_note_off(midi_drum_lane(note));
        return;
    }
    if (kh_on(t) && kh_has(t, note)) {
        if (kh_phys[trk_index(t) % NTRK])
            kh_phys[trk_index(t) % NTRK]--;
        return;
    }
    if (sus_on[trk_index(t) % NTRK]) {
        sus_wait[trk_index(t) % NTRK][(note >> 5) & 3u] |= 1u << (note & 31u);
        return;
    }
    input_off_now(t, note);
}
static void input_off_now(track_t *t, uint32_t note)
{
    if (ft_on && t == &trk[ft_trk % NTRK])
        ft_note_off(note);
    rec_release(t, note);
    arp_remove(t, note);
    live_set(t, note, 0);
    if (!(song.playing && seq_has(t, note)))
        trk_note_off(t, note);
}

static void drum_input(uint32_t lane, uint32_t lvl, uint32_t rat, int rec)
{
    track_t *t = TDRUM;
    lane &= 15u;
    pen_lane = (uint8_t)lane;
    arm_start(t);
    if (ft_on && ft_trk == TRK_DRUM)
        ft_note_on(lane, lvl);
    if (rec && ((song.rec >> TRK_DRUM) & 1u) && song.playing)
        rec_hit(t, lane, lvl, rat);
    trk_note_on(t, LANE_NOTE[lane], lvl_vel(lvl, 100));
}

static const uint8_t ROLL_DEN[5] = {2, 4, 8, 12, 16};
#define NROLL 4u
static struct {
    uint8_t on, key, trk, note, lvl;
    uint32_t last;
    uint32_t off;
    uint32_t rec_abs;
} roll[NROLL];
static uint32_t roll_den(void) { return ROLL_DEN[(uint32_t)clamp(song.g[G_ROLL], 0, 4)]; }
static uint32_t roll_lanes(const track_t *t)
{
    uint32_t r, m = 0;
    for (r = 0; r < NROLL; r++)
        if (roll[r].on && roll[r].trk == trk_index(t))
            m |= 1u << (roll[r].note & 15u);
    return m;
}
static int roll_has(const track_t *t, uint32_t note)
{
    uint32_t r;
    for (r = 0; r < NROLL; r++)
        if (roll[r].on && roll[r].trk == trk_index(t) && roll[r].note == note)
            return 1;
    return 0;
}
static void roll_hit(uint32_t r)
{
    track_t *t = &trk[roll[r].trk % NTRK];
    uint32_t u = BEAT_U / roll_den(), su = div_units((uint32_t)t->p[P_SDIV]);
    uint32_t hits = su / u, rat = hits > 4u ? 3u : hits > 1u ? hits - 1u : 0u;
    uint32_t armed = ((song.rec >> trk_index(t)) & 1u) && song.playing, later, abs = 0, rec = 0;
    if (armed) {
        abs = rec_target(t, &later);
        rec = abs != roll[r].rec_abs;
        roll[r].rec_abs = abs;
    }
    if (is_drum(t)) {
        roll_rec = 1;
        drum_input(roll[r].note, roll[r].lvl, rat, rec || !armed);
        roll_rec = 0;
        seq_out_on(t, LANE_NOTE[roll[r].note & 15u], lvl_vel(roll[r].lvl, 100));
        return;
    }
    arm_start(t);
    if (roll[r].off)
        trk_note_off(t, roll[r].note);
    trk_note_on(t, roll[r].note, lvl_vel(roll[r].lvl, 100));
    seq_out_on(t, roll[r].note, lvl_vel(roll[r].lvl, 100));
    roll[r].off = u / 2u;
    if (rec)
        rec_note(t, roll[r].note, 100, rat, 0);
    if (ft_on && t == &trk[ft_trk % NTRK])
        ft_note_on(roll[r].note, 100), ft_note_off(roll[r].note);
}

static void roll_start(uint32_t k, track_t *t, uint32_t note, uint32_t lvl)
{
    uint32_t r, den = roll_den(), into, slen, abs;
    for (r = 0; r < NROLL && roll[r].on; r++)
        ;
    if (r == NROLL)
        return;
    roll[r].on = 1;
    roll[r].key = (uint8_t)k;
    roll[r].trk = (uint8_t)trk_index(t);
    roll[r].note = (uint8_t)note;
    roll[r].lvl = (uint8_t)lvl;
    roll[r].off = 0;
    roll[r].rec_abs = SEQ_NONE;
    if (song.playing) {
        abs = grid_den(den, 0, &into, &slen);
        roll[r].last = abs;
        if (into * 4u >= slen * 3u)
            return;
    } else {
        roll[r].last = 0;
    }
    roll_hit(r);
}

static void roll_end(uint32_t r)
{
    if (roll[r].on && roll[r].off && roll[r].trk != TRK_DRUM) {
        trk_note_off(&trk[roll[r].trk % NTRK], roll[r].note);
        seq_out_off(&trk[roll[r].trk % NTRK], roll[r].note);
    }
    if (roll[r].on && roll[r].trk == TRK_DRUM)
        seq_out_off(&trk[TRK_DRUM], LANE_NOTE[roll[r].note & 15u]);
    roll[r].on = 0;
}

static void roll_block(uint32_t adv)
{
    uint32_t r, den = roll_den(), u = BEAT_U / den, into, slen;
    for (r = 0; r < NROLL; r++) {
        if (!roll[r].on)
            continue;
        if (roll[r].off) {
            if (roll[r].off <= adv) {
                trk_note_off(&trk[roll[r].trk % NTRK], roll[r].note);
                seq_out_off(&trk[roll[r].trk % NTRK], roll[r].note);
                roll[r].off = 0;
            } else {
                roll[r].off -= adv;
            }
        }
        if (song.playing) {
            uint32_t abs = grid_den(den, 0, &into, &slen);
            if (abs != roll[r].last) {
                roll[r].last = abs;
                roll_hit(r);
            }
        } else {
            roll[r].last += adv;
            if (roll[r].last >= u) {
                roll[r].last = roll[r].last - u < u ? roll[r].last - u : 0;
                roll_hit(r);
            }
        }
    }
}
static uint32_t key_lvl(void)
{
    uint32_t b = fm1_in.buttons;
    return (b & dyn_bit[0]) ? LV_GHOST : (b & dyn_bit[1]) ? LV_HARD : LV_NORM;
}

static uint32_t chord_mods(void)
{
    uint32_t k, m = 0;
    for (k = 0; k < 27u; k++)
        if (kb_kind[k] == KS_MOD)
            m |= chord_mod_of_key(k);
    return m;
}
static void chord_revoice(uint32_t sel)
{
    uint32_t k, i, j, mods = chord_mods(), mc = trk_midi_ch(sel);
    track_t *t = &trk[sel % NTRK];
    for (k = 0; k < 27u; k++) {
        uint8_t nw[4];
        uint32_t nn;
        if (kb_kind[k] != KS_NOTE || kb_trk[k] != sel || !t->p[P_CHORD] || is_drum(t))
            continue;
        nn = chord_play_notes(t, kb_root[k], mods, nw);
        for (i = 0; i < kb_n[k]; i++) {
            for (j = 0; j < nn && nw[j] != kb_nt[k][i]; j++)
                ;
            if (j == nn) {
                uint32_t n = kb_nt[k][i], ti = sel % NTRK;
                if (kh_on(t) && kh_has(t, n)) {
                    kh_set[ti][(n >> 5) & 3u] &= ~(1u << (n & 31u));
                    kh_mo[ti][(n >> 5) & 3u] &= ~(1u << (n & 31u));
                    if (kh_phys[ti])
                        kh_phys[ti]--;
                    input_off_now(t, n);
                } else {
                    input_off(t, n);
                }
                midi_out_note(mc, n, 0);
            }
        }
        for (j = 0; j < nn; j++) {
            for (i = 0; i < kb_n[k] && kb_nt[k][i] != nw[j]; i++)
                ;
            if (i == kb_n[k]) {
                in_key = (uint8_t)(k + 1u);
                input_on(t, nw[j], 100);
                in_key = 0;
                midi_out_note(mc, (uint32_t)nw[j], 100u);
            }
        }
        memcpy(kb_nt[k], nw, nn);
        kb_n[k] = (uint8_t)nn;
    }
}

static uint8_t mfx_key;
static void midi_fx(uint32_t note, int on)
{
    int32_t fx;
    uint32_t k;
    if (note < 53u || note > 79u)
        return;
    k = note - 53u;
    fx = punch_fx_of_key(k);
    if (fx < 0 || fx >= (int32_t)PUNCH_NFX)
        return;
    if (on) {
        punch.req = (int8_t)fx;
        punch.keybit = 0;
        mfx_key = (uint8_t)(k + 1u);
    } else if (mfx_key == k + 1u) {
        mfx_key = 0;
        if (!punch.keybit)
            punch.req = -1;
    }
}

static void key_down(uint32_t k)
{
    uint32_t layer = layer_now(), sel = song.sel % NTRK, i, mc;
    track_t *t = &trk[sel];
    kb_kind[k] = KS_NONE;
    kb_trk[k] = (uint8_t)sel;
    kb_n[k] = 0;
    switch (layer) {
    case LY_FX: {
        int32_t fx = punch_fx_of_key(k);
        kb_kind[k] = KS_FX;
        if (fx >= 0 && fx < (int32_t)PUNCH_NFX) {
            punch.req = (int8_t)fx;
            punch.keybit = 1u << k;
        }
        return;
    }
    case LY_STEP:
    case LY_SCALE:
    case LY_MIX:
    case LY_SONG:
        kb_kind[k] = KS_UI;
        kb_nt[k][0] = (uint8_t)layer;
        lk_push(layer, k, 1);
        return;
    case LY_ERASE:
        if (is_drum(t) && punch_key(k) < 0) {
            er_mute = (uint8_t)!er_mute;
            return;
        }
        if (is_drum(t) && er_mute) {
            drum_mute ^= (uint16_t)(1u << lane_of_key(k));
            return;
        }
        kb_kind[k] = KS_ERASE;
        if (!erasing(t) || er_trk != sel)
            undo_erase_sess = (undo_sess += 4u) | 2u;
        if (er_trk != sel) {
            er_lanes = 0;
            er_notes[0] = er_notes[1] = er_notes[2] = er_notes[3] = 0;
            er_trk = (uint8_t)sel;
        }
        if (is_drum(t)) {
            kb_nt[k][0] = (uint8_t)lane_of_key(k);
            kb_n[k] = 1;
            er_lanes |= (uint16_t)(1u << kb_nt[k][0]);
        } else {
            uint32_t n = kb_map(t, k);
            if (n == KB_SILENT)
                return;
            kb_n[k] = (uint8_t)(t->p[P_CHORD] ? chord_notes(t, n, kb_nt[k]) : 1u);
            if (!t->p[P_CHORD])
                kb_nt[k][0] = (uint8_t)n;
            for (i = 0; i < kb_n[k]; i++)
                er_notes[(kb_nt[k][i] >> 5) & 3u] |= 1u << (kb_nt[k][i] & 31u);
        }
        erase_now(t);
        return;
    default:
        break;
    }
    if (is_drum(t) && layer == LY_PLAY && kb_grid) {
        kb_kind[k] = KS_UI;
        kb_nt[k][0] = (uint8_t)KB_GRID;
        lk_push(KB_GRID, k, 1);
        return;
    }
    if (is_drum(t)) {
        uint32_t lane = lane_of_key(k), lvl = key_lvl();
        kb_nt[k][0] = (uint8_t)lane;
        kb_n[k] = 1;
        if (layer == LY_ROLL) {
            kb_kind[k] = KS_ROLL;
            roll_start(k, t, lane, lvl);
            return;
        }
        kb_kind[k] = KS_DRUM;
        drum_input(lane, lvl, 0, 1);
        mc = trk_midi_ch(sel);
        midi_out_note(mc, (uint32_t)LANE_NOTE[lane], lvl_vel(lvl, 100));
        return;
    }
    {
        uint32_t n = kb_map(t, k);
        if (n == KB_SILENT) {
            if (t->p[P_CHORD] && layer == LY_PLAY && chord_mod_of_key(k)) {
                kb_kind[k] = KS_MOD;
                chord_revoice(sel);
            }
            return;
        }
        if (layer == LY_ROLL) {
            kb_kind[k] = KS_ROLL;
            kb_nt[k][0] = (uint8_t)n;
            kb_n[k] = 1;
            roll_start(k, t, n, LV_NORM);
            return;
        }
        kb_kind[k] = KS_NOTE;
        kb_root[k] = (uint8_t)n;
        kb_oct[k] = (int8_t)song.octave;
        if (t->p[P_CHORD]) {
            kb_n[k] = (uint8_t)chord_play_notes(t, n, chord_mods(), kb_nt[k]);
        } else {
            kb_nt[k][0] = (uint8_t)n;
            kb_n[k] = 1;
        }
        mc = trk_midi_ch(sel);
        in_key = (uint8_t)(k + 1u);
        for (i = 0; i < kb_n[k]; i++) {
            in_chord = kb_n[k] > 1u ? kb_nt[k] : 0;
            in_chord_n = kb_n[k];
            in_chord_i = i;
            input_on(t, kb_nt[k][i], 100);
            in_chord = 0;
            midi_out_note(mc, (uint32_t)kb_nt[k][i], 100u);
        }
        in_key = 0;
        if (!(kb_prev & ~(1u << k)) || pen_n >= 4u)
            pen_n = 0;
        for (i = 0; i < kb_n[k] && pen_n < 4u; i++)
            pen_note[pen_n++] = kb_nt[k][i];
    }
}

static void key_up(uint32_t k)
{
    uint32_t i, mc, kind = kb_kind[k];
    track_t *t = &trk[kb_trk[k] % NTRK];
    kb_kind[k] = KS_NONE;
    switch (kind) {
    case KS_FX:
        if (punch.keybit == 1u << k) {
            punch.keybit = 0;
            punch.req = -1;
        }
        return;
    case KS_UI:
        lk_push(kb_nt[k][0], k, 0);
        return;
    case KS_MOD:
        chord_revoice(kb_trk[k]);
        return;
    case KS_ERASE:
        if (is_drum(t))
            er_lanes &= (uint16_t)~(1u << kb_nt[k][0]);
        else
            for (i = 0; i < kb_n[k]; i++)
                er_notes[(kb_nt[k][i] >> 5) & 3u] &= ~(1u << (kb_nt[k][i] & 31u));
        return;
    case KS_ROLL:
        for (i = 0; i < NROLL; i++)
            if (roll[i].on && roll[i].key == k)
                roll_end(i);
        return;
    case KS_NOTE:
        mc = trk_midi_ch(kb_trk[k] % NTRK);
        for (i = 0; i < kb_n[k]; i++) {
            uint32_t n = kb_nt[k][i];
            input_off(t, n);
            if (kh_on(t) && kh_has(t, n)) {
                kh_mo[kb_trk[k] % NTRK][(n >> 5) & 3u] |= 1u << (n & 31u);
                kh_mc[kb_trk[k] % NTRK] = (uint8_t)mc;
            } else {
                midi_out_note(mc, n, 0);
            }
        }
        return;
    case KS_DRUM:
        if (ft_on && ft_trk == TRK_DRUM)
            ft_note_off(kb_nt[k][0]);
        mc = trk_midi_ch(TRK_DRUM);
        midi_out_note(mc, (uint32_t)LANE_NOTE[kb_nt[k][0] & 15u], 0);
        return;
    default:
        return;
    }
}

static volatile uint32_t aud_lanes, aud_lvl;
static void audition_req(uint32_t lanes, uint32_t lvls)
{
    fm1_irq_off();
    aud_lvl = lvls;
    aud_lanes = lanes & 0xFFFFu;
    fm1_irq_on();
}
static void audition_lane(uint32_t lane) { audition_req(1u << (lane & 15u), 0u); }
static void audition_step(const dstep_t *s)
{
    uint32_t m = dstep_mask(s), lv = 0, l;
    for (l = 0; l < 16u; l++)
        if ((m >> l) & 1u)
            lv |= dstep_lvl(s, l) << (2u * l);
    audition_req(m, lv);
}
static void audition_block(void)
{
    uint32_t m = aud_lanes, lv = aud_lvl, l;
    if (!m)
        return;
    aud_lanes = 0;
    for (l = 0; m; l++, m >>= 1)
        if (m & 1u)
            trk_note_on(TDRUM, LANE_NOTE[l], lvl_vel((lv >> (2u * l)) & 3u, 100));
}

static void keyboard_block(void)
{
    uint32_t cur = fm1_in.notes, ch = cur ^ kb_prev, k, r;
    audition_block();
    if (!(layer_buttons() & ly_bit[LY_ROLL]))
        for (r = 0; r < NROLL; r++)
            if (roll[r].on && roll[r].key < 27u)
                roll_end(r);
    if (!ch)
        return;
    for (k = 0; k < 27u; k++) {
        if (!((ch >> k) & 1u))
            continue;
        if ((cur >> k) & 1u)
            key_down(k);
        else
            key_up(k);
        kb_prev ^= 1u << k;
    }
}

static void rec_follow(uint32_t sel)
{
    if (song.rec)
        song.rec = (uint8_t)(1u << (sel % NTRK));
}
static void seq_step(track_t *t, const step_t *s, uint32_t slen, uint32_t skip)
{
    uint32_t i, j, gate = slen * (uint32_t)t->p[P_SGATE] / 128u;
    uint32_t slide_in = t->seq_hold && t->seq_n;
    const step_t *nx = &t->step[(t->seq_idx + 1u) % trk_len(t)];
    uint32_t next_tie = nx->time == ST_TIE || (nx->time == ST_NOTE && (nx->flags & (SF_CONT_ALL | SF_LEG)));
    if (s->time == ST_TIE) {
        if (t->seq_n) {
            t->seq_off = gate + slen / 2u;
            t->seq_hold = (s->flags & SF_SLIDE) != 0 || next_tie;
        }
        return;
    }
    if (s->time == ST_REST || !s->n) {
        seq_release(t);
        return;
    }
    if (s->flags & SF_CONT_ALL) {
        uint8_t old[4];
        uint32_t no = t->seq_n > 4u ? 4u : t->seq_n, kept = 0;
        memcpy(old, t->seq_notes, no);
        for (j = 0; j < no; j++) {
            for (i = 0; i < s->n && !((s->flags & SF_CONT(i)) && s->note[i] == old[j] && !((kept >> i) & 1u)); i++)
                ;
            if (i < s->n) {
                kept |= 1u << i;
            } else {
                if (!live_has(t, old[j]))
                    trk_note_off(t, old[j]);
                seq_out_off(t, old[j]);
            }
        }
        t->slide_glide = 0;
        for (i = 0; i < s->n; i++)
            if ((s->flags & SF_CONT(i)) && !((kept >> i) & 1u) && live_has(t, s->note[i]))
                skip |= 1u << i;
        for (i = 0; i < s->n; i++)
            if (!((kept >> i) & 1u) && !((skip >> i) & 1u) && !roll_has(t, s->note[i])) {
                trk_note_on(t, s->note[i], step_vel(s, i));
                seq_out_on(t, s->note[i], step_vel(s, i));
            }
        t->seq_n = 0;
        for (i = 0; i < s->n; i++)
            if (!((skip >> i) & 1u))
                t->seq_notes[t->seq_n++] = s->note[i];
        t->seq_off = step_pure_cont(s) ? gate + slen / 2u : gate;
        t->seq_hold = (s->flags & SF_SLIDE) != 0 || next_tie;
        return;
    }
    if (s->rat) {
        uint32_t hits = 1u + ((s->rat >> 0) & 3u);
        for (i = 1; i < s->n; i++)
            if (1u + ((s->rat >> (2u * i)) & 3u) > hits)
                hits = 1u + ((s->rat >> (2u * i)) & 3u);
        gate /= hits;
    }
    t->slide_glide = (uint8_t)(slide_in && (!(s->flags & SF_LEG) ||
                                            (t->step[(t->seq_idx + trk_len(t) - 1u) % trk_len(t)].flags & SF_SLIDE)));
    if (!slide_in)
        seq_release(t);
    for (i = 0; i < s->n; i++)
        if (roll_has(t, s->note[i]))
            skip |= 1u << i;
    for (i = 0; i < s->n; i++)
        if (!((skip >> i) & 1u)) {
            if (slide_in)
                trk_note_on(t, s->note[i], step_vel(s, i));
            else
                trk_note_chord(t, s->note, s->n, i, step_vel(s, i));
            if (!slide_in || !(mo_set[trk_index(t) % NTRK][s->note[i] >> 5] & (1u << (s->note[i] & 31u))))
                seq_out_on(t, s->note[i], step_vel(s, i));
        }
    if (slide_in)
        for (i = 0; i < t->seq_n; i++) {
            for (j = 0; j < s->n && s->note[j] != t->seq_notes[i]; j++)
                ;
            if (j == s->n) {
                if (!live_has(t, t->seq_notes[i]))
                    trk_note_off(t, t->seq_notes[i]);
                seq_out_off(t, t->seq_notes[i]);
            }
        }
    t->seq_n = 0;
    for (i = 0; i < s->n; i++)
        if (!((skip >> i) & 1u))
            t->seq_notes[t->seq_n++] = s->note[i];
    t->seq_off = gate;
    t->seq_hold = !s->rat && ((s->flags & SF_SLIDE) != 0 || next_tie);
}

static void drum_step(track_t *t, const dstep_t *s, uint32_t skip)
{
    uint32_t l, m = dstep_mask(s) & ~skip & ~roll_lanes(t) & ~(uint32_t)drum_mute;
    seq_out_track_off(t);
    for (l = 0; m; l++, m >>= 1)
        if (m & 1u) {
            trk_note_on(t, LANE_NOTE[l], lvl_vel(dstep_lvl(s, l), 100));
            seq_out_on(t, LANE_NOTE[l], lvl_vel(dstep_lvl(s, l), 100));
        }
}
#include "audilune_gen.c"

static void seq_ratchets(track_t *t, uint32_t into, uint32_t slen)
{
    uint32_t i;
    if (t->seq_skip)
        return;
    if (is_drum(t)) {
        const dstep_t *s = &t->dstep[t->seq_idx % NSTEP];
        uint32_t m = dstep_mask(s) & ~roll_lanes(t) & ~(uint32_t)drum_mute;
        for (i = 0; m; i++, m >>= 1) {
            uint32_t hits = 1u + dstep_rat(s, i), h, done;
            if (!(m & 1u) || hits == 1u)
                continue;
            h = into * hits / slen;
            done = (t->rat_lanes >> (2u * i)) & 3u;
            if (h > done && h < hits) {
                t->rat_lanes = (t->rat_lanes & ~(3u << (2u * i))) | h << (2u * i);
                trk_note_on(t, LANE_NOTE[i], lvl_vel(dstep_lvl(s, i), 100));
                seq_out_on(t, LANE_NOTE[i], lvl_vel(dstep_lvl(s, i), 100));
            }
        }
        return;
    }
    {
        const step_t *s = &t->step[t->seq_idx % NSTEP];
        if (s->time != ST_NOTE || !s->rat)
            return;
        for (i = 0; i < s->n; i++) {
            uint32_t hits = 1u + ((s->rat >> (2u * i)) & 3u), h;
            if (hits == 1u || roll_has(t, s->note[i]))
                continue;
            h = into * hits / slen;
            if (h > t->rat_done[i] && h < hits) {
                uint32_t j;
                t->rat_done[i] = (uint8_t)h;
                trk_note_off(t, s->note[i]);
                trk_note_on(t, s->note[i], step_vel(s, i));
                seq_out_on(t, s->note[i], step_vel(s, i));
                t->seq_off = slen / hits * (uint32_t)t->p[P_SGATE] / 128u;
                for (j = 0; j < t->seq_n && t->seq_notes[j] != s->note[i]; j++)
                    ;
                if (j == t->seq_n && t->seq_n < 4u)
                    t->seq_notes[t->seq_n++] = s->note[i];
            }
        }
    }
}
static int32_t micro_units(const track_t *t, uint32_t abs, uint32_t slen)
{
    int32_t m = t->micro[abs % trk_len(t) % NSTEP];
    return (int32_t)(slen / 64u) * m;
}

static void seq_tick(track_t *t, uint32_t adv)
{
    uint32_t len = trk_len(t), into, slen, abs, idx, nabs, fire = 0;
    int32_t rel;
    if (t->seq_n && !t->seq_hold) {
        if (t->seq_off <= adv)
            seq_release(t);
        else
            t->seq_off -= adv;
    }
    if (!song.playing)
        return;
    abs = trk_grid(t, &into, &slen);
    {
        uint32_t div = trk_div(t);
        if (t->seq_abs != SEQ_NONE && div != t->seq_den)
            t->seq_abs = abs;
        t->seq_den = (uint8_t)div;
    }
    if (t->seq_abs == SEQ_NONE) {
        nabs = abs;
        fire = micro_units(t, nabs, slen) <= (int32_t)into;
    } else {
        int32_t mu;
        nabs = t->seq_abs + 1u;
        mu = micro_units(t, nabs, slen);
        if (abs == nabs)
            fire = mu <= (int32_t)into;
        else if (abs + 1u == nabs)
            fire = mu < 0 && (int32_t)slen + mu <= (int32_t)into;
        else if ((int32_t)(abs - nabs) > 0)
            fire = 1;
    }
    if (fire) {
        t->seq_abs = nabs;
        idx = nabs % len;
        t->seq_idx = (uint16_t)idx;
        t->rat_done[0] = t->rat_done[1] = t->rat_done[2] = t->rat_done[3] = 0;
        t->rat_lanes = 0;
        if (!idx)
            t->pass++;
        if (erasing(t))
            erase_step(t, idx);
        t->seq_skip = (uint8_t)!step_plays(t, idx);
        if (t->seq_skip) {
            lock_step(t, NSTEP);
            audilune_step_fx_apply(t);
            t->rskip_lanes = 0;
            t->rskip_n = 0;
            if (is_drum(t)) {
                seq_out_track_off(t);
            } else {
                rec_hold(t, idx, len, nabs);
                seq_release(t);
            }
        } else if (is_drum(t)) {
            uint32_t skip = t->rskip_abs == nabs ? t->rskip_lanes : 0u;
            lock_step(t, idx);
            audilune_step_fx_apply(t);
            t->rskip_lanes = 0;
            drum_step(t, &t->dstep[idx], skip);
        } else {
            const step_t *s = &t->step[idx];
            uint32_t skip = 0, i, k;
            lock_step(t, idx);
            audilune_step_fx_apply(t);
            rec_hold(t, idx, len, nabs);
            if (t->rskip_n && t->rskip_abs == nabs)
                for (i = 0; i < s->n; i++)
                    for (k = 0; k < t->rskip_n; k++)
                        if (s->note[i] == t->rskip[k])
                            skip |= 1u << i;
            t->rskip_n = 0;
            seq_step(t, s, slen, skip);
        }
    }
    if (t->seq_abs == abs)
        rel = (int32_t)into - micro_units(t, abs, slen);
    else if (t->seq_abs == abs + 1u)
        rel = (int32_t)into - ((int32_t)slen + micro_units(t, abs + 1u, slen));
    else
        rel = (int32_t)into;
    if (t->seq_abs != SEQ_NONE)
        seq_ratchets(t, rel < 0 ? 0u : (uint32_t)rel, slen);
}
static track_t *midi_own(uint32_t ch)
{
    uint32_t i;
    if (song.g[G_DRCH] && ch + 1u == (uint32_t)song.g[G_DRCH])
        return TDRUM;
    for (i = 0; i < NPART; i++)
        if (ch + 1u == (uint32_t)song.g[G_CH1 + i])
            return &trk[i];
    return 0;
}
static track_t *midi_track(uint32_t ch)
{
    track_t *t = midi_own(ch);
    return t ? t : song.g[G_CHOTH] ? 0 : TSEL;
}

static uint8_t midi_sel_on[16][64];
static inline uint32_t midi_sel_get(uint32_t ch, uint32_t note)
{
    return (midi_sel_on[ch & 15u][(note & 127u) >> 1] >> (4u * (note & 1u))) & 15u;
}
static inline void midi_sel_set(uint32_t ch, uint32_t note, uint32_t v)
{
    uint8_t *b = &midi_sel_on[ch & 15u][(note & 127u) >> 1];
    uint32_t sh = 4u * (note & 1u);
    *b = (uint8_t)((*b & ~(15u << sh)) | ((v & 15u) << sh));
}
static track_t *midi_route(uint32_t ch, uint32_t note, int on)
{
    track_t *t = midi_own(ch);
    if (t)
        return t;
    if (song.g[G_CHOTH] && (on || !midi_sel_get(ch, note)))
        return 0;
    t = TSEL;
    if (on)
        midi_sel_set(ch, note, song.sel + 1u);
    else if (midi_sel_get(ch, note)) {
        t = &trk[(midi_sel_get(ch, note) - 1u) % NTRK];
        midi_sel_set(ch, note, 0);
    }
    return t;
}

static uint8_t ch_sus[16], ch_bend[16];
static uint8_t ch_bank[16];
static void sus_set(track_t *t, int on);
static void midi_pedal(uint32_t ch, track_t *t, uint32_t value)
{
    uint32_t was = ch_sus[ch & 15u];
    if (was && was - 1u != trk_index(t))
        sus_set(&trk[(was - 1u) % NTRK], 0);
    ch_sus[ch & 15u] = (uint8_t)(value >= 64u ? trk_index(t) + 1u : 0u);
}
static void midi_bend(uint32_t ch, track_t *t, uint32_t d1, uint32_t d2)
{
    uint32_t was = ch_bend[ch & 15u];
    if (was && was - 1u != trk_index(t))
        trk[(was - 1u) % NTRK].bend = 0;
    t->bend = (int16_t)((int32_t)(d1 | d2 << 7) - 8192);
    ch_bend[ch & 15u] = (uint8_t)(trk_index(t) + 1u);
}
#define MCC_RES 0xFFu
#define MCC_CUT 0xFEu
static const uint8_t MIDI_CC_MAP[][2] = {
    {5, P_GLIDE}, {7, P_LEVEL}, {10, P_PAN}, {71, MCC_RES}, {72, P_REL}, {73, P_ATK}, {74, MCC_CUT}, {75, P_DEC},
    {91, P_REV}, {93, P_CHOR}, {94, P_DLY},
};
#define ML_N 16u
#define ML_E0 120u
_Static_assert(P_E0 < ML_E0 && P_COUNT - P_E0 == 8, "MIDI LEARN: a code per parameter");
static volatile uint16_t ml_tab[ML_N];
static volatile uint8_t ml_arm, ml_heard;
static uint32_t ml_id(uint32_t code)
{
    return code >= ML_E0 ? (code - ML_E0 < 8u ? P_E0 + code - ML_E0 : P_COUNT) : code && code <= P_E0 ? code - 1u : P_COUNT;
}
static int ml_free_cc(uint32_t cc)
{
    return cc && cc < 120u && cc != 1u && cc != 6u && cc != 11u && cc != 32u && cc != 38u && cc != 64u &&
           (cc < 96u || cc > 101u);
}
static void sus_set(track_t *t, int on)
{
    uint32_t ti = trk_index(t) % NTRK, n;
    if (is_drum(t))
        return;
    sus_on[ti] = (uint8_t)(on != 0);
    if (on)
        return;
    for (n = 0; n < 128u; n++)
        if ((sus_wait[ti][n >> 5] >> (n & 31u)) & 1u) {
            sus_wait[ti][n >> 5] &= ~(1u << (n & 31u));
            input_off(t, n);
        }
}
static void midi_cc_set(track_t *t, uint32_t id, uint32_t value)
{
    const param_desc_t *d = is_drum(t) && id == P_E0 ? &DRUM_KIT_DESC
                            : id >= P_E0 ? &ENGINES[t->eng_req % NENGINES]->edit[(id - P_E0) & 7u] : &TP[id % P_COUNT];
    if (d->max > d->min)
        t->p[id % P_COUNT] = (int16_t)(d->min + ((int32_t)(value & 127u) * (d->max - d->min) + 63) / 127);
}
static int __attribute__((noinline)) midi_learned(uint32_t cc, uint32_t value)
{
    uint32_t i, e, id, hit = 0;
    if (!ml_free_cc(cc)) {
        if (ml_arm && !ml_heard)
            ml_heard = (uint8_t)(cc + 1u);
        return 0;
    }
    if (ml_arm) {
        if (!ml_heard)
            ml_heard = (uint8_t)(cc + 1u);
        return 1;
    }
    for (i = 0; i < ML_N; i++) {
        e = ml_tab[i];
        if ((e >> 9) && (e & 127u) == cc) {
            if ((id = ml_id(e >> 9)) < P_COUNT)
                midi_cc_set(&trk[((e >> 7) & 7u) % NTRK], id, value);
            hit = 1;
        }
    }
    return hit;
}
static uint32_t cc74_slot(const engine_t *e)
{
    static const struct { const char *name; uint8_t slot; } T[] = {
        {"ANALOG", 4}, {"PHASE", 2}, {"LOFI", 7}, {"SAMPLE", 4}, {"TRIO", 5}, {"GRAIN", 7}, {"FM6", 2}, {"PHYS", 2},
        {"NOISE", 2}, {"SLICE", 7}};
    uint32_t i;
    for (i = 0; i < NELEM(T); i++)
        if (str_eq(e->name, T[i].name) && e->edit[T[i].slot].max > e->edit[T[i].slot].min &&
            !e->edit[T[i].slot].names)
            return T[i].slot;
    return 8u;
}
static void __attribute__((noinline)) midi_cc(track_t *t, uint32_t cc, uint32_t value)
{
    const param_desc_t *d = 0;
    int16_t *slot = 0;
    uint32_t i, id = 0xFFFFu;
    if (midi_learned(cc, value))
        return;
    if (cc == 64u) {
        sus_set(t, value >= 64u);
        return;
    }
    if (cc == 11u || cc == 121u) {
        ex_off[trk_index(t) % NTRK] = (uint8_t)(cc == 11u ? 127u - (value & 127u) : 0u);
        return;
    }
    for (i = 0; i < NELEM(MIDI_CC_MAP); i++)
        if (MIDI_CC_MAP[i][0] == cc)
            id = MIDI_CC_MAP[i][1];
    if (id == 0xFFFFu)
        return;
    if (id == MCC_CUT) {
        uint32_t k = is_drum(t) ? 8u : cc74_slot(ENGINES[t->eng_req % NENGINES]);
        if (k < 8u) {
            d = &ENGINES[t->eng_req % NENGINES]->edit[k];
            slot = &t->p[P_E0 + k];
        } else {
            id = P_TFLT;
        }
    }
    if (d) {
    } else if (is_drum(t)) {
        if (id == P_LEVEL || id == P_REV || id == P_DLY) {
            id = id == P_LEVEL ? G_DRLVL : id == P_REV ? G_DRREV : G_DRDLY;
            d = &GP[id];
            slot = &song.g[id];
        } else if (id == P_PAN || id == P_TFLT) {
            d = &TP[id];
            slot = &t->p[id];
        }
    } else if (id == MCC_RES) {
        const engine_t *e = ENGINES[t->eng_req % NENGINES];
        for (i = 0; i < 8u && !d; i++)
            if (str_eq(e->edit[i].label, "RES") || str_eq(e->edit[i].label, "Q")) {
                d = &e->edit[i];
                slot = &t->p[P_E0 + i];
            }
    } else {
        d = &TP[id];
        slot = &t->p[id];
    }
    if (!d || d->max <= d->min)
        return;
    *slot = (int16_t)(d->min + ((int32_t)value * (d->max - d->min) + 63) / 127);
}
#define MCLK_PULSE_U (BEAT_U / 24u)
#define MCLK_NB 8u
static struct {
    uint32_t pos, done;
    uint32_t last_ms, iv_ms;
    uint32_t beat_ms;
    uint8_t have, n24;
    uint8_t alive;
    uint8_t nb, bi;
    uint16_t bdt[MCLK_NB];
} mclk;

static int mclk_on(void)
{
    return song.g[G_SYNC] && mclk.alive && fm1_ms - mclk.last_ms < 500u;
}

static void mclk_tempo(uint32_t dt)
{
    uint32_t k, sum = 0, t10;
    int32_t one = (int32_t)((60000u + dt / 2u) / dt), cur = song.g[G_BPM];
    if (mclk.nb && (one - cur >= 5 || cur - one >= 5))
        mclk.nb = 0;
    mclk.bdt[mclk.bi] = (uint16_t)dt;
    mclk.bi = (uint8_t)((mclk.bi + 1u) % MCLK_NB);
    if (mclk.nb < MCLK_NB)
        mclk.nb++;
    for (k = 0; k < mclk.nb; k++)
        sum += mclk.bdt[(mclk.bi + MCLK_NB - 1u - k) % MCLK_NB];
    t10 = (600000u * mclk.nb + sum / 2u) / sum;
    if (mclk.nb < MCLK_NB || (int32_t)t10 - cur * 10 >= 8 || cur * 10 - (int32_t)t10 >= 8)
        song.g[G_BPM] = (int16_t)clamp((int32_t)((t10 + 5u) / 10u), 40, 240);
}

static void mclk_event(uint32_t st, uint32_t src)
{
    uint32_t now = fm1_ms;
    if (!song.g[G_SYNC] || src != (uint32_t)song.g[G_SYNC])
        return;
    if (st == 0xFAu || st == 0xFBu) {
        mclk.pos = mclk.done = 0;
        mclk.have = 0;
        if (st == 0xFAu)
            transport_req = 3;
        else if (!song.playing)
            song.playing = 1;
        return;
    }
    if (st == 0xFCu) {
        transport_req = 2;
        return;
    }
    if (st != 0xF8u)
        return;
    if (mclk.alive && now - mclk.last_ms < 200u) {
        uint32_t iv = now - mclk.last_ms;
        mclk.iv_ms = mclk.iv_ms ? (mclk.iv_ms * 3u + iv + 2u) / 4u : iv;
    }
    if (!mclk.alive || now - mclk.last_ms >= 500u) {
        mclk.n24 = 0;
        mclk.nb = 0;
        mclk.beat_ms = now;
    } else if (++mclk.n24 == 24u) {
        uint32_t dt = now - mclk.beat_ms;
        mclk.n24 = 0;
        mclk.beat_ms = now;
        if (dt >= 250u && dt <= 1500u)
            mclk_tempo(dt);
    }
    mclk.alive = 1;
    mclk.last_ms = now;
    if (song.playing || transport_req == 1u || transport_req == 3u) {
        if (mclk.have)
            mclk.pos += MCLK_PULSE_U;
        mclk.have = 1;
    }
}

static uint32_t mclk_adv(uint32_t n)
{
    uint32_t el, off = 0, tgt, adv, cap = n * 2u * (uint32_t)song.g[G_BPM];
    if (!mclk.have)
        return 0;
    el = fm1_ms - mclk.last_ms;
    if (mclk.iv_ms) {
        if (el > mclk.iv_ms)
            el = mclk.iv_ms;
        off = el * MCLK_PULSE_U / mclk.iv_ms;
        if (off >= MCLK_PULSE_U)
            off = MCLK_PULSE_U - 1u;
    }
    tgt = mclk.pos + off;
    adv = (int32_t)(tgt - mclk.done) > 0 ? tgt - mclk.done : 0u;
    if (adv > cap)
        adv = cap;
    mclk.done += adv;
    return adv;
}

static uint32_t mco_u;
static uint8_t mco_play;
static void clkout_block(uint32_t n)
{
    if (!song.g[G_CLKOUT] || song.g[G_SYNC]) {
        if (mco_play)
            midi_out_event(0x0Fu | 0xFCu << 8);
        mco_play = 0;
        mco_u = 0;
        return;
    }
    if (song.playing && !mco_play) {
        midi_out_event(0x0Fu | 0xFAu << 8);
        midi_out_event(0x0Fu | 0xF8u << 8);
        mco_u = 0;
    } else if (!song.playing && mco_play) {
        midi_out_event(0x0Fu | 0xFCu << 8);
    }
    mco_play = (uint8_t)(song.playing != 0);
    mco_u += n * (uint32_t)song.g[G_BPM];
    while (mco_u >= MCLK_PULSE_U) {
        mco_u -= MCLK_PULSE_U;
        midi_out_event(0x0Fu | 0xF8u << 8);
    }
}
static void events_block(uint32_t n)
{
    uint32_t i, pr, adv;
    audilune_gen_block();
    if (mo_any && !song.g[G_MIDI]) {
        seq_out_all_off();
        mo_any = 0;
    }
    if (transport_req == 1u || transport_req == 3u) {
        uint32_t ext = transport_req == 3u;
        transport_req = 0;
        if (ext && ci_on)
            ci_on = 0;
        if (ft_on) {
            ft_close();
        } else if (ci_on) {
            ci_on = 0;
        } else if (!ext && rec_wait && rec_count && !song.playing && !mclk_on() &&
                   !(project_empty() && !rec_tempo)) {
            ci_on = 1;
            ci_u = 0;
            ci_beat = 0;
            click_on(1);
        } else {
            seq_start();
            if (rec_wait && song.playing)
                rec_begin();
        }
    } else if (transport_req == 2u) {
        seq_stop();
        transport_req = 0;
        song.rec = 0;
        rec_wait = 0;
        if (ft_on) {
            ft_on = 0;
            ft_bars = 0xFF;
        }
    }
    if (ci_on) {
        if (!rec_wait || song.playing) {
            ci_on = 0;
        } else {
            uint32_t b;
            ci_u += n * (uint32_t)song.g[G_BPM];
            b = ci_u / BEAT_U;
            if (b >= 4u) {
                ci_on = 0;
                seq_start();
                if (song.playing)
                    rec_begin();
            } else if (b != ci_beat) {
                ci_beat = (uint8_t)b;
                click_on(0);
            }
        }
    }
    if (rec_wait && song.playing)
        rec_begin();
    adv = mclk_on() && song.playing ? mclk_adv(n) : n * (uint32_t)song.g[G_BPM];
    clkout_block(n);
    ft_block();
#if FELUCCA_ARRANGER
    if (song.playing && arrangement_clock.running) {
        int scene = arr_next(&arrangement_clock, &arrangement, FS);
        if (scene == ARR_DONE) {
            arrangement_clock.running = 1;
            seq_stop();
        }
        else if (scene >= 0) {
            arrangement_apply((uint32_t)scene);
            seq_reset_tracks(arrangement_clock.phase);
        }
    } else if (song.playing) {
        live_block();
    }
#endif
    if (song.playing && !(clk_beat & 3u) && (clk_beat >> 2) != fill_last_bar) {
        fill_last_bar = clk_beat >> 2;
        fill_bar_on = fill_arm;
        fill_arm = 0;
    }
    fill_now = (uint8_t)(fill_held || fill_bar_on);
    {
        static uint8_t route_was;
        static uint32_t chans_was = 0xFFFFFFFFu;
        uint32_t rt = (uint32_t)song.g[G_ROUTE];
        uint32_t chans = (uint32_t)(song.g[G_CH1] & 31) | (uint32_t)(song.g[G_CH2] & 31) << 5 |
                         (uint32_t)(song.g[G_CH3] & 31) << 10 | (uint32_t)(song.g[G_CH4] & 31) << 15 |
                         (uint32_t)(song.g[G_CH5] & 31) << 20 | (uint32_t)(song.g[G_CHOTH] & 1) << 25 |
                         (uint32_t)(song.g[G_DRCH] & 31) << 26;
        if ((rt != route_was && (rt >= 2u || route_was >= 2u)) || (chans != chans_was && chans_was != 0xFFFFFFFFu)) {
            panic_req = (uint8_t)((1u << NTRK) - 1u);
            seq_out_all_off();
            memset(midi_sel_on, 0, sizeof midi_sel_on);
            memset(ch_sus, 0, sizeof ch_sus);
            memset(ch_bend, 0, sizeof ch_bend);
            for (i = 0; i < NTRK; i++)
                trk[i].bend = 0;
        }
        route_was = (uint8_t)rt;
        chans_was = chans;
    }
    pr = panic_req;
    panic_req = 0;
    {
        uint32_t ak = arp_keep_req;
        arp_keep_req = 0;
        if (pr == (1u << NTRK) - 1u)
            ak = 0;
        arp_keep_req = (uint8_t)ak;
    }
    if (pr)
        mi_pen[0] = mi_pen[1] = mi_pen[2] = mi_pen[3] = 0;
    if (pr == (1u << NTRK) - 1u) {
        memset(ch_bend, 0, sizeof ch_bend);
        memset(ch_sus, 0, sizeof ch_sus);
        for (i = 0; i < NTRK; i++)
            trk[i].bend = 0;
    }
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        if ((pr >> i) & 1u) {
            kh_release(t);
            if (t->pold) {
                strum_cancel(t, 0, 1);
                t->nmono = 0, t->mono_note = 0, t->xp_n = 0;
            } else {
                trk_all_off(t);
            }
            live_held[i][0] = live_held[i][1] = live_held[i][2] = live_held[i][3] = 0;
            mi_trk_held[i][0] = mi_trk_held[i][1] = mi_trk_held[i][2] = mi_trk_held[i][3] = 0;
            sus_wait[i][0] = sus_wait[i][1] = sus_wait[i][2] = sus_wait[i][3] = 0;
            sus_on[i] = 0;
            if (!((arp_keep_req >> i) & 1u)) {
                t->nheld = 0;
                t->arp_phys = 0;
                t->arp_note = t->arp_ch[0] = t->arp_ch[1] = t->arp_ch[2] = 0;
            }
        }
        if (i < NPART)
            engine_block(t);
        if ((t->armp && !t->p[P_AMODE]) || (t->aholdp && !t->p[P_AHOLD] && !t->arp_phys)) {
            t->nheld = 0;
            if (!t->p[P_AMODE])
                t->arp_phys = 0;
            arp_end(t);
        }
        if (kh_any(t) && !kh_on(t))
            kh_release(t);
        t->armp = t->p[P_AMODE];
        t->aholdp = t->p[P_AHOLD];
    }
    if (pr)
        arp_keep_req = 0;
    keyboard_block();
    drum_audition_poll();
    strum_block(n);
    while (mi_r != mi_w) {
        uint32_t pkt = midi_in_q[mi_r % MQ], st = (pkt >> 8) & 0xF0u, ch = (pkt >> 8) & 0x0Fu;
        uint32_t d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        track_t *t;
        mi_r++;
        if ((pkt & 15u) == 0xFu) {
            mclk_event((pkt >> 8) & 0xFFu, ((pkt >> 4) & 15u) ? 2u : 1u);
            continue;
        }
        if (song.g[G_RMCH] && ch == (uint32_t)song.g[G_RMCH] - 1u && st == 0xB0u && d1 >= REMOTE_CC0 &&
            d1 < REMOTE_CC0 + 14u) {
            remote_cc(d1 - REMOTE_CC0, d2);
            continue;
        }
        if (song.g[G_FXCH] && ch == (uint32_t)song.g[G_FXCH] - 1u && song.g[G_ROUTE] != 1 &&
            (st == 0x90u || st == 0x80u)) {
            midi_fx(d1, st == 0x90u && d2);
            continue;
        }
        if (song.g[G_ROUTE] >= 2) {
            if (ch != (uint32_t)song.g[G_ROUTE] - 2u)
                continue;
            if (st == 0xB0u) {
                if (d1 == 0u) {
                    ch_bank[ch & 15u] = (uint8_t)d2;
                    continue;
                }
                if (d1 == 64u)
                    midi_pedal(ch, TSEL, d2);
                midi_cc(TSEL, d1, d2);
                continue;
            }
            if (st == 0xE0u) {
                if (!is_drum(TSEL))
                    midi_bend(ch, TSEL, d1, d2);
                continue;
            }
            if (st == 0xC0u) {
                pc_req[song.sel % NTRK] = (uint8_t)(d1 + 1u);
                pc_bank[song.sel % NTRK] = ch_bank[ch & 15u];
                continue;
            }
            if (st != 0x90u && st != 0x80u)
                continue;
            if (st == 0x90u && d2) {
                midi_sel_set(ch, d1, song.sel + 1u);
                t = TSEL;
            } else {
                t = midi_sel_get(ch, d1) ? &trk[(midi_sel_get(ch, d1) - 1u) % NTRK] : TSEL;
                midi_sel_set(ch, d1, 0);
            }
        } else {
            if (st == 0xB0u) {
                if (d1 == 0u) {
                    ch_bank[ch & 15u] = (uint8_t)d2;
                    continue;
                }
                if (!song.g[G_ROUTE] && (t = midi_track(ch)) != 0) {
                    if (d1 == 64u)
                        midi_pedal(ch, t, d2);
                    midi_cc(t, d1, d2);
                }
                else if (!song.g[G_ROUTE])
                    (void)midi_learned(d1, d2);
                continue;
            }
            if (st == 0xC0u) {
                if (!song.g[G_ROUTE] && (t = midi_track(ch)) != 0) {
                    pc_req[trk_index(t) % NTRK] = (uint8_t)(d1 + 1u);
                    pc_bank[trk_index(t) % NTRK] = ch_bank[ch & 15u];
                }
                continue;
            }
            if (st == 0xE0u) {
                if (!song.g[G_ROUTE] && (t = midi_track(ch)) != 0 && !is_drum(t))
                    midi_bend(ch, t, d1, d2);
                continue;
            }
            if (st != 0x90u && st != 0x80u)
                continue;
            if (song.g[G_ROUTE] && st == 0x90u && d2)
                continue;
            t = midi_route(ch, d1, st == 0x90u && d2);
            if (!t)
                continue;
        }
        if (is_drum(t)) {
            if (st == 0x90u && d2)
                drum_input(midi_drum_lane(d1), vel_lvl(d2), 0, 1);
        } else if (st == 0x90u && d2) {
            if (t == TSEL) {
                if (!(mi_pen[0] | mi_pen[1] | mi_pen[2] | mi_pen[3]) || pen_n >= 4u)
                    pen_n = 0;
                pen_note[pen_n++] = (uint8_t)d1;
                mi_pen[d1 >> 5] |= 1u << (d1 & 31u);
            }
            step_in_push(d1 | 0x80u | trk_index(t) << 8);
            mi_trk_held[trk_index(t) % NTRK][d1 >> 5] |= 1u << (d1 & 31u);
            input_on(t, d1, d2);
        } else {
            mi_pen[d1 >> 5] &= ~(1u << (d1 & 31u));
            step_in_push(d1 | trk_index(t) << 8);
            mi_trk_held[trk_index(t) % NTRK][d1 >> 5] &= ~(1u << (d1 & 31u));
            input_off(t, d1);
        }
    }
    if (midi_in_lost) {
        uint32_t ti, w, b;
        midi_in_lost = 0;
        for (ti = 0; ti < NTRK; ti++)
            for (w = 0; w < 4u; w++)
                for (b = 0; b < 32u; b++)
                    if ((mi_trk_held[ti][w] >> b) & 1u) {
                        mi_trk_held[ti][w] &= ~(1u << b);
                        input_off(&trk[ti], w * 32u + b);
                    }
        mi_pen[0] = mi_pen[1] = mi_pen[2] = mi_pen[3] = 0;
    }
    for (i = 0; i < NTRK; i++)
        seq_tick(&trk[i], adv);
    click_tick();
    roll_block(adv);
    for (i = 0; i < NPART; i++)
        arp_tick(&trk[i], adv);
    if (song.playing) {
        song.tick++;
        clk_pos += adv;
        while (clk_pos >= BEAT_U) {
            clk_pos -= BEAT_U;
            clk_beat++;
        }
#if FELUCCA_ARRANGER
        arr_elapse(&arrangement_clock, adv, 1u);
#endif
    }
}
