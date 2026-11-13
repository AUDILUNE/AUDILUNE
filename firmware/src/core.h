/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Adapted for AUDILUNE Firmware (Block 4: 6 Tracks & 128-Step Sequencer) */
#include <stdint.h>
#define NVOICE 8                 /* voices per part, and the budget shared by all parts */
#define NPOLY 8
#define NPART 5                  /* AUDILUNE 6-track architecture: 5 synth parts (tracks 1..5) */
#define NTRK 6                   /* + 1 drum track (track 6) = 6 polymorphic tracks */
#define TRK_DRUM 5
enum { V_POLY, V_MONO, V_LEGATO, V_UNISON };   /* P_VOICE */
#define NSTEP 128                /* AUDILUNE 128-step deep sequencer array per pattern block */
#define HALF_FRAMES 256          /* I2S half buffer: 5.8 ms at 44.1 kHz */
#ifndef FELUCCA_SLICE
#define FELUCCA_SLICE 0
#endif
#define NENGINES (12 + FELUCCA_SLICE)
#define ENGI_GRAIN 8u
#define ENGI_PHYS 10u
#define ENGI_NOISE 11u
#define ENGI_SLICE 12u
#define ENGI_FM6 9u
#define UP_SLOTS 32u
#define NELEM(a) (sizeof(a) / sizeof((a)[0u]))

/* ------------------------------------------------------- parameters --- */
enum {
    F_INT, F_PCT, F_BIPCT, F_TIME, F_LFOHZ, F_CUTOFF, F_DB, F_SEMI, F_ENUM, F_BPM, F_NOTE,
    F_ONOFF, F_OCT, F_STEPS,
    F_SWING,
    F_FILT
};

typedef struct {
    const char *label;
    uint8_t fmt;
    int16_t min, max, def;
    const char *const *names;
    const char *unit;
} param_desc_t;

enum {                          /* per-track parameters */
    P_LEVEL,
    P_ATK, P_DEC, P_SUS, P_REL,
    P_ED_FLT, P_ED_PIT, P_ED_SHP, P_ED_FX,
    P_LRATE, P_LWAVE, P_LPHASE, P_LFADE,
    P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP,
    P_AMODE, P_ARATE, P_AOCT, P_AGATE,
    P_ASWING, P_APROB, P_AHOLD, P_AORDER,
    P_ROOT, P_SCALE, P_QUANT, P_TRANS,
    P_SLEN, P_SDIV, P_SSWING, P_SGATE,
    P_DIST, P_CHOR, P_DLY, P_REV,
    P_VOICE, P_GLIDE, P_PAN, P_MUTE,
    P_GLMODE, P_PRIO, P_ALLOC, P_DETUNE,
    P_SLCR, P_SLPAT, P_SLRATE, P_SLDEPTH,
    P_CHORD,
    P_TFLT,
    P_STRUM,
    P_VLEAD,
    P_ITYPE, P_IA, P_IB, P_IC, P_IMIX,
    P_FTYPE,
    P_ESYNC,
    P_LSYNC, P_LTRIG, P_LPOL,
    P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7,
    P_COUNT
};
#define P_FILT P_TFLT            /* AUDILUNE alias: maps track DJ filter directly to P_TFLT */

enum {                          /* global parameters */
    G_BPM, G_SWING, G_CLOCK, G_TUNE,
    G_DTIME, G_DFDBK, G_DCOLOR, G_DMIX,
    G_RSIZE, G_RDAMP, G_CRATE, G_CDEPTH,
    G_MIDI, G_SYNC, G_ROUTE, G_INFO,
    G_SLOT, G_NAME, G_LOAD, G_SAVE,
    G_ENGSEL, G_ENGGO,
    G_CLRSEQ, G_INITSND,
    G_DRCH, G_DRLVL, G_DRREV,
    G_DUST, G_DUCK, G_FILT,
    G_ROLL,
    G_NEWPRJ,
    G_DRDLY,
    G_CH1, G_CH2, G_CH3, G_CH4, G_CH5,   /* AUDILUNE 1-to-1 MIDI channels for synth tracks 1..5 (track 6 = G_DRCH) */
    G_CHOTH,
    G_TXSRC, G_TXLVL, G_TXTONE, G_TXMOVE,
    G_TXRATE, G_TXWHEN, G_TXDUCK,
    G_BEND,
    G_FXCH,
    G_DRMAP,
    G_RMCH,
    G_CLKOUT,
    G_FXFADE,
    G_COUNT
};
#define G_CLICK G_CLOCK          /* AUDILUNE alias: maps metronome/click level directly to G_CLOCK */
/* ----------------------------------------------------------- voices --- */
typedef struct {
    uint8_t note, vel, gate, active;
    uint8_t stage;
    uint8_t kill;
    uint8_t pitch_frac;
    uint8_t pend;
    int16_t penv;
    uint8_t pend_note, pend_vel;
    int32_t env;
    int32_t env_out;
    int32_t pitch16, pitch_cur;
    int32_t gstep;
    int32_t fine;
    uint32_t ph[3u];
    int32_t s[8u];
    uint32_t age;
} voice_t;

typedef struct {
    uint32_t inc;
    int32_t pitch16;
    int32_t amp0, amp1;
    int32_t cutoff;
    int32_t shape;
    int32_t envq15;
    int32_t fine;
} vmod_t;

typedef struct {
    const char *name;
    int8_t e[8u];
    uint8_t env[4u];
    int8_t fenv;
    uint8_t mono;
    uint8_t fx[4u];
    uint8_t arp[4u];
    uint8_t pat;
    int8_t x[8u];
} preset_t;
#define FX(d, c, dl, r) .fx = {(d) + 1, (c) + 1, (dl) + 1, (r) + 1}
#define ARP(m, rt, o, g) .arp = {(m) + 1, (rt) + 1, (o) + 1, (g) + 1}
#define PAT(n) .pat = (n)
#define XP(...) .x = {__VA_ARGS__}

struct track;
typedef struct {
    const char *name;
    const char *page_title[2u];
    param_desc_t edit[8u];
    const preset_t *presets;
    uint8_t npresets;
    int8_t fil_page;
    void (*note_on)(struct track *t, voice_t *v);
    void (*render)(struct track *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m);
    uint16_t color;
    uint8_t macro[4u];
    uint8_t poly;
    int32_t (*amp)(struct track *t, voice_t *v, int32_t adsr);
    const param_desc_t *(*desc)(const struct track *t, uint32_t k);
    void (*block)(struct track *t);
    uint8_t ownenv;
    int (*done)(struct track *t, voice_t *v);
} engine_t;
/* ------------------------------------------------------------ track --- */
enum { ST_NOTE, ST_TIE, ST_REST };
#define SF_ACCENT 1u
#define SF_SLIDE 2u
#define SF_CONT0 8u
#define SF_CONT(k) (SF_CONT0 << (k))
#define SF_CONT_ALL (15u * SF_CONT0)
#define SF_LEG 0x80u
enum { LV_NORM, LV_GHOST, LV_SOFT, LV_HARD };
typedef struct {                 /* a step of a synth part (10 bytes): up to 4 notes (POLY), time, accent, slide */
    uint8_t note[4u];
    uint8_t n;                   /* notes in use, 0 = empty */
    uint8_t time;                /* ST_NOTE / ST_TIE / ST_REST */
    uint8_t flags;               /* SF_ACCENT | SF_SLIDE */
    uint8_t vel;
    uint8_t lvl;                 /* 2 bits per note (note k: bits 2k..2k+1): LV_* */
    uint8_t rat;                 /* 2 bits per note: the hits it plays in its step - 1 (ratchet x1..x4) */
} step_t;
#define DRUM_LANES 16            /* the drum track: 16 sounds, one per white key (drums.c LANE_*) */
typedef struct {                 /* a step of the drum track (10 bytes, the size of a step_t) */
    uint8_t on[2u];              /* bit per lane */
    uint8_t lvl[4u];             /* 2 bits per lane (lane k: byte k / 4, bits 2 (k % 4)..): LV_* */
    uint8_t rat[4u];             /* 2 bits per lane: ratchet */
} dstep_t;
_Static_assert(sizeof(step_t) == 10 && sizeof(dstep_t) == 10, "a step is 10 bytes on every track");
#define MICRO_MIN (-32)          /* a step's nudge in 1/64 of its length: half a step early .. */
#define MICRO_MAX 31             /* .. just under half a step late (0 = on the grid) */
#define NLOCK 24                 /* parameter locks per track (several may share a step: other params) */
typedef struct {                 /* a lock: on step `step` the track's p[param] is `val` (4 bytes) */
    uint8_t step;                /* 0xFF = a free slot */
    uint8_t param;               /* P_* (a lockable one: p_lockable) */
    int16_t val;
} plock_t;
#define LOCK_FREE 0xFFu
enum { FC_NORM, FC_FILL, FC_NOFILL };

typedef struct track {
    int16_t p[P_COUNT];
    uint8_t engine, preset;
    uint8_t eng_req;
    uint8_t user;
    voice_t v[NVOICE];
    /* LFO */
    uint32_t lfo_ph;
    int32_t lfo_val;
    int32_t lfo_fade;
    uint32_t lfo_rnd;
    /* keyboard / arp input: held notes in press order */
    uint8_t held[16u];
    uint8_t nheld;
    uint8_t arp_phys;
    uint8_t latched;
    /* arp runtime */
    uint32_t arp_pos;
    uint32_t arp_abs;
    uint32_t arp_idx;
    uint8_t arp_note;
    uint8_t arp_new;
    uint8_t arp_ch[3u];
    uint8_t arp_walk;
    uint8_t arp_odd;
    uint32_t arp_off;
    /* sequencer: synth parts step[], the drum track dstep[] (16 lanes) */
    union {
        step_t step[NSTEP];
        dstep_t dstep[NSTEP];
    };
    int8_t micro[NSTEP];
    plock_t lock[NLOCK];
    uint8_t fill[NSTEP / 4u];
    /* the locks in force */
    uint8_t lk_n;
    uint8_t lk_param[NLOCK];
    int16_t lk_base[NLOCK];
    int16_t lk_set[NLOCK];
    uint32_t seq_abs;
    int16_t bend, bend_s;
    uint16_t seq_idx;
    uint8_t seq_den;
    uint8_t arp_den;
    uint8_t seq_notes[4u];
    uint8_t seq_n;
    uint8_t seq_hold;
    uint8_t seq_skip;
    uint8_t slide_glide;
    uint32_t seq_off;
    uint8_t seq_active;
    uint8_t rat_done[4u];
    uint32_t rat_lanes;
    uint32_t rskip_abs;
    uint8_t rskip_n, rskip[4u];
    uint16_t rskip_lanes;
    /* live recording of held notes */
    uint8_t rh_n, rh_note[4u];
    uint8_t rh_start;
    uint8_t rh_ties;
    uint8_t rh_last;
    uint32_t rh_last_abs;
    step_t rh_bak;
    step_t rh_put;
    uint8_t rh_end;
    uint8_t rh_rs;
    uint8_t rh_offn, rh_off[4u];
    uint32_t pass;
    /* mono */
    uint8_t mono_stack[8u];
    uint8_t nmono;
    uint8_t mono_note;
    uint8_t rr;
    /* mix runtime */
    int32_t lvl;
    int32_t gpl, gpr;
    uint8_t mix_on;
    int32_t peak;
    int32_t dist_hp, dist_lp1, dist_lp2;
    int32_t att;
    uint8_t dist_on;
    uint8_t tail;
    uint8_t ins_run;
    int16_t armp, aholdp;
    /* engine switch */
    uint8_t xf_on, xf;
    int16_t pe_old[8u];
    int16_t p_old[P_COUNT];
    uint8_t pold;
    uint16_t ins_res;
    uint8_t ins_xt;
    uint8_t xp_n, xp_note[4u], xp_vel[4u];
} track_t;

typedef struct {
    int16_t g[G_COUNT];
    uint8_t playing, seq_mode;
    uint8_t rec;
    uint8_t sel;
    int8_t octave;
    volatile uint8_t solo;
    uint32_t tick;
    uint32_t cpu_q8;
    uint32_t master_q12;
    int32_t batt_raw;
} song_t;

static track_t trk[NTRK];
static song_t song;

#ifndef FS
#define FS 44100
#endif
#define BEAT_U ((uint32_t)FS * 60u)
static volatile uint32_t clk_beat, clk_pos;
static const uint8_t DIV_DEN[6u] = {1, 2, 4, 8, 3, 6};
#define NDIV_SHORT 6u
#define NDIV_STEP 9u
#define NDIV_DLY 8u
static const uint8_t DIV_BEATS[3u] = {2, 4, 8};

static uint32_t div_units(uint32_t div)
{
    return div < NDIV_SHORT ? BEAT_U / DIV_DEN[div] : BEAT_U * DIV_BEATS[(div - NDIV_SHORT) % 3u];
}

static uint32_t dly_units(uint32_t d)
{
    return d < NDIV_SHORT ? BEAT_U / DIV_DEN[d] : d == NDIV_SHORT ? BEAT_U * 3u / 4u : d == NDIV_SHORT + 1u ? BEAT_U * 3u / 8u
                                                                                         : BEAT_U * 3u / 2u;
}

static uint32_t div_samples(uint32_t div) { return div_units(div) / (uint32_t)song.g[G_BPM]; }
static uint32_t dly_samples(uint32_t d) { return dly_units(d) / (uint32_t)song.g[G_BPM]; }

static volatile uint8_t thr_req;
static volatile uint8_t ex_off[NTRK];
static inline int32_t expr_gain(int32_t lvl, uint32_t k)
{
    uint32_t e = 127u - ex_off[k % NTRK];
    return e >= 127u ? lvl : (int32_t)(((uint32_t)lvl * ((e * e * 16384u + 8064u) / 16129u)) >> 14);
}
static volatile uint8_t thr_ed;
static volatile uint8_t ed_master;
static volatile uint16_t ed_master_k = 0xFFFFu;
static int32_t thr_rev, thr_dly;
static inline int32_t send_thr(int32_t s, int32_t g) { return g ? s + (((127 * 258 - s) * g) >> 15) : s; }
#define TSEL (&trk[song.sel])
#define TDRUM (&trk[TRK_DRUM])
static int is_drum(const track_t *t) { return t == TDRUM; }
static int trk_silent(const track_t *t)
{
    uint32_t i = (uint32_t)(t - trk);
    return t->p[P_MUTE] || (song.solo && !((song.solo >> i) & 1u));
}
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
static volatile uint32_t fm1_ms;
#include "bootguard.h"
bootguard_t bootguard __attribute__((section(".noinit")));
