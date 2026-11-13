/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 AUDILUNE Firmware
 * Block 4: Infinite Algorithmic Compositions Engine & 6-Genre Macro Rules */

#ifndef AUDILUNE_GEN_C
#define AUDILUNE_GEN_C

/* Internal generative helpers */
static uint32_t next_rand(void)
{
    static uint32_t gen_seed = 0x5A17C39Du;
    gen_seed ^= gen_seed << 13;
    gen_seed ^= gen_seed >> 17;
    gen_seed ^= gen_seed << 5;
    return gen_seed;
}

static uint8_t audilune_gen_clamp_note(int32_t note)
{
    if (note < 24)
        return 24u;
    if (note > 108)
        return 108u;
    return (uint8_t)note;
}

/* Shared state read/written by ui_input.c (HOME 3s hold & ALGORITHM encoder) */
static volatile uint8_t audilune_infinite_on;
static volatile uint8_t audilune_genre;          /* 0..5: Ambient, Downtempo, Glitch Hop, Punchy, Experimental, Industrial */
static volatile uint8_t audilune_arp_drift_mask; /* Bitmask of tracks selected for ARP + OP1..OP6 generative drift */

enum {
    GENRE_AMBIENT = 0,
    GENRE_DOWNTEMPO = 1,
    GENRE_GLITCH_HOP = 2,

    GENRE_PUNCHY = 3,
    GENRE_EXPERIMENTAL = 4,
    GENRE_INDUSTRIAL = 5,
    GENRE_COUNT = 6
};

static const char *const AUDILUNE_GENRE_NAMES[3 + 3] = {
    "AMBIENT", "DOWNTEMPO", "GLITCH HOP", "PUNCHY", "EXPERIMENTAL", "INDUSTRIAL"
};


/* Genre behavioral DNA: note density (0..100%), tie chance, ratchet chance, step FX chance, micro-jitter */
typedef struct {
    uint8_t synth_prob;
    uint8_t drum_prob;
    uint8_t tie_prob;
    uint8_t ratchet_prob;
    uint8_t fx_lock_prob;
    uint8_t accent_prob;
    int8_t micro_jitter;
    uint8_t default_len;
} genre_dna_t;

static const genre_dna_t GENRE_DNA[GENRE_COUNT] = {
    /* Ambient */      {28, 18, 65,  4,  6, 10,  4, 64},
    /* Downtempo */    {48, 45, 30, 12, 10, 25, 10, 32},
    /* Glitch Hop */   {55, 62, 10, 42, 32, 40, 18, 32},
    /* Punchy */       {60, 70, 12, 15,  8, 55,  2, 16},
    /* Experimental */ {50, 52, 25, 35, 25, 35, 22, 24},
    /* Industrial */   {72, 85,  8, 48, 38, 75,  3, 16}
};

/* Macro-Genre DSP morph targets: attack, decay, release, warmth/saturation, chorus, delay, reverb */
typedef struct {
    int16_t atk;
    int16_t dec;
    int16_t rel;
    int16_t dist;
    int16_t chor;
    int16_t dly;
    int16_t rev;
} genre_dsp_t;

static const genre_dsp_t GENRE_DSP[GENRE_COUNT] = {
    /* Ambient */      {62, 95, 102,  8, 54, 68, 92},
    /* Downtempo */    {24, 76,  72, 28, 34, 48, 56},
    /* Glitch Hop */   { 4, 38,  30, 46, 18, 42, 26},
    /* Punchy */       { 2, 44,  34, 36, 12, 24, 22},
    /* Experimental */ {18, 64,  68, 52, 48, 74, 64},
    /* Industrial */   { 4, 52,  38, 86, 14, 38, 42}
};

static void audilune_genre_morph_dsp(uint8_t g)
{
    const genre_dsp_t *dsp = &GENRE_DSP[g % GENRE_COUNT];
    uint32_t i;
    for (i = 0; i < NPART; i++) {
        trk[i].p[P_ATK] = dsp->atk;
        trk[i].p[P_DEC] = dsp->dec;
        trk[i].p[P_REL] = dsp->rel;
        trk[i].p[P_DIST] = dsp->dist;
        trk[i].p[P_CHOR] = dsp->chor;
        trk[i].p[P_DLY] = dsp->dly;
        trk[i].p[P_REV] = dsp->rev;
    }
}

static uint8_t audilune_inf_was;
static uint8_t audilune_genre_was = 0xFFu;
static uint32_t audilune_gen_bar_last = 0xFFFFFFFFu;
static uint8_t audilune_gen_arp_was;

/* Fast deterministic 0..99 roll using the audio engine's RNG */
static uint32_t gen_pct(void)
{
    return (rng() >> 8) % 100u;
}

/* Builds a live harmonic scale mask from active non-generative anchor tracks */
static uint32_t gen_anchor_scale_mask(uint32_t trk_idx, int32_t *out_root)

{
    uint32_t i, s, combined = 0u;
    int32_t root = trk[0u].p[P_ROOT];
    for (i = 0; i < NPART; i++) {
        if (i == trk_idx)
            continue;
        if ((audilune_arp_drift_mask >> i) & 1u)
            continue;
        for (s = 0; s < trk_len(&trk[i]); s++) {
            const step_t *st = &trk[i].step[s];
            if (st->time == ST_NOTE && st->n > 0u) {
                uint32_t semi = (uint32_t)((st->note[0u] - root + 120) % 12);
                combined |= (1u << semi);
            }
        }
    }
    /* Count active pitch classes; if fewer than 4 notes found in anchor tracks, fall back to track scale */
    uint32_t bits = 0u;
    for (i = 0; i < 12u; i++)
        bits += (combined >> i) & 1u;
    *out_root = root;
    if (bits >= 4u)
        return combined;
    return scale_mask(&trk[trk_idx]);
}

/* Quantizes a MIDI note into the active harmonic scale mask */
static uint8_t gen_quantize_note(int32_t raw_note, int32_t root, uint32_t mask)
{
    int32_t n = clamp(raw_note, 24, 108);
    uint32_t guard = 12u;
    if (!mask)
        return (uint8_t)n;
    while (guard-- && !((mask >> (uint32_t)((n - root + 120) % 12)) & 1u))
        n--;
    return (uint8_t)clamp(n, 24, 108);
}
/* Generates or mutates one synth track (parts 0..4) according to role and active Macro-Genre */
static void gen_populate_synth(uint32_t ti, const genre_dna_t *dna, int full_seed)
{
    track_t *t = &trk[ti];
    int32_t root = 0;
    uint32_t mask = gen_anchor_scale_mask(ti, &root);
    uint32_t len = full_seed ? dna->default_len : trk_len(t);
    int32_t base_oct = (ti == 0u) ? 36 : (ti == 1u) ? 48 : (ti == 2u) ? 60 : (ti == 3u) ? 72 : 48;
    uint32_t s;

    if (full_seed) {
        steps_clear(t);
        t->p[P_SLEN] = (int16_t)len;
        t->p[P_SDIV] = (audilune_genre == GENRE_AMBIENT) ? 1 : 2; /* 1/8 for Ambient, 1/16 otherwise */
    }

    for (s = 0; s < len && s < NSTEP; s++) {
        step_t *st = &t->step[s];
        /* In mutation mode, only refresh ~25% of steps per bar to keep motifs recognizable */
        if (!full_seed && gen_pct() >= 25u)
            continue;

        lock_del(t, s, P_ED_FX);
        t->micro[s] = 0;

        /* Bass track (ti == 0) anchors on downbeats */
        uint32_t prob = dna->synth_prob;
        if (ti == 0u && (s % 4u == 0u))
            prob = prob < 75u ? 75u : prob;

        if (gen_pct() < prob) {
            int32_t offset = (int32_t)(rng() % 15u) - 3;
            uint8_t note = gen_quantize_note(base_oct + root + offset, root, mask);
            memset(st, 0, sizeof *st);
            st->time = ST_NOTE;
            st->note[0u] = note;
            st->n = 1u;
            st->vel = (gen_pct() < dna->accent_prob) ? 124u : (uint8_t)(72u + (rng() & 31u));
            if (st->vel >= 120u)
                st->flags |= SF_ACCENT;

            /* Ratchets (x2..x4) driven by genre DNA */
            if (gen_pct() < dna->ratchet_prob)
                st->rat = (uint8_t)(1u + (rng() & 1u));

            /* Micro-timing humanization / jitter */
            if (dna->micro_jitter > 0) {
                int32_t jit = (int32_t)(rng() % (uint32_t)(dna->micro_jitter * 2 + 1)) - dna->micro_jitter;
                t->micro[s] = (int8_t)clamp(jit, MICRO_MIN, MICRO_MAX);
            }

            /* Step-level Punch-In FX lock (OP1..OP6 -> P_ED_FX 1..6) */
            if (gen_pct() < dna->fx_lock_prob)
                lock_set(t, s, P_ED_FX, (int32_t)(1u + (rng() % 6u)));
        } else if (s > 0u && t->step[s - 1u].time != ST_REST && gen_pct() < dna->tie_prob) {
            memset(st, 0, sizeof *st);
            st->time = ST_TIE;
        } else {
            memset(st, 0, sizeof *st);
            st->time = ST_REST;
        }
    }
    t->seq_active = 1;
}

/* Generates or mutates the drum track (track 6 / TRK_DRUM) for the active Macro-Genre */
static void gen_populate_drums(const genre_dna_t *dna, int full_seed)
{
    track_t *t = TDRUM;
    uint32_t len = full_seed ? dna->default_len : trk_len(t);
    uint32_t s;

    if (full_seed) {
        steps_clear(t);
        t->p[P_SLEN] = (int16_t)len;
        t->p[P_SDIV] = 2; /* 1/16 */
    }

    for (s = 0; s < len && s < NSTEP; s++) {
        dstep_t *ds = &t->dstep[s];
        if (!full_seed && gen_pct() >= 30u)
            continue;
        memset(ds, 0, sizeof *ds);
        lock_del(t, s, P_ED_FX);

        /* Lane 0: Kick on 4/4 beats (or syncopated in Glitch Hop / Experimental) */
        if ((s % 4u == 0u && audilune_genre != GENRE_AMBIENT) ||
            (audilune_genre == GENRE_GLITCH_HOP && (s % 16u == 6u || s % 16u == 10u) && gen_pct() < 60u)) {
            dstep_set(ds, 0u, LV_HARD, 0u);
        }

        /* Lane 2: Snare / Clap on backbeats (steps 4 & 12) */
        if ((s % 8u == 4u && audilune_genre != GENRE_AMBIENT) ||
            (gen_pct() < (dna->drum_prob / 4u))) {
            dstep_set(ds, 2u, (s % 8u == 4u) ? LV_HARD : LV_GHOST, 0u);
        }

        /* Lane 4: Hi-Hat groove with genre ratchets */
        if (gen_pct() < dna->drum_prob) {
            uint32_t rat = (gen_pct() < dna->ratchet_prob) ? (1u + (rng() & 1u)) : 0u;
            uint32_t lvl = (s % 2u == 0u) ? LV_NORM : LV_GHOST;
            dstep_set(ds, 4u, lvl, rat);
        }

        /* Step-level Punch-In FX lock on drums */
        if (gen_pct() < dna->fx_lock_prob)
            lock_set(t, s, P_ED_FX, (int32_t)(1u + (rng() % 6u)));
    }
    t->seq_active = 1;
}
/* Triggers step-level Punch-In FX (P_ED_FX 1..6) when the sequencer enters a step with an FX lock */
static void audilune_step_fx_apply(const track_t *t)
{
    int32_t fx_val = t->p[P_ED_FX];
    if (fx_val >= 1 && fx_val <= 6) {
        if (!punch.keybit)
            punch.req = (int8_t)(fx_val - 1);
    } else if (!punch.keybit && punch.req >= 0 && punch.req < 6) {
        punch.req = -1;
    }
}

/* Back-Home Resolution: 1-bar recovery cycle fading parameters & snapping rhythm on bar boundary */
static uint8_t audilune_back_home_active;
static uint32_t audilune_back_home_start_beat;

static void audilune_back_home_trigger(void)
{
    audilune_back_home_active = 1u;
    audilune_back_home_start_beat = clk_beat;
}

static void audilune_back_home_tick(void)
{
    uint32_t i, elapsed;
     if (!audilune_back_home_active)
        return;

    elapsed = clk_beat - audilune_back_home_start_beat;
    for (i = 0; i < NTRK; i++) {
        locks_restore(&trk[i]);
        if (elapsed >= 4u)
            memset(trk[i].micro, 0, sizeof trk[i].micro);
    }
    if (elapsed >= 4u)
        audilune_back_home_active = 0u;
}

/* Main generative engine tick: called once per audio block inside events_block() */
static void audilune_gen_block(void)
{
    uint32_t g = audilune_genre % GENRE_COUNT;
    const genre_dna_t *dna = &GENRE_DNA[g];
    uint32_t i;
    int genre_changed = (g != audilune_genre_was);

    if (genre_changed) {
        audilune_genre_was = (uint8_t)g;
        audilune_genre_morph_dsp((uint8_t)g);
    }

    /* Trigger 1-bar Back-Home Resolution when ARP generative drift turns off */
    if (audilune_gen_arp_was && !audilune_arp_drift_mask)
        audilune_back_home_trigger();
    audilune_gen_arp_was = audilune_arp_drift_mask;
    audilune_back_home_tick();

    /* Handle Infinite Algorithmic Compositions Engine activation (HOME 3s hold) */
    if (audilune_infinite_on) {
        int just_enabled = !audilune_inf_was || genre_changed;
        audilune_inf_was = 1u;

        if (!song.playing && !transport_req)
            transport_req = 1u;

        if (just_enabled) {
            for (i = 0; i < NPART; i++)
                gen_populate_synth(i, dna, 1);
            gen_populate_drums(dna, 1);
            audilune_gen_bar_last = clk_beat >> 2;
        } else if (song.playing && !(clk_beat & 3u) && (clk_beat >> 2) != audilune_gen_bar_last) {
            /* Evolve composition subtly on every 2nd bar boundary */
            audilune_gen_bar_last = clk_beat >> 2;

            if ((audilune_gen_bar_last & 1u) == 0u) {
                for (i = 0; i < NPART; i++)
                    gen_populate_synth(i, dna, 0);
                gen_populate_drums(dna, 0);
            }
        }
    } else {
        audilune_inf_was = 0u;
    }
}

#endif /* AUDILUNE_GEN_C */
uint8_t audilune_gen_eval_step(uint8_t track, uint8_t step, uint8_t genre, uint8_t *out_note, uint8_t *out_vel) {
    /* Trigger probability thresholds (%) across the 6 AUDILUNE Macro-Genres:
       0: Ambient, 1: Downtempo, 2: Glitch Hop, 3: Punchy, 4: Experimental, 5: Industrial */
    static const uint8_t k_genre_prob[6] = { 28, 42, 65, 60, 52, 75 };
    if (genre > 5) {
        genre = 0;
    }

    uint32_t r = next_rand();
    uint8_t chance = (uint8_t)(r % 100u);

    /* Encourage rhythmic downbeats on 16-step bar boundaries */
    uint8_t threshold = k_genre_prob[genre];
    if ((step % 16u) == 0u) {
        threshold = (threshold + 25u > 95u) ? 95u : (uint8_t)(threshold + 25u);
    }

    if (chance >= threshold) {
        return 0; /* Rest step */
    }

    /* Track-aware base octaves: Track 0 = Bass, Tracks 1-4 = Mid/Lead, Track 5 = Drum/Perc */
    uint8_t base_note = 60;
    if (track == 0) {
        base_note = 36 + (uint8_t)((r >> 8) % 12u);
    } else if (track == 5) {
        base_note = 36 + (uint8_t)((r >> 8) % 8u);
    } else {
        base_note = 48 + (uint8_t)((r >> 8) % 24u);
    }

    if (out_note) {
        *out_note = (track == 5) ? base_note : audilune_gen_clamp_note(base_note);
    }
    if (out_vel) {
        uint8_t base_vel = (genre == 0) ? 65 : 95;
        uint8_t spread   = (uint8_t)((r >> 16) % 30u);
        uint16_t vel     = (uint16_t)base_vel + spread;
        *out_vel         = (vel > 127u) ? 127u : (uint8_t)vel;
    }

    return 1; /* Trigger note */
}
