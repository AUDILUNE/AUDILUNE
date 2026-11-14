/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Adapted for AUDILUNE Firmware (Block 4: 6 Tracks x 128 Steps Compact Flash Format) */

#ifndef UI_SMALL
#define UI_SMALL
#endif
#define PROJ_MAGIC 0x41554431u                 /* "AUD1": AUDILUNE 6-track x 128-step compact format */
#define PROJ_MAGIC_V6 0x46554E36u              /* "FUN6": SLOOP 2.6 (4 tracks x 64 steps) */
#define PROJ_MAGIC_V5 0x46554E35u              /* "FUN5": SLOOP 2.4..2.5 */
#define PROJ_MAGIC_V4 0x46554E34u              /* "FUN4": SLOOP 2.0..2.3 */
#define PROJ_MAGIC_V3 0x46554E33u              /* "FUN3": SLOOP 1.x */
#define PROJ_MAGIC_V2 0x46554E32u              /* "FUN2": SLOOP 0.9 */
#define PROJ_MAGIC_V1 0x46554E31u              /* "FUN1": SLOOP 0.5 */

#define LEGACY_NTRK 4u
#define LEGACY_NSTEP 64u
#define PROJ_NP_V5 61u
#define PROJ_NP_V4 58u
#define PROJ_NP_V3 57u
#define PROJ_NG_V3 27u
#define PROJ_NP_V2 53u
#define PROJ_NG_V2 27u
#define PROJ_NG 32u

_Static_assert(G_DRDLY == PROJ_NG && G_TXSRC == PROJ_NG + 7u,
               "AUDILUNE globals: G_CH1..G_CH5 + G_CHOTH precede G_TXSRC");
_Static_assert(G_FXFADE == G_CLKOUT + 1u && G_COUNT == G_FXFADE + 1u,
               "G_FXFADE is the last global");

#define PROJ_NC6 ((uint32_t)P_ITYPE)
#define PROJ_NX6 ((uint32_t)(P_E0 - P_ITYPE))
_Static_assert(PROJ_NC6 == PROJ_NP_V5 - 8u && PROJ_NX6 == 10u, "track parameter layout");

/* Compact 2-byte step for flash persistence (fits 6 tracks x 128 steps inside 3840-byte sector) */
typedef struct {
    uint8_t a;  /* Synth: bit 7 = active/tie, bits 0..6 = note | Drum: lanes 0..7 */
    uint8_t b;  /* Synth: bits 0..1 = type (ST_*), bits 2..3 = lvl, bits 4..5 = rat, bit 6 = slide, bit 7 = accent | Drum: lanes 8..15 */
} cstep_t;

typedef struct {
    int16_t p[PROJ_NC6];
    int16_t e[8u];
    uint8_t x[PROJ_NX6];
    uint8_t engine, preset;
    cstep_t cstep[NSTEP];                      /* 128 steps x 2 bytes = 256 bytes per track */
    uint8_t mf[NSTEP];                         /* 128 steps x 1 byte (micro-timing + fill cond) */
    plock_t lock[NLOCK];                       /* 24 parameter/FX locks x 4 bytes = 96 bytes */
} proj_trk_t;

typedef struct {
    uint32_t magic, size;
    int16_t g[PROJ_NG];
    uint8_t sel, drdly, rsv[2u];
    proj_trk_t t[NTRK];                        /* 6 tracks x 588 bytes = 3528 bytes */
    uint32_t sum;
} project_t;
static int16_t pt_get(const proj_trk_t *t, uint32_t id)
{
    return id < PROJ_NC6 ? t->p[id] : id < P_E0 ? (int16_t)t->x[id - PROJ_NC6] : t->e[(id - P_E0) & 7u];
}

UI_SMALL static void pt_set(proj_trk_t *t, uint32_t id, int32_t v)
{
    if (id < PROJ_NC6)
        t->p[id] = (int16_t)v;
    else if (id < P_E0)
        t->x[id - PROJ_NC6] = (uint8_t)clamp(v, 0, 255);
    else
        t->e[(id - P_E0) & 7u] = (int16_t)v;
}

static int32_t pt_micro(const proj_trk_t *t, uint32_t i) { int32_t m = t->mf[i] & 63; return m >= 32 ? m - 64 : m; }
static uint32_t pt_fill(const proj_trk_t *t, uint32_t i) { return (uint32_t)t->mf[i] >> 6; }

UI_SMALL static void pt_set_mf(proj_trk_t *t, uint32_t i, int32_t micro, uint32_t fill)
{
    t->mf[i] = (uint8_t)(((uint32_t)micro & 63u) | (fill & 3u) << 6);
}

/* Pack/unpack step_t and dstep_t <-> compact 2-byte cstep_t */
UI_SMALL static void cstep_pack_synth(cstep_t *d, const step_t *s)
{
    uint8_t flags = (uint8_t)(((s->flags & SF_SLIDE) ? 0x40u : 0u) | ((s->flags & SF_ACCENT) ? 0x80u : 0u));
    d->a = (uint8_t)((s->note[0u] & 127u) | ((s->n > 0u) ? 0x80u : 0u));
    d->b = (uint8_t)((s->time & 3u) | ((s->lvl & 3u) << 2) | ((s->rat & 3u) << 4) | flags);
}

UI_SMALL static void cstep_unpack_synth(step_t *d, const cstep_t *s)
{
    memset(d, 0, sizeof *d);
    d->time = (uint8_t)(s->b & 3u);
    if (d->time > ST_REST)
        d->time = ST_REST;
    if ((s->a & 0x80u) && d->time == ST_NOTE) {
        d->note[0u] = (uint8_t)(s->a & 127u);
        d->n = 1u;
        d->lvl = (uint8_t)((s->b >> 2) & 3u);
        d->rat = (uint8_t)((s->b >> 4) & 3u);
        d->vel = (s->b & 0x80u) ? 127u : 100u;
        if (s->b & 0x40u)
            d->flags |= SF_SLIDE;
        if (s->b & 0x80u)
            d->flags |= SF_ACCENT;
    }
}

UI_SMALL static void cstep_pack_drum(cstep_t *d, const dstep_t *s)
{
    d->a = s->on[0u];
    d->b = s->on[1u];
}

UI_SMALL static void cstep_unpack_drum(dstep_t *d, const cstep_t *s)
{
    memset(d, 0, sizeof *d);
    d->on[0u] = s->a;
    d->on[1u] = s->b;
}
/* Legacy SLOOP 2.6 (FUN6) and older read-only structures (fixed 4 tracks x 64 steps) */
typedef struct {
    int16_t p[PROJ_NC6];
    int16_t e[8u];
    uint8_t x[PROJ_NX6];
    uint8_t engine, preset;
    union {
        step_t step[LEGACY_NSTEP];
        dstep_t dstep[LEGACY_NSTEP];
    };
    uint8_t mf[LEGACY_NSTEP];
    plock_t lock[NLOCK];
} proj_trk_v6_t;

typedef struct {
    uint32_t magic, size;
    int16_t g[PROJ_NG];
    uint8_t sel, drdly, rsv[2u];
    proj_trk_v6_t t[LEGACY_NTRK];
    uint32_t sum;
} project_v6_t;

typedef struct {
    int16_t p[PROJ_NP_V5];
    uint8_t engine, preset;
    union {
        step_t step[LEGACY_NSTEP];
        dstep_t dstep[LEGACY_NSTEP];
    };
    int8_t micro[LEGACY_NSTEP];
    plock_t lock[NLOCK];
    uint8_t fill[LEGACY_NSTEP / 4u];
} proj_trk_v5_t;

typedef struct {
    uint32_t magic, size;
    int16_t g[PROJ_NG];
    uint8_t sel, drdly, rsv[2u];
    proj_trk_v5_t t[LEGACY_NTRK];
    uint32_t sum;
} project_v5_t;

typedef struct {
    int16_t p[PROJ_NP_V4];
    uint8_t engine, preset;
    union {
        step_t step[LEGACY_NSTEP];
        dstep_t dstep[LEGACY_NSTEP];
    };
} proj_trk_v4_t;

typedef struct {
    uint32_t magic, size;
    int16_t g[PROJ_NG];
    uint8_t sel, rsv[3u];
    proj_trk_v4_t t[LEGACY_NTRK];
    uint32_t sum;
} project_v4_t;
typedef struct { uint8_t note[4u], n, time, flags, vel; } step8_t;

typedef struct {
    int16_t p[PROJ_NP_V3];
    uint8_t engine, preset;
    step8_t step[LEGACY_NSTEP];
} proj_trk_v3_t;

typedef struct {
    uint32_t magic, size;
    int16_t g[PROJ_NG_V3];
    uint8_t sel, rsv[3u];
    proj_trk_v3_t t[LEGACY_NTRK];
    uint32_t sum;
} project_v3_t;

typedef struct {
    int16_t p[PROJ_NP_V2];
    uint8_t engine, preset;
    step8_t step[LEGACY_NSTEP];
} proj_trk_v2_t;

typedef struct {
    uint32_t magic, size;
    int16_t g[PROJ_NG_V2];
    uint8_t sel, rsv[3u];
    proj_trk_v2_t t[LEGACY_NTRK];
    uint32_t sum;
} project_v2_t;

typedef struct {
    uint32_t magic, size;
    int16_t g[PROJ_NG_V2];
    proj_trk_v2_t t;
    uint32_t sum;
} project_v1_t;

_Static_assert(sizeof(project_v2_t) == 2552u && sizeof(project_v1_t) == 688u && sizeof(project_v3_t) == 2584u &&
               sizeof(project_v4_t) == 3112u && sizeof(project_v5_t) == 3840u && sizeof(project_v6_t) == 3816u,
               "legacy formats match stored flash sizes");
_Static_assert(sizeof(project_t) <= 3840u, "AUDILUNE 6-track x 128-step format fits one flash object (3840 B)");

#define AUDILUNE_VAULT_SLOTS 6u
#define AUDILUNE_NOINIT_CACHE 2u
project_t proj_slot[AUDILUNE_NOINIT_CACHE] __attribute__((section(".noinit")));
static project_t proj_ext[AUDILUNE_VAULT_SLOTS - AUDILUNE_NOINIT_CACHE];
static inline project_t *proj_ptr(uint32_t s)
{
    s %= AUDILUNE_VAULT_SLOTS;
    return s < AUDILUNE_NOINIT_CACHE ? &proj_slot[s] : &proj_ext[s - AUDILUNE_NOINIT_CACHE];
}
UI_SMALL static uint32_t proj_hash(const void *p, uint32_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t i, s = 0x811C9DC5u;
    for (i = 0; i < n; i++)
        s = (s ^ b[i]) * 16777619u;
    return s;
}

UI_SMALL static uint32_t proj_sum(const project_t *p) { return proj_hash(p, sizeof *p - 4u); }
UI_SMALL static int proj_ok(const project_t *q) { return q->magic == PROJ_MAGIC && q->size == sizeof *q && q->sum == proj_sum(q); }

UI_SMALL static void step_from8(step_t *d, const step8_t *s)
{
    memcpy(d->note, s->note, 4u);
    d->n = s->n;
    d->time = s->time;
    d->flags = s->flags;
    d->vel = s->vel;
    d->lvl = d->rat = 0;
}

UI_SMALL static void dstep_from8(dstep_t *d, const step8_t *s)
{
    uint32_t i, lvl = (s->flags & SF_ACCENT) || s->vel > 115u ? LV_HARD : !s->vel ? LV_NORM : vel_lvl(s->vel);
    memset(d, 0, sizeof *d);
    if (s->time != ST_NOTE)
        return;
    for (i = 0; i < s->n && i < 4u; i++)
        dstep_set(d, lane_of_note(s->note[i] & 127u), lvl, 0);
}

UI_SMALL static int16_t swing_from_v3(int32_t v) { return (int16_t)clamp((v * 4 + 2) / 5, 0, 100); }

UI_SMALL static void proj_g_from_old(int16_t *g, const int16_t *g2)
{
    uint32_t i;
    for (i = 0; i < PROJ_NG; i++)
        g[i] = i < PROJ_NG_V3 ? g2[i] : GP[i].def;
    g[G_SWING] = swing_from_v3(g[G_SWING]);
}

UI_SMALL static void proj_trk_plain(proj_trk_t *d)
{
    uint32_t k;
    memset(d->mf, 0, sizeof d->mf);
    memset(d->cstep, 0, sizeof d->cstep);
    for (k = PROJ_NC6; k < P_E0; k++)
        pt_set(d, k, TP[k].def);
    for (k = 0; k < NLOCK; k++) {
        d->lock[k].step = LOCK_FREE;
        d->lock[k].param = 0;
        d->lock[k].val = 0;
    }
}
UI_SMALL static void proj_trk_default_init(proj_trk_t *d, uint32_t i)
{
    uint32_t k;
    for (k = 0; k < P_COUNT; k++)
        pt_set(d, k, k >= P_E0 ? ENGINES[trk_def_engine(i)]->edit[k - P_E0].def : TP[k].def);
    d->engine = (uint8_t)trk_def_engine(i);
    d->preset = 0xFFu;
    proj_trk_plain(d);
    if (i != TRK_DRUM)
        for (k = 0; k < NSTEP; k++)
            d->cstep[k].b = ST_REST;
}

UI_SMALL static void proj_trk_from_v6(proj_trk_t *d, const proj_trk_v6_t *s, int drum)
{
    uint32_t k;
    proj_trk_plain(d);
    for (k = 0; k < PROJ_NC6; k++)
        d->p[k] = s->p[k];
    for (k = 0; k < 8u; k++)
        d->e[k] = s->e[k];
    memcpy(d->x, s->x, sizeof d->x);
    d->engine = s->engine;
    d->preset = s->preset;
    for (k = 0; k < LEGACY_NSTEP; k++) {
        if (drum)
            cstep_pack_drum(&d->cstep[k], &s->dstep[k]);
        else
            cstep_pack_synth(&d->cstep[k], &s->step[k]);
        d->mf[k] = s->mf[k];
    }
    if (!drum)
        for (k = LEGACY_NSTEP; k < NSTEP; k++)
            d->cstep[k].b = ST_REST;
    memcpy(d->lock, s->lock, sizeof d->lock);
}

UI_SMALL static void proj_trk_from_v3(proj_trk_t *d, const proj_trk_v3_t *s, int drum)
{
    uint32_t k, nc = PROJ_NP_V3 - 8u;
    proj_trk_plain(d);
    for (k = 0; k < PROJ_NC6; k++)
        d->p[k] = k < nc ? s->p[k] : TP[k].def;
    for (k = 0; k < 8u; k++)
        d->e[k] = s->p[nc + k];
    d->p[P_SSWING] = swing_from_v3(d->p[P_SSWING]);
    d->p[P_ASWING] = swing_from_v3(d->p[P_ASWING]);
    d->engine = drum ? 0u : s->engine;
    d->preset = drum ? 0u : s->preset;
    for (k = 0; k < LEGACY_NSTEP; k++) {
        if (drum) {
            dstep_t ds;
            dstep_from8(&ds, &s->step[k]);
            cstep_pack_drum(&d->cstep[k], &ds);
        } else {
            step_t st;
            step_from8(&st, &s->step[k]);
            cstep_pack_synth(&d->cstep[k], &st);
        }
    }
    if (!drum)
        for (k = LEGACY_NSTEP; k < NSTEP; k++)
            d->cstep[k].b = ST_REST;
}

UI_SMALL static void proj_trk_v2_to_v3(proj_trk_v3_t *d, const proj_trk_v2_t *s, int drum)
{
    uint32_t k, nc = PROJ_NP_V2 - 8u;
    for (k = 0; k < PROJ_NP_V3 - 8u; k++)
        d->p[k] = k < nc ? s->p[k] : TP[k].def;
    for (k = 0; k < 8u; k++)
        d->p[PROJ_NP_V3 - 8u + k] = s->p[nc + k];
    d->engine = drum ? 0u : s->engine;
    d->preset = drum ? 0u : s->preset;
    memcpy(d->step, s->step, sizeof d->step);
}
UI_SMALL static int proj_from_v6(project_t *q, const project_v6_t *v6, int n)
{
    uint32_t i;
    if (n != (int)sizeof *v6 || v6->magic != PROJ_MAGIC_V6 || v6->size != sizeof *v6 ||
        v6->sum != proj_hash(v6, sizeof *v6 - 4u))
        return 0;
    memset(q, 0, sizeof *q);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    memcpy(q->g, v6->g, sizeof q->g);
    q->sel = v6->sel < NTRK ? v6->sel : 0u;
    q->drdly = v6->drdly;
    memcpy(q->rsv, v6->rsv, sizeof q->rsv);
    for (i = 0; i < LEGACY_NTRK; i++) {
        uint32_t dst = i == LEGACY_NTRK - 1u ? TRK_DRUM : i;
        proj_trk_from_v6(&q->t[dst], &v6->t[i], dst == TRK_DRUM);
    }
    for (i = LEGACY_NTRK - 1u; i < TRK_DRUM; i++)
        proj_trk_default_init(&q->t[i], i);
    q->sum = proj_sum(q);
    return 1;
}

UI_SMALL static int proj_from_v4(project_t *q, const project_v4_t *v4, int n)
{
    uint32_t i;
    if (n != (int)sizeof *v4 || v4->magic != PROJ_MAGIC_V4 || v4->size != sizeof *v4 ||
        v4->sum != proj_hash(v4, sizeof *v4 - 4u))
        return 0;
    memset(q, 0, sizeof *q);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    memcpy(q->g, v4->g, sizeof q->g);
    q->sel = v4->sel < NTRK ? v4->sel : 0u;
    for (i = 0; i < LEGACY_NTRK; i++) {
        uint32_t k, dst = i == LEGACY_NTRK - 1u ? TRK_DRUM : i;
        proj_trk_plain(&q->t[dst]);
        for (k = 0; k < PROJ_NC6; k++)
            q->t[dst].p[k] = k < PROJ_NP_V4 - 8u ? v4->t[i].p[k] : TP[k].def;
        for (k = 0; k < 8u; k++)
            q->t[dst].e[k] = v4->t[i].p[PROJ_NP_V4 - 8u + k];
        q->t[dst].engine = v4->t[i].engine;
        q->t[dst].preset = v4->t[i].preset;
        for (k = 0; k < LEGACY_NSTEP; k++) {
            if (dst == TRK_DRUM)
                cstep_pack_drum(&q->t[dst].cstep[k], &v4->t[i].dstep[k]);
            else
                cstep_pack_synth(&q->t[dst].cstep[k], &v4->t[i].step[k]);
        }
        if (dst != TRK_DRUM)
            for (k = LEGACY_NSTEP; k < NSTEP; k++)
                q->t[dst].cstep[k].b = ST_REST;
    }
    for (i = LEGACY_NTRK - 1u; i < TRK_DRUM; i++)
        proj_trk_default_init(&q->t[i], i);
    q->sum = proj_sum(q);
    return 1;
}
UI_SMALL static int proj_from_v5(project_t *q, const project_v5_t *v5, int n)
{
    uint32_t i, k;
    if (n != (int)sizeof *v5 || v5->magic != PROJ_MAGIC_V5 || v5->size != sizeof *v5 ||
        v5->sum != proj_hash(v5, sizeof *v5 - 4u))
        return 0;
    memset(q, 0, sizeof *q);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    memcpy(q->g, v5->g, sizeof q->g);
    q->sel = v5->sel < NTRK ? v5->sel : 0u;
    q->drdly = v5->drdly;
    for (i = 0; i < LEGACY_NTRK; i++) {
        uint32_t dst = i == LEGACY_NTRK - 1u ? TRK_DRUM : i;
        const proj_trk_v5_t *s = &v5->t[i];
        proj_trk_t *d = &q->t[dst];
        proj_trk_plain(d);
        for (k = 0; k < PROJ_NC6; k++)
            d->p[k] = s->p[k];
        for (k = 0; k < 8u; k++)
            d->e[k] = s->p[PROJ_NP_V5 - 8u + k];
        d->engine = s->engine;
        d->preset = s->preset;
        for (k = 0; k < LEGACY_NSTEP; k++) {
            if (dst == TRK_DRUM)
                cstep_pack_drum(&d->cstep[k], &s->dstep[k]);
            else
                cstep_pack_synth(&d->cstep[k], &s->step[k]);
            pt_set_mf(d, k, clamp(s->micro[k], MICRO_MIN, MICRO_MAX), (uint32_t)s->fill[k / 4u] >> (2u * (k % 4u)));
        }
        if (dst != TRK_DRUM)
            for (k = LEGACY_NSTEP; k < NSTEP; k++)
                d->cstep[k].b = ST_REST;
        for (k = 0; k < NLOCK; k++) {
            d->lock[k] = s->lock[k];
            if (s->lock[k].step != LOCK_FREE && s->lock[k].param >= PROJ_NP_V5 - 8u)
                d->lock[k].param = (uint8_t)(s->lock[k].param - (PROJ_NP_V5 - 8u) + P_E0);
        }
    }
    for (i = LEGACY_NTRK - 1u; i < TRK_DRUM; i++)
        proj_trk_default_init(&q->t[i], i);
    q->sum = proj_sum(q);
    return 1;
}

UI_SMALL static void proj_from_v3_ok(project_t *q, const project_v3_t *v3)
{
    uint32_t i;
    memset(q, 0, sizeof *q);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    proj_g_from_old(q->g, v3->g);
    q->sel = v3->sel < NTRK ? v3->sel : 0u;
    for (i = 0; i < LEGACY_NTRK; i++) {
        uint32_t dst = i == LEGACY_NTRK - 1u ? TRK_DRUM : i;
        proj_trk_from_v3(&q->t[dst], &v3->t[i], dst == TRK_DRUM);
    }
    for (i = LEGACY_NTRK - 1u; i < TRK_DRUM; i++)
        proj_trk_default_init(&q->t[i], i);
    q->sum = proj_sum(q);
}
UI_SMALL static int proj_from_v3(project_t *q, const project_v3_t *v3, int n)
{
    if (n != (int)sizeof *v3 || v3->magic != PROJ_MAGIC_V3 || v3->size != sizeof *v3 ||
        v3->sum != proj_hash(v3, sizeof *v3 - 4u))
        return 0;
    proj_from_v3_ok(q, v3);
    return 1;
}

static project_v3_t proj_v3_tmp;

UI_SMALL static int proj_from_v2(project_t *q, const project_v2_t *v2, int n)
{
    project_v3_t *v3 = &proj_v3_tmp;
    uint32_t i;
    if (n != (int)sizeof *v2 || v2->magic != PROJ_MAGIC_V2 || v2->size != sizeof *v2 ||
        v2->sum != proj_hash(v2, sizeof *v2 - 4u))
        return 0;
    memset(v3, 0, sizeof *v3);
    memcpy(v3->g, v2->g, sizeof v3->g);
    v3->sel = v2->sel;
    for (i = 0; i < LEGACY_NTRK; i++)
        proj_trk_v2_to_v3(&v3->t[i], &v2->t[i], i == LEGACY_NTRK - 1u);
    proj_from_v3_ok(q, v3);
    return 1;
}

UI_SMALL static int proj_from_v1(project_t *q, const project_v1_t *v1, int n)
{
    project_v3_t *v3 = &proj_v3_tmp;
    uint32_t i;
    if (n != (int)sizeof *v1 || v1->magic != PROJ_MAGIC_V1 || v1->size != sizeof *v1 ||
        v1->sum != proj_hash(v1, sizeof *v1 - 4u))
        return 0;
    memset(v3, 0, sizeof *v3);
    memcpy(v3->g, v1->g, sizeof v3->g);
    proj_trk_v2_to_v3(&v3->t[0u], &v1->t, 0);
    proj_from_v3_ok(q, v3);
    for (i = 1; i < NTRK; i++)
        proj_trk_default_init(&q->t[i], i);
    q->sum = proj_sum(q);
    return 1;
}

UI_SMALL static int proj_import(project_t *q, const void *b, int n)
{
    if (n == (int)sizeof *q && proj_ok((const project_t *)b)) {
        memcpy(q, b, sizeof *q);
        return 1;
    }
    return proj_from_v6(q, (const project_v6_t *)b, n) || proj_from_v5(q, (const project_v5_t *)b, n) ||
           proj_from_v4(q, (const project_v4_t *)b, n) || proj_from_v3(q, (const project_v3_t *)b, n) ||
           proj_from_v2(q, (const project_v2_t *)b, n) || proj_from_v1(q, (const project_v1_t *)b, n);
}
UI_SMALL static void proj_tx_put(project_t *p)
{
    p->rsv[0u] = (uint8_t)(0x80u | ((uint32_t)song.g[G_TXSRC] & 15u) | ((uint32_t)song.g[G_TXWHEN] & 1u) << 4 |
                           ((uint32_t)song.g[G_TXDUCK] & 1u) << 5 | 0x40u);
    p->rsv[1u] = (uint8_t)song.g[G_TXRATE];
    p->g[G_DRCH] = (int16_t)((song.g[G_TXLVL] & 127) | ((song.g[G_TXTONE] + 64) & 127) << 7);
    p->g[G_INFO] = song.g[G_TXMOVE];
}

static void proj_tx_get(const project_t *p)
{
    static const uint8_t ID[7u] = {G_TXSRC, G_TXLVL, G_TXTONE, G_TXMOVE, G_TXRATE, G_TXWHEN, G_TXDUCK};
    int32_t v[7u];
    uint32_t i;
    if (!(p->rsv[0u] & 0x80u)) {
        for (i = 0; i < 7u; i++)
            v[i] = GP[ID[i]].def;
    } else {
        v[0u] = p->rsv[0u] & 15u;
        v[5u] = (p->rsv[0u] >> 4) & 1u;
        v[6u] = (p->rsv[0u] >> 5) & 1u;
        v[4u] = p->rsv[1u];
        v[1u] = p->g[G_DRCH] & 127;
        v[2u] = ((p->g[G_DRCH] >> 7) & 127) - 64;
        v[3u] = p->g[G_INFO];
    }
    for (i = 0; i < 7u; i++)
        song.g[ID[i]] = (int16_t)clamp(v[i], GP[ID[i]].min, GP[ID[i]].max);
}

#define DRUM_TW_MARK 0x5400
UI_SMALL static uint32_t drum_tw_sum(const int16_t *e)
{
    uint32_t i, s = 0x5Au;
    for (i = 1; i < 8u; i++)
        s += ((uint32_t)(uint16_t)e[i] & 0xFFu) * (i * 2u + 1u) + ((uint32_t)(uint16_t)e[i] >> 8) * (i * 2u + 2u);
    return s & 0xFFu;
}

UI_SMALL static void drum_tw_put(project_t *p)
{
    uint16_t w[7u] = {0};
    uint32_t l, b;
    for (l = 0; l < DRUM_LANES; l++) {
        uint32_t v = ((uint32_t)drum_tune[l] & 15u) | ((uint32_t)drum_dec[l] & 7u) << 4;
        for (b = 0; b < 7u; b++)
            if ((v >> b) & 1u)
                w[(l * 7u + b) / 16u] |= (uint16_t)(1u << ((l * 7u + b) % 16u));
    }
    for (l = 0; l < 7u; l++)
        p->t[TRK_DRUM].e[l + 1u] = (int16_t)w[l];
    p->g[G_SAVE] = (int16_t)(DRUM_TW_MARK | drum_tw_sum(p->t[TRK_DRUM].e));
}

static void drum_tw_get(const project_t *p)
{
    const int16_t *e = p->t[TRK_DRUM].e;
    uint32_t l, b, ok = (uint16_t)p->g[G_SAVE] == (DRUM_TW_MARK | drum_tw_sum(e));
    for (l = 0; l < DRUM_LANES; l++) {
        uint32_t v = 0;
        for (b = 0; ok && b < 7u; b++)
            if (((uint16_t)e[1u + (l * 7u + b) / 16u] >> ((l * 7u + b) % 16u)) & 1u)
                v |= 1u << b;
        drum_tune[l] = (int8_t)((int32_t)((v & 15u) ^ 8u) - 8);
        drum_dec[l] = (int8_t)((int32_t)(((v >> 4) & 7u) ^ 4u) - 4);
    }
}
UI_SMALL static void proj_capture(project_t *p)
{
    uint32_t i;
    memset(p, 0, sizeof *p);
    p->magic = PROJ_MAGIC;
    p->size = sizeof *p;
    for (i = 0; i < PROJ_NG; i++)
        p->g[i] = song.g[i];
    p->sel = song.sel;
    p->drdly = (uint8_t)song.g[G_DRDLY];
    proj_tx_put(p);
    for (i = 0; i < NTRK; i++) {
        uint32_t k;
        for (k = 0; k < P_COUNT; k++)
            pt_set(&p->t[i], k, trk[i].p[k]);
        p->t[i].engine = trk[i].eng_req;
        p->t[i].preset = trk[i].preset;
        for (k = 0; k < NSTEP; k++) {
            if (i == TRK_DRUM)
                cstep_pack_drum(&p->t[i].cstep[k], &trk[i].dstep[k]);
            else
                cstep_pack_synth(&p->t[i].cstep[k], &trk[i].step[k]);
            pt_set_mf(&p->t[i], k, trk[i].micro[k], (uint32_t)trk[i].fill[k / 4u] >> (2u * (k % 4u)));
        }
        memcpy(p->t[i].lock, trk[i].lock, sizeof trk[i].lock);
    }
    drum_tw_put(p);
    p->sum = proj_sum(p);
}

static void proj_apply(const project_t *p, int all)
{
    uint32_t i, k;
    for (i = 0; i < PROJ_NG; i++)
        if (all ? i != G_SLOT && i != G_LOAD && i != G_SAVE && i != G_SYNC && i != G_MIDI && i != G_ROUTE && i != G_DRCH &&
                      i != G_INFO
                : i == G_DRLVL || i == G_DRREV)
            song.g[i] = (int16_t)clamp(p->g[i], GP[i].min, GP[i].max);
    song.g[G_DRDLY] = (int16_t)clamp(p->drdly, GP[G_DRDLY].min, GP[G_DRDLY].max);
    proj_tx_get(p);
    drum_tw_get(p);
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        const proj_trk_t *s = &p->t[k];
        uint32_t e = k < NPART ? s->engine % NENGINES : 0u;
        t->eng_req = (uint8_t)e;
        t->user = 0;
        t->lk_n = 0;
        for (i = 0; i < P_COUNT; i++) {
            const param_desc_t *d = k == TRK_DRUM && i == P_E0 ? &DRUM_KIT_DESC :
                                    i >= P_E0 && i <= P_E7 ? &ENGINES[e]->edit[i - P_E0] : &TP[i];
            t->p[i] = (int16_t)clamp(pt_get(s, i), d->min, d->max);
        }
        if (!(p->rsv[0u] & 0x40u) && !t->p[P_AMODE])
            t->p[P_AHOLD] = 0;
        t->preset = (uint8_t)(ENGINES[e]->npresets ? (s->preset == 0xFFu ? 0u : s->preset) % ENGINES[e]->npresets : 0u);
        memset(t->step, 0, sizeof t->step);
        memset(t->fill, 0, sizeof t->fill);
        for (i = 0; i < NSTEP; i++) {
            uint32_t c = pt_fill(s, i);
            if (k == TRK_DRUM)
                cstep_unpack_drum(&t->dstep[i], &s->cstep[i]);
            else
                cstep_unpack_synth(&t->step[i], &s->cstep[i]);
            t->micro[i] = (int8_t)clamp(pt_micro(s, i), MICRO_MIN, MICRO_MAX);
            if (c != 3u)
                t->fill[i / 4u] |= (uint8_t)(c << (2u * (i % 4u)));
        }
        for (i = 0; i < NLOCK; i++) {
            const plock_t *l = &s->lock[i];
            uint32_t id = l->param;
            if (l->step < NSTEP && id < P_COUNT && p_lockable(id)) {
                const param_desc_t *d = k == TRK_DRUM && id == P_E0 ? &DRUM_KIT_DESC :
                                        id >= P_E0 && id <= P_E7 ? &ENGINES[e]->edit[id - P_E0] : &TP[id];
                t->lock[i].step = l->step;
                t->lock[i].param = (uint8_t)id;
                t->lock[i].val = (int16_t)clamp(l->val, d->min, d->max);
            } else {
                t->lock[i].step = LOCK_FREE;
                t->lock[i].param = 0;
                t->lock[i].val = 0;
            }
        }
        fm6_track_loaded(t);
    }
}
#ifndef PROJ_HOST
#if FELUCCA_ARRANGER
#include "arranger_scene.c"
#endif
static uint8_t sec_dirty, song_dirty;

#if FELUCCA_FLASH
static union {
    project_t aud1;
    project_v6_t v6;
    project_v5_t v5;
    project_v4_t v4;
    project_v3_t v3;
    project_v2_t v2;
    project_v1_t v1;
} proj_tmp;

UI_SMALL static void *up_stage(void) { return &proj_tmp; }

UI_SMALL static void proj_fetch(uint32_t slot)
{
    uint32_t s = slot % AUDILUNE_VAULT_SLOTS;
    project_t *q = proj_ptr(s);
    int n = st_load(OBJ_PROJECT0 + s, &proj_tmp, sizeof proj_tmp);
    if (!proj_import(q, &proj_tmp, n))
        q->magic = 0;
}
#include "fm6_bank.c"
#endif

static const char *const SEC_NAME[AUDILUNE_VAULT_SLOTS] = {"A", "B", "C", "D", "E", "F"};

UI_SMALL static void project_save(uint32_t slot)
{
    uint32_t s = slot % AUDILUNE_VAULT_SLOTS;
    project_t *p = proj_ptr(s);
#if FELUCCA_ARRANGER
    if (song.playing || transport_req) {
        ui_message("STOP BEFORE SAVE");
        return;
    }
#endif
    proj_capture(p);
#if FELUCCA_FLASH
    if (flash_ok) {
        if (st_save(OBJ_PROJECT0 + s, p, sizeof *p))
            ui_message("SAVE ERROR");
        else
            ui_say("SAVED BLOCK ", SEC_NAME[s]);
        return;
    }
#endif
    ui_say("SAVED BLOCK ", SEC_NAME[s]);
}

UI_SMALL static void project_erase(uint32_t slot)
{
    uint32_t s = slot % AUDILUNE_VAULT_SLOTS;
    project_t *p = proj_ptr(s);
#if FELUCCA_ARRANGER
    if (song.playing || transport_req) {
        ui_message("STOP BEFORE ERASE");
        return;
    }
#endif
    if (!proj_ok(p)) {
        ui_say("EMPTY BLOCK ", SEC_NAME[s]);
        return;
    }
#if FELUCCA_FLASH
    if (flash_ok && st_save(OBJ_PROJECT0 + s, p, 0)) {
        ui_message("ERASE ERROR");
        return;
    }
#endif
    fm1_irq_off();

    memset(p, 0, sizeof *p);
    sec_dirty &= (uint8_t)~(1u << s);
    if (live_sec == (int8_t)s)
        live_sec = -1;
#if FELUCCA_ARRANGER
    if (s < 4u && arr_drop_scene(&arrangement, s))
        song_dirty = 1;
#endif
    fm1_irq_on();
    ui_say("ERASED BLOCK ", SEC_NAME[s]);
}
UI_SMALL static void proj_foreign_parts(const project_t *p)
{
    uint32_t k;
    for (k = 0; k < NPART; k++)
        if (p->t[k].preset != 0xFFu && p->t[k].engine >= NENGINES) {
            set_engine_of(&trk[k], TRK_DEF[k][0u]);
            apply_preset_to(&trk[k], TRK_DEF[k][1u]);
        }
}

UI_SMALL static void project_apply(const project_t *p)
{
    uint32_t k;
    transport_req = 2;
    panic_req = (1u << NTRK) - 1u;
    drum_mute = 0;
    for (k = 0; k < NTRK; k++)
        ex_off[k] = 0;
    fm1_irq_off();
    proj_apply(p, 1);
    song.sel = (uint8_t)(p->sel < NTRK ? p->sel : 0u);
    fm1_irq_on();
    for (k = 0; k < NPART; k++)
        if (p->t[k].preset == 0xFFu) {
            apply_preset_to(&trk[k], TRK_DEF[k][1u]);
            steps_clear(&trk[k]);
        }
    proj_foreign_parts(p);
    sync_reload = 1;
    ui.force = 1;
}

UI_SMALL static void project_load(uint32_t slot)
{
    uint32_t s = slot % AUDILUNE_VAULT_SLOTS;
    project_t *p = proj_ptr(s);
#if FELUCCA_ARRANGER
    if (song.playing || transport_req) {
        ui_message("STOP BEFORE LOAD");
        return;
    }
#endif
#if FELUCCA_FLASH
    if (flash_ok && !proj_ok(p))
        proj_fetch(s);
#endif
    if (!proj_ok(p)) {
        ui_say("EMPTY BLOCK ", SEC_NAME[s]);
        return;
    }
    project_apply(p);
    ui_say("LOADED BLOCK ", SEC_NAME[s]);
}

#define AUTOSAVE_IDLE 2500u
#define AUTOSAVE_GAP 20000u
static project_t autosave_buf __attribute__((section(".pool")));
static uint32_t autosave_hash, autosave_ms, autosave_checked;

UI_SMALL static int audio_quiet(void)
{
    uint32_t p, i;
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++)
            if (trk[p].v[i].active)
                return 0;
    for (i = 0; i < NDRUM; i++)
        if (drums.v[i].active)
            return 0;
    return 1;
}

UI_SMALL static void autosave_tick(void)
{
#if FELUCCA_FLASH
    uint32_t h, now = fm1_ms;
    if (!flash_ok || song.playing || transport_req || rec_wait || ft_on || ui.menu ||
        now - ui_input_ms < AUTOSAVE_IDLE || now - autosave_ms < AUTOSAVE_GAP || now - autosave_checked < 1000u)
        return;
    autosave_checked = now;
    proj_capture(&autosave_buf);
    h = autosave_buf.sum;
    if (h == autosave_hash || !audio_quiet())
        return;
    if (st_save(OBJ_AUTOSAVE, &autosave_buf, sizeof autosave_buf) == 0)
        autosave_hash = h;
    autosave_ms = fm1_ms;
#endif
}

static uint8_t drch_seed;
UI_SMALL static void autosave_resume(void)
{
#if FELUCCA_FLASH
    project_t *q = &autosave_buf;
    int n;
    if (!flash_ok)
        return;
    n = st_load(OBJ_AUTOSAVE, &proj_tmp, sizeof proj_tmp);
    if (!proj_import(q, &proj_tmp, n))
        return;
    autosave_hash = q->sum;
    if (drch_seed && !(q->rsv[0u] & 0x80u) && q->g[G_DRCH] >= 0 && q->g[G_DRCH] <= 16 && q->g[G_DRCH] != midi_set[3u]) {
        midi_set[3u] = (uint8_t)q->g[G_DRCH];
        settings_later = 1;
    }
    drch_seed = 0;
    proj_apply(q, 1);
    song.sel = (uint8_t)(q->sel < NTRK ? q->sel : 0u);
    proj_foreign_parts(q);
    for (n = 0; n < (int)NPART; n++)
        trk[n].engine = trk[n].eng_req;
#endif
}
typedef struct {
    uint32_t magic, palette, lowcut, zoom;
    panel_t panel;
#if FELUCCA_ARRANGER
    arr_config_t arrangement;
#endif
    uint32_t lights;
} persist_t;

_Static_assert(sizeof(side_rec_t) == 80u && sizeof(side_rec_t) <= ST_SIDE_MAX, "side record size");
static side_rec_t side_out;
#define PERSIST_SIZE_V22 __builtin_offsetof(persist_t, lights)
_Static_assert(sizeof(persist_t) == PERSIST_SIZE_V22 + 4u, "lights offset");

#if FELUCCA_ARRANGER
_Static_assert(__builtin_offsetof(persist_t, arrangement) == 48u && sizeof(arr_config_t) == 36u,
               "arrangement layout");
#define PERSIST_MAGIC 0x50455233u
#else
#define PERSIST_MAGIC 0x50455232u
#endif

#if FELUCCA_FLASH
static persist_t persist_saved;
static void dsu_boot(void);
#endif

UI_SMALL static void persist_boot(void)
{
#if FELUCCA_ARRANGER
    arr_defaults(&arrangement);
#endif
#if FELUCCA_FLASH
    persist_t p;
    uint32_t f = irq_save();
    flash_ok = FL_JEDEC_OK(FL_FAR(fl_jedec_ram)());
    irq_restore(f);
    if (!flash_ok)
        return;
    fl_plain_window_init();
    {
        uint32_t k;
        for (k = 0; k < SMP_USER_SLOTS; k++)
            smp_user_scan(k);
    }
    {
        int n = st_load(OBJ_SETTINGS, &p, sizeof p);
        if (n == (int)PERSIST_SIZE_V22 && p.magic == PERSIST_MAGIC)
            p.lights = 0;
        if (((n == (int)sizeof p || n == (int)PERSIST_SIZE_V22) && p.magic == PERSIST_MAGIC)
#if FELUCCA_ARRANGER
            || (n == (int)(16u + sizeof(panel_t)) && p.magic == 0x50455232u)
#endif
            ) {
            settings.magic = SETTINGS_MAGIC;
            settings.palette = p.palette;
            settings.lowcut = p.lowcut;
            settings.zoom = p.zoom;
            if (p.panel.magic == PANEL_MAGIC)
                panel = p.panel;
            if (p.magic == PERSIST_MAGIC)
                lights_from_word(p.lights);
            else
                p.lights = 0;
#if FELUCCA_ARRANGER
            if (p.magic == PERSIST_MAGIC && arr_valid(&p.arrangement, 15u))
                arrangement = p.arrangement;
            else
                 p.arrangement = arrangement;
#endif
            persist_saved = p;
            {
                side_rec_t r;
                if (st_side_load(OBJ_SETTINGS, &r, sizeof r) == 0)
                    side_take(&r);
            }
        } else if (n == (int)(8u + sizeof(panel_t)) && p.magic == 0x50455231u) {
            const uint32_t *w = (const uint32_t *)&p;
            panel_t old;
            memcpy(&old, w + 2, sizeof old);
            settings.magic = SETTINGS_MAGIC;
            settings.palette = w[1u];
            settings.lowcut = 0;
            settings.zoom = 0;
            if (old.magic == PANEL_MAGIC)
                panel = old;
        }
    }
    {
        uint32_t i;
        for (i = 0; i < AUDILUNE_VAULT_SLOTS; i++) {
            project_t *q = proj_ptr(i);
            if (!proj_ok(q)) {
                proj_fetch(i);
            } else {
                int n = st_load(OBJ_PROJECT0 + i, &proj_tmp, sizeof proj_tmp);
                if (n != (int)sizeof *q || memcmp(&proj_tmp.aud1, q, sizeof *q))
                    sec_dirty |= (uint8_t)(1u << i);

            }
        }
    }
    dsu_boot();
    up_boot();
    fm6_bank_boot();
#endif
}

UI_SMALL static int project_used(uint32_t slot) { return proj_ok(proj_ptr(slot)); }

#if FELUCCA_FLASH
#define MIDI_PART (2u * ML_N + 8u)
_Static_assert(sizeof(persist_t) + sizeof(dsu_bank_t) + MIDI_PART <= ST_PAYLOAD_MAX, "settings sector limit");
static uint8_t midi_saved[MIDI_PART];

UI_SMALL static void midi_part_fill(uint8_t *b)
{
    uint32_t k;
    for (k = 0; k < ML_N; k++) {
        b[2u * k] = (uint8_t)ml_tab[k];
        b[2u * k + 1u] = (uint8_t)(ml_tab[k] >> 8);
    }
    b[2u * ML_N] = midi_set[0u];
    b[2u * ML_N + 1u] = midi_set[1u];
    b[2u * ML_N + 2u] = midi_set[2u];
    b[2u * ML_N + 3u] = (uint8_t)(midi_set[3u] | (drum_preview != 0u) << 5 | (step_adv_off != 0u) << 6 | midi_set[4u] << 7);
    b[2u * ML_N + 4u] = (uint8_t)(midi_set[5u] | (fxfade > 15u ? 15u : fxfade) << 4);
    b[2u * ML_N + 5u] = midi_set[6u];
    b[2u * ML_N + 6u] = (uint8_t)(midi_set[7u] | midi_set[9u] << 1 | (flt_reso > 10u ? 10u : flt_reso) << 2);
    b[2u * ML_N + 7u] = midi_set[8u];
}

UI_SMALL static int midi_part_default(const uint8_t *b)
{
    static const uint8_t def[10u] = MIDI_SET_DEF;
    uint32_t k;
    for (k = 0; k < 2u * ML_N; k++)
        if (b[k])
            return 0;
    return b[2u * ML_N] == def[0u] && b[2u * ML_N + 1u] == def[1u] && b[2u * ML_N + 2u] == def[2u] &&
           b[2u * ML_N + 3u] == (uint8_t)(def[3u] | def[4u] << 7) && b[2u * ML_N + 4u] == def[5u] && b[2u * ML_N + 5u] == def[6u] &&
           b[2u * ML_N + 6u] == (uint8_t)(def[7u] | def[9u] << 1) && b[2u * ML_N + 7u] == def[8u];
}

UI_SMALL static void midi_part_read(const uint8_t *b, uint32_t n)
{
    uint32_t k, e;
    fm1_irq_off();
    for (k = 0; k < ML_N; k++) {
        e = (uint32_t)b[2u * k] | (uint32_t)b[2u * k + 1u] << 8;
        ml_tab[k] = (uint16_t)((e >> 9) && ml_free_cc(e & 127u) && ml_id(e >> 9) < P_COUNT ? e : 0u);
    }
    if (n >= 2u * ML_N + 4u) {
        for (k = 0; k < 3u; k++)
            midi_set[k] = (uint8_t)(b[2u * ML_N + k] <= 16u ? b[2u * ML_N + k] : k + 1u);
        midi_set[3u] = (uint8_t)((b[2u * ML_N + 3u] & 31u) <= 16u ? b[2u * ML_N + 3u] & 31u : 10u);
        midi_set[4u] = (uint8_t)(b[2u * ML_N + 3u] >> 7);
        drum_preview = (uint8_t)((b[2u * ML_N + 3u] >> 5) & 1u);
        step_adv_off = (uint8_t)((b[2u * ML_N + 3u] >> 6) & 1u);
        song.g[G_CH1] = midi_set[0u];
        song.g[G_CH2] = midi_set[1u];
        song.g[G_CH3] = midi_set[2u];
        song.g[G_CH4] = 4;
        song.g[G_CH5] = 5;
        song.g[G_DRCH] = midi_set[3u];
        song.g[G_CHOTH] = midi_set[4u];
    }
    if (n >= MIDI_PART) {
        midi_set[5u] = (uint8_t)((b[2u * ML_N + 4u] & 15u) >= 1u && (b[2u * ML_N + 4u] & 15u) <= 12u ? b[2u * ML_N + 4u] & 15u : 2u);
        song.g[G_BEND] = midi_set[5u];
        fxfade = (uint8_t)((b[2u * ML_N + 4u] >> 4) & 15u);
        song.g[G_FXFADE] = fxfade;
        midi_set[6u] = (uint8_t)(b[2u * ML_N + 5u] <= 16u ? b[2u * ML_N + 5u] : 0u);
        song.g[G_FXCH] = midi_set[6u];
        midi_set[7u] = (uint8_t)(b[2u * ML_N + 6u] & 1u);
        midi_set[9u] = (uint8_t)((b[2u * ML_N + 6u] >> 1) & 1u);
        flt_reso = (uint8_t)((b[2u * ML_N + 6u] >> 2) & 15u);
        if (flt_reso > 10u)
            flt_reso = 10u;
        song.g[G_DRMAP] = midi_set[7u];
        song.g[G_CLKOUT] = midi_set[9u];
        midi_set[8u] = (uint8_t)(b[2u * ML_N + 7u] <= 16u ? b[2u * ML_N + 7u] : 0u);
        song.g[G_RMCH] = midi_set[8u];
    }
    fm1_irq_on();
}

UI_SMALL static int settings_write(const persist_t *p)
{
    uint8_t mp[MIDI_PART];
    int rc;
    if (!dsu_valid(&dsu))
        dsu_defaults();
    midi_part_fill(mp);
    side_fill(&side_out);
    st_side = &side_out;
    st_side_len = sizeof side_out;
    rc = st_save3(OBJ_SETTINGS, p, sizeof *p, &dsu, sizeof dsu, mp, midi_part_default(mp) ? 0u : sizeof mp);
    st_side = 0;
    if (rc)
        return -1;
    dsu_dirty = 0;
    fav_dirty = 0;
    memcpy(midi_saved, mp, sizeof midi_saved);
    return 0;
}

static void dsu_boot(void)
{
    st_hdr_t h;
    uint32_t len = sizeof(persist_t) + sizeof(dsu_bank_t), k;
    dsu_defaults();
    for (k = 0; k < ML_N; k++)
        ml_tab[k] = 0;
    if (st_current(OBJ_SETTINGS, &h) >= 0 &&
        (h.len == len || h.len == len + 2u * ML_N || h.len == len + 2u * ML_N + 4u || h.len == len + MIDI_PART) &&
        dsu_valid((const dsu_bank_t *)(st_buf + sizeof(persist_t)))) {
        memcpy(&dsu, st_buf + sizeof(persist_t), sizeof dsu);
        for (k = 0; k < DSU_N; k++)
            dsu_fix_kit(&dsu.k[k]);
        if (h.len > len)
            midi_part_read(st_buf + len, h.len - len);
        drch_seed = h.len == len;
    }
    midi_part_fill(midi_saved);
    dsu_dirty = 0;
}
#endif
UI_SMALL static void persist_fill(persist_t *p)
{
    memset(p, 0, sizeof *p);
    p->magic = PERSIST_MAGIC;
    p->palette = settings.palette;
    p->lowcut = settings.lowcut;
    p->zoom = settings.zoom;
    p->panel = panel;
    p->lights = lights_word();
#if FELUCCA_ARRANGER
    p->arrangement = arrangement;
#endif
}

UI_SMALL static void settings_save(void)
{
#if FELUCCA_FLASH
    persist_t p;
    if (!flash_ok)
        return;
    persist_fill(&p);
    {
        uint8_t mp[MIDI_PART];
        midi_part_fill(mp);
        if (!memcmp(&p, &persist_saved, sizeof p) && !memcmp(mp, midi_saved, sizeof mp) && !fav_dirty)
            return;
    }
    if (settings_write(&p) == 0)
        persist_saved = p;
#endif
}

#if FELUCCA_FLASH
_Static_assert(sizeof(project_t) <= ST_PAYLOAD_MAX, "project fits flash sector");

UI_SMALL static int panel_valid(const panel_t *q)
{
    uint32_t i, b = 0, e = 0;
    if (q->magic != PANEL_MAGIC)
        return 0;
    for (i = 0; i < NB; i++) {
        if (q->btn[i] >= 14u || (b >> q->btn[i]) & 1u)
            return 0;
        b |= 1u << q->btn[i];
    }
    for (i = 0; i < NE; i++) {
        if (q->enc[i] >= 7u || (e >> q->enc[i]) & 1u || (q->dir[i] != 1 && q->dir[i] != -1))
            return 0;
        e |= 1u << q->enc[i];
    }
    return 1;
}

UI_SMALL static uint32_t settings_restore(const void *raw, uint32_t n)
{
    persist_t p;
    if (n != sizeof p && n != PERSIST_SIZE_V22)
        return 2;
    memset(&p, 0, sizeof p);
    memcpy(&p, raw, n);
    if (p.magic != PERSIST_MAGIC || p.palette >= NPALETTES || p.lowcut > 1u || p.zoom > 1u || !panel_valid(&p.panel))
        return 2;
#if FELUCCA_ARRANGER
    if (!arr_valid(&p.arrangement, 15u))
        return 2;
    if ((song.playing || transport_req) && memcmp(&p.arrangement, &arrangement, sizeof arrangement))
        return 3;
#endif
    if (!flash_ok || settings_write(&p))
        return 4;
    persist_saved = p;
    settings.palette = p.palette;
    settings.lowcut = p.lowcut;
    settings.zoom = p.zoom;
    panel = p.panel;
#if FELUCCA_ARRANGER
    arrangement = p.arrangement;
#endif
    lights_from_word(p.lights);
    song.g[G_SYNC] = (int16_t)lights_sync;
    song.g[G_MIDI] = (int16_t)lights_mout;
    song.g[G_ROUTE] = (int16_t)lights_min;
    palette_set(settings.palette);
    fx_lowcut = (uint8_t)(settings.lowcut != 0);
    ui.force = 1;
    return 0;
}

UI_SMALL static uint32_t project_restore(uint32_t slot, const void *raw, uint32_t n)
{
    project_t *dst = proj_ptr(slot);
    if (song.playing || transport_req)
        return 3;
    if (slot < AUDILUNE_VAULT_SLOTS && !n) {
        if (!flash_ok || st_save(OBJ_PROJECT0 + slot, raw, 0))
            return 4;
        memset(dst, 0, sizeof *dst);
        sec_dirty &= (uint8_t)~(1u << slot);
        return 0;
    }
    if (!proj_import(&autosave_buf, raw, (int)n))
        return 2;
    if (slot == AUDILUNE_VAULT_SLOTS) {
        project_apply(&autosave_buf);
        return 0;
    }
    if (!flash_ok || st_save(OBJ_PROJECT0 + slot, &autosave_buf, sizeof autosave_buf))
        return 4;
    memcpy(dst, &autosave_buf, sizeof *dst);
    sec_dirty &= (uint8_t)~(1u << slot);
    return 0;
}
#endif

#if FELUCCA_ARRANGER
UI_SMALL static void arrangement_save(void)
{
    if (song.playing || transport_req) {
        ui_message("STOP BEFORE SAVE");
        return;
    }
    settings_save();
#if FELUCCA_FLASH
    if (flash_ok) {
        ui_message(memcmp(&persist_saved.arrangement, &arrangement, sizeof arrangement) ? "SAVE ERROR" : "SONG SAVED");
        return;
    }
#endif
    ui_message("SONG IN RAM ONLY");
}

UI_SMALL static void section_store(uint32_t s)
{
    project_t *dst = proj_ptr(s);
    s %= AUDILUNE_VAULT_SLOTS;
    fm1_irq_off();
    proj_capture(dst);
    live_sec = (int8_t)s;
    fm1_irq_on();
    sec_dirty |= (uint8_t)(1u << s);
}

UI_SMALL static void section_load(uint32_t s)
{
    s %= AUDILUNE_VAULT_SLOTS;
    project_apply(proj_ptr(s));
    live_sec = (int8_t)s;
}

UI_SMALL static void sections_write(void)
{
    uint32_t i;
#if FELUCCA_FLASH
    if (flash_ok)
        for (i = 0; i < AUDILUNE_VAULT_SLOTS; i++) {
            project_t *p = proj_ptr(i);
            if (((sec_dirty >> i) & 1u) && st_save(OBJ_PROJECT0 + i, p, sizeof *p) == 0)
                sec_dirty &= (uint8_t)~(1u << i);
        }
    if (!flash_ok)
#endif
        sec_dirty = 0;
    (void)i;

    if (song_dirty || fav_dirty) {
        song_dirty = 0;
        settings_save();
    }
}

UI_SMALL static void persist_flush_now(void)
{
    sections_write();
#if FELUCCA_FLASH
    if (flash_ok && !arrangement_clock.running) {
        proj_capture(&autosave_buf);
        if (autosave_buf.sum != autosave_hash && st_save(OBJ_AUTOSAVE, &autosave_buf, sizeof autosave_buf) == 0)
            autosave_hash = autosave_buf.sum;
    }
#endif
}

UI_SMALL static void sections_flush(void)
{
    static uint32_t tried;
    if (srec_done) {
        song_dirty = srec_done != 0xFFu;
        if (song_dirty) {
            char b[8u];
            fmt_int(b, srec_done);
            ui_say("SONG PARTS ", b);
        } else {
            ui_message("NO SONG");
        }
        srec_done = 0;
    }
    if ((uint32_t)song.g[G_SYNC] != lights_sync) {
        lights_sync = (uint8_t)song.g[G_SYNC];
        settings_later = 1;
    }
    if ((uint32_t)(song.g[G_MIDI] != 0) != lights_mout) {
        lights_mout = (uint8_t)(song.g[G_MIDI] != 0);
        settings_later = 1;
    }
    if ((uint32_t)song.g[G_ROUTE] != lights_min) {
        lights_min = (uint8_t)song.g[G_ROUTE];
        settings_later = 1;
    }
    {
        static const uint8_t gid[10u] = {G_CH1, G_CH2, G_CH3, G_DRCH, G_CHOTH, G_BEND, G_FXCH, G_DRMAP, G_RMCH, G_CLKOUT};
        uint32_t k;
        for (k = 0; k < 10u; k++)
            if ((uint32_t)song.g[gid[k]] != midi_set[k]) {
                midi_set[k] = (uint8_t)song.g[gid[k]];
                settings_later = 1;
            }
        if ((uint32_t)song.g[G_FXFADE] != fxfade) {
            fxfade = (uint8_t)song.g[G_FXFADE];
            settings_later = 1;
        }
    }
    if (settings_later) {
        settings_later = 0;
        song_dirty = 1;
    }
    if ((!sec_dirty && !song_dirty) || song.playing || transport_req || !audio_quiet() || fm1_ms - ui_input_ms < 1500u ||
        fm1_ms - tried < 5000u)
        return;
    tried = fm1_ms;
    sections_write();
    if (sec_dirty)
        ui_message("SAVE ERROR: RETRYING");
}
#endif
#endif /* PROJ_HOST */
