/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Adapted for AUDILUNE Firmware (Block 3: hardware_controls) */

#include <stdio.h>

/* ------------------------------------------------ AUDILUNE Helpers --- */
#define AUDILUNE_GENRE_COUNT 6
#define AUDILUNE_THEME_COUNT 8

static const char *const AUDILUNE_GENRE_NAMES[AUDILUNE_GENRE_COUNT] = {
    "AMBIENT", "DOWNTEMPO", "GLITCH HOP", "PUNCHY", "EXPERIMENTAL", "INDUSTRIAL"
};

static const char *const AUDILUNE_THEME_NAMES[AUDILUNE_THEME_COUNT] = {
    "TEAL", "ORANGE", "BURGY", "PURPLE", "PINK", "BLUE", "MINT", "GREY"
};

static uint8_t audilune_genre_idx = 0;
static uint8_t audilune_theme_idx = 0;
static uint8_t audilune_infinite_on = 0;
static uint8_t audilune_pit_latch = 0; /* 1 = SELECT+PIT bend, 2 = EDIT+PIT transpose */

/* Safe HUD popup wrapper for Block 2 bottom status bar (1.5s linger) */
UI_SMALL static void audilune_hud_popup(const char *label, int32_t val, const char *unit)
{
    char buf[40];
    if (unit && unit[0])
        snprintf(buf, sizeof(buf), "%s: %ld%s", label, (long)val, unit);
    else
        snprintf(buf, sizeof(buf), "%s: %ld", label, (long)val);
    ui_message(buf);
}

UI_SMALL static void audilune_hud_text(const char *prefix, const char *value)
{
    char buf[40];
    snprintf(buf, sizeof(buf), "%s: %s", prefix, value);
    ui_message(buf);
}

/* ----------------------------------------------------------- LEDs --- */
static uint8_t led_pos[41];

UI_SMALL static void led_pos_init(void)
{
    uint32_t id, p, r;
    for (id = 0; id < 41u; id++) {
        led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (FM1_KEYMAP[r][p] == (int8_t)id)
                    led_pos[id] = (uint8_t)((p << 3) | r);
    }
}

UI_SMALL static void led_put(uint8_t *nl, uint32_t id, int on)
{
    uint8_t q = led_pos[id];
    if (q != 0xFF && on)
        nl[q >> 3] |= (uint8_t)(1u << (q & 7u));
}

static const uint8_t FAM_BTN[FAM_COUNT] = {B_HOME, B_ENV, B_LFO, B_FX, B_SCL, B_EDIT, B_GLO, B_SAVE,
                                           B_ARP, B_SEQ, B_HOME};

UI_SMALL static uint32_t cur_fam(void) { return ui.home ? FAM_HOME : cur_page()->fam; }
UI_SMALL static uint32_t cur_btn(void)
{
    if (!ui.home && cur_page()->scope == SC_SONG)
        return B_SAVE;
    if (!ui.home && cur_page()->scope == SC_DRUM)
        return B_SEQ;
    return FAM_BTN[cur_fam()];
}

UI_SMALL static int play_led(void)
{
    uint32_t p = clk_pos, lat = HALF_FRAMES * 3u / 2u * (uint32_t)song.g[G_BPM];
    if (!song.playing)
        return 0;
    p = p >= lat ? p - lat : p + BEAT_U - lat;
    return p < BEAT_U / 4u;
}

UI_SMALL static uint32_t keys_sounding(const track_t *t)
{
    uint32_t i, m = 0;
    if (is_drum(t)) {
        for (i = 0; i < DRUM_LANES; i++)
            if (pad_lit[i])
                m |= 1u << key_of_white(i);
        return m;
    }
    for (i = 0; i < NVOICE; i++) {
        const voice_t *v = &t->v[i];
        if (v->active && v->gate && v->stage <= 2) {
            uint32_t k, note = v->note;
            for (k = 0; k < 27u; k++)
                if (kb_map(t, k) == note)
                    m |= 1u << k;
        }
    }
    if (t == TSEL)
        for (i = 0; i < 27u; i++)
            if (key_lit[i])
                m |= 1u << i;
    return m;
}

UI_SMALL static uint32_t scale_keys(uint32_t root_only)
{
    uint32_t i, m = 0, root = (uint32_t)trk[0].p[P_ROOT] % 12u, mask = SCALE_MASK[clamp(trk[0].p[P_SCALE], 0, NSCALES - 1)];
    for (i = 0; i < 27u; i++) {
        uint32_t d = (53u + i - root + 120u) % 12u;
        if ((mask >> d) & 1u && (!root_only || !d))
            m |= 1u << i;
    }
    return m;
}

UI_SMALL static uint32_t erase_lanes(const track_t *t)
{
    uint32_t i, m = 0;
    if (is_drum(t))
        for (i = 0; i < trk_len(t); i++) {
            uint32_t l, d = dstep_mask(&t->dstep[i]);
            for (l = 0; d; l++, d >>= 1)
                if (d & 1u)
                    m |= 1u << key_of_white(l);
        }
    return m;
}

UI_SMALL static uint32_t keys_lit(void)
{
    uint32_t m = 0, i, blink = (fm1_ms / 125u) & 1u;
    track_t *t = TSEL;
    switch (ui.layer) {
    case LY_FX:
        return punch.req >= 0 ? 1u << punch_key_of_fx((uint32_t)punch.req) : 0u;
    case LY_STEP: {
        uint32_t len = trk_len(t);
        for (i = 0; i < 16u; i++) {
            uint32_t idx = ui.step_page * 16u + i, on;
            if (idx >= len)
                continue;
            on = is_drum(t) ? dstep_has(&t->dstep[idx], pen_lane) : step_on(&t->step[idx]);
            if (song.playing && idx == t->seq_idx)
                on = !on || blink;
            if (on)
                m |= 1u << key_of_white(i);
        }
        if (is_drum(t))
            m |= 1u << drum_black_key(pen_lane);
        return m | fm1_in.notes;
    }
    case LY_SCALE:
        if (lights_notes)
            return (blink ? scale_keys(1) : 0u) | keys_sounding(t) | fm1_in.notes;
        return scale_keys(0) & ~(blink ? 0u : scale_keys(1));
    case LY_MIX:
        for (i = 0; i < NTRK && i < 6u; i++) {
            if (!trk_silent(&trk[i]))
                m |= 1u << key_of_white(i);
            if ((song.solo >> i) & 1u)
                m |= 1u << key_of_white(6u + i);
        }
        if (fill_now)
            m |= 1u << key_of_white(12);
        if (fill_arm || fill_bar_on)
            m |= 1u << key_of_white(13);
        if (play_led())
            m |= 1u << key_of_white(15);
        return m;
    case LY_ERASE:
        return (lights_notes ? 0u : erase_lanes(t)) | fm1_in.notes | (lights_notes ? keys_sounding(t) : 0u);
    default:
        break;
    }
    if (kb_grid) {
        uint32_t len = trk_len(TDRUM), b0 = drum_cursor / 16u * 16u;
        for (i = 0; i < 16u; i++) {
            uint32_t idx = b0 + i, on;
            if (idx >= len)
                continue;
            on = dstep_has(&TDRUM->dstep[idx], drum_lane);
            if (song.playing && idx == TDRUM->seq_idx)
                on = !on || blink;
            if (on)
                m |= 1u << key_of_white(i);
        }
        return m | fm1_in.notes | 1u << drum_black_key(drum_lane);
    }
    m = fm1_in.notes;
    if (is_drum(t) || lights_notes)
        m |= keys_sounding(t);
    return m;
}

UI_SMALL static uint32_t keys_notes_dim(void)
{
    if (!lights_notes)
        return 0u;
    switch (ui.layer) {
    case LY_FX:
    case LY_STEP:
    case LY_MIX:
        return keys_sounding(TSEL);
    case LY_SCALE:
        return scale_keys(0);
    case LY_ERASE:
        return erase_lanes(TSEL);
    default:
        return 0u;
    }
}

UI_SMALL static uint32_t keys_guide(void)
{
    if (ui.layer == LY_PLAY && !is_drum(TSEL))
        return 0u;
    return 1u << key_of_white(0) | 1u << key_of_white(4) | 1u << key_of_white(8) | 1u << key_of_white(12);
}

UI_SMALL static uint32_t lights_keys_mask(void)
{
    uint32_t k, m = 0;
    if (!lights_lvl || !lights_keys)
        return 0u;
    for (k = 0; k < 27u; k++) {
        uint32_t pc = (53u + k) % 12u;
        if (lights_keys == KEYS_ALL || (lights_keys == KEYS_C ? pc == 0u : ((0xAB5u >> pc) & 1u) != 0u))
            m |= 1u << k;
    }
    return m;
}

UI_SMALL static void ui_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0}, dl[FM1_NCOL] = {0}, bl[FM1_NCOL] = {0};
    uint32_t k, c, keys, guide, back;
    static uint8_t ready;
    if (!ready) {
        led_pos_init();
        ready = 1;
    }
    led_put(nl, panel.btn[ui.layer != LY_PLAY ? LAYER_BTN[ui.layer] : cur_btn()],
            ly_lock == LY_PLAY || ((fm1_ms / 300u) & 1u) != 0u || (fm1_in.buttons & ly_bit[ly_lock % LY_COUNT]) != 0u);
    led_put(nl, panel.btn[B_PLAY], play_led() || (song.playing && !song.rec && ft_on) ||
                                      (ci_on && ci_u % BEAT_U < BEAT_U / 4u));
    led_put(nl, panel.btn[B_REC], song.rec != 0u || ft_on || (rec_wait && ((fm1_ms / 125u) & 1u)) ||
                                     (ui.hold_kind == 1u && ((fm1_ms / 60u) & 1u)));
    if (ui.layer == LY_FX) {
        led_put(nl, panel.btn[B_OCTDN], ((thr_req | thr_ed) & 1u) != 0u);
        led_put(nl, panel.btn[B_OCTUP], ((thr_req | thr_ed) & 2u) != 0u);
    } else if (is_drum(TSEL)) {
        led_put(nl, panel.btn[B_OCTDN], (fm1_in.buttons & dyn_bit[0]) != 0u);
        led_put(nl, panel.btn[B_OCTUP], (fm1_in.buttons & dyn_bit[1]) != 0u);
    } else {
        led_put(nl, panel.btn[B_OCTDN], song.octave < 0);
        led_put(nl, panel.btn[B_OCTUP], song.octave > 0);
    }
    keys = keys_lit();
    guide = (keys_guide() | keys_notes_dim()) & ~keys;
    back = lights_keys_mask() & ~keys & ~guide;
    for (k = 0; k < 27u; k++) {
        led_put(nl, 14u + k, (int)((keys >> k) & 1u));
        led_put(dl, 14u + k, (int)((guide >> k) & 1u));
        led_put(bl, 14u + k, (int)((back >> k) & 1u));
    }
    if (lights_lvl)
        for (k = 0; k < NB; k++)
            led_put(bl, panel.btn[k], 1);
    for (c = 0; c < FM1_NCOL; c++) {
        fm1_led[c] = nl[c];
        fm1_led_dim[c] = dl[c];
        fm1_led_bg[c] = (uint8_t)(bl[c] & ~nl[c]);
    }
    fm1_led_bg_ns = LIGHTS_NS[lights_lvl % LIGHTS_N];
}
/* ---------------------------------------------------------- input --- */
#define ACC_GAP 60u
#define ACC_RATE 80u
UI_SMALL static int32_t accel(uint32_t role, int32_t s, int32_t range)
{
    uint32_t now = fm1_ms & 0xFFFFFFu, st = ui.enc_t[role], up = s > 0, pi = st >> 25, a, i, m = 1;
    if (knob_accel_off || range <= 16 || !s)
        return s;
    a = (uint32_t)(s < 0 ? -s : s);
    i = ((now - st) & 0xFFFFFFu) / a;
    if (!st || ((st >> 24) & 1u) != up || i >= ACC_GAP)
        i = 127u;
    else if (pi < ACC_GAP) {
        m = ACC_RATE / (i > pi ? i : pi ? pi : 1u);
        a = range > 100 ? 16u : range > 64 ? 8u : range > 32 ? 4u : 2u;
        m = m < 1u ? 1u : m > a ? a : m;
    }
    ui.enc_t[role] = now | up << 24 | (i ? i : 1u) << 25;
    return s * (int32_t)m;
}

/* AUDILUNE Main Layer Knobs (Play & Edit base layer):
 * KNOB 1: Global Tempo (BPM)
 * KNOB 2: Active Track DJ Filter (LP <- OFF -> HP)
 * KNOB 3: Active Track Volume (0% = MUTE)
 * KNOB 4: Global Metronome / Click Volume (0% = OFF) */
UI_SMALL static void audilune_main_knobs(uint32_t slot, int32_t steps)
{
    track_t *t = TSEL;
    switch (slot) {
    case 0: {
        song.g[G_BPM] = (int16_t)clamp(song.g[G_BPM] + accel(EN_K1, steps, 200), GP[G_BPM].min, GP[G_BPM].max);
        ui.bpm_t = 40;
        audilune_hud_popup("BPM", song.g[G_BPM], "");
        break;
    }
    case 1: {
        int32_t v = clamp((int32_t)t->p[P_FILT] + accel(EN_K2, steps, 100), -50, 50);
        t->p[P_FILT] = (int16_t)v;
        if (v == 0)
            audilune_hud_text("DJ FILTER", "OFF");
        else if (v < 0)
            audilune_hud_popup("LP FILTER", -v * 2, "%");
        else
            audilune_hud_popup("HP FILTER", v * 2, "%");
        break;
    }
    case 2: {
        if (t->p[P_MUTE])
            t->p[P_MUTE] = 0;
        if (is_drum(t)) {
            int32_t v = clamp((int32_t)song.g[G_DRLVL] + accel(EN_K3, steps, 100), 0, 100);
            song.g[G_DRLVL] = (int16_t)v;
            if (v == 0)
                audilune_hud_text("DRUM VOL", "MUTE");
            else
                audilune_hud_popup("DRUM VOL", v, "%");
        } else {
            int32_t v = clamp((int32_t)t->p[P_LEVEL] + accel(EN_K3, steps, 100), 0, 100);
            t->p[P_LEVEL] = (int16_t)v;
            ml_knob(t, P_LEVEL);
            if (v == 0)
                audilune_hud_text("TRK VOL", "MUTE");
            else
                audilune_hud_popup("TRK VOL", v, "%");
        }
        break;
    }
    default: {
        int32_t v = clamp((int32_t)song.g[G_CLICK] + accel(EN_K4, steps, 100), 0, 100);
        song.g[G_CLICK] = (int16_t)v;
        if (v == 0)
            audilune_hud_text("CLICK", "OFF");
        else
            audilune_hud_popup("CLICK", v, "%");
        break;
    }
    }
}

UI_SMALL static void tracks_edit(uint32_t slot, int32_t steps)
{
    audilune_main_knobs(slot, steps);
}

UI_SMALL static void note_mark(void)
{
    static uint32_t sess, at = 0xFFFFu;
    uint32_t here = ui.cursor | (uint32_t)song.sel << 8;
    if (here != at || !undo.valid || undo.undone || undo.sess != sess) {
        sess = (undo_sess += 4u) | 3u;
        at = here;
    }
    undo_mark(TSEL, sess);
}

UI_SMALL static int on_notes_page(void)
{
    return !ui.home && song.seq_mode && cur_page()->scope == SC_STEP && !is_drum(TSEL);
}

UI_SMALL static void note_sheet_knob(uint32_t slot, int32_t steps)
{
    track_t *t = TSEL;
    step_t *st = &t->step[ui.cursor];
    uint32_t row = ui.sheet - 1u;
    if (slot == 0u) {
        ui.sheet = (uint8_t)(1 + clamp((int32_t)row + steps, 0, 5));
        ui.sheet_last = (uint8_t)(ui.sheet - 1u);
        return;
    }
    if (slot != 1u)
        return;
    note_mark();
    switch (row) {
    case 0:
        st->flags = (uint8_t)(steps > 0 ? st->flags | SF_SLIDE : st->flags & ~SF_SLIDE);
        audilune_hud_text("SLIDE", (st->flags & SF_SLIDE) ? "ON" : "OFF");
        break;
    case 1:
        t->micro[ui.cursor] = (int8_t)clamp(t->micro[ui.cursor] + steps, MICRO_MIN, MICRO_MAX);
        audilune_hud_popup("NUDGE", t->micro[ui.cursor], "");
        break;
    case 2:
        step_fill_set(t, ui.cursor, (uint32_t)clamp((int32_t)step_fill(t, ui.cursor) % 3 + (steps > 0 ? 1 : -1), 0, 2));
        audilune_hud_popup("FILL COND", (int32_t)step_fill(t, ui.cursor), "");
        break;
    case 3: {
        int32_t n = steps;
        while (n) {
            int32_t to = note_shift(t, ui.cursor, n, 0), k = ui.note_k;
            if (to < 0) {
                ui_message(step_on(st) ? "NO ROOM" : "NO NOTE");
                break;
            }
            cursor_set(to);
            ui.note_k = (uint8_t)k;
            n += n > 0 ? -1 : 1;
        }
        break;
    }
    default:
        break;
    }
    sync_reload = 1;
}

UI_SMALL static void note_sheet_do(void)
{
    track_t *t = TSEL;
    uint32_t row = ui.sheet - 1u;
    note_mark();
    if (row == 0u) {
        t->step[ui.cursor].flags ^= SF_SLIDE;
        audilune_hud_text("SLIDE", (t->step[ui.cursor].flags & SF_SLIDE) ? "ON" : "OFF");
    } else if (row == 4u) {
        int32_t to = note_shift(t, ui.cursor, 1, 1), k = ui.note_k;
        if (to < 0) {
            ui_message(step_on(&t->step[ui.cursor]) ? "NO ROOM" : "NO NOTE");
            return;
        }
        cursor_set(to);
        ui.note_k = (uint8_t)k;
        ui.sheet = 4;
        ui_message("COPIED");
    } else if (row == 5u) {
        note_del(t, ui.cursor, ui.note_k);
        cursor_fix();
        ui.sheet = 0;
        ui_message("NOTE DELETED");
    }
    sync_reload = 1;
}

/* Edit Note Mode Knobs (with ENV / LFO modifiers per AUDILUNE Block 3 spec):
 * KNOB 1: Step Pitch Offset
 * KNOB 2: Step Velocity (or Envelope Decay if ENV held)
 * KNOB 3: Step Gate Length (or LFO Speed if LFO held)
 * KNOB 4: Step Micro-Timing Nudge */
UI_SMALL static void step_edit(uint32_t slot, int32_t steps)
{
    track_t *t = TSEL;
    step_t *st = &t->step[ui.cursor];
    uint32_t k = ui.note_k;
    int env_mod = ((fm1_in.buttons >> panel.btn[B_ENV]) & 1u) != 0u;
    int lfo_mod = ((fm1_in.buttons >> panel.btn[B_LFO]) & 1u) != 0u;

    if (is_drum(t))
        return;
    if (ui.sheet) {
        note_sheet_knob(slot, steps);
        return;
    }
    switch (slot) {
    case 0:
        note_mark();
        if (!step_on(st)) {
            memset(st, 0, sizeof *st);
            st->note = last_note;
            st->n = 1;
            st->time = ST_NOTE;
            ui.note_k = 0;
        } else {
            st->note[k] = (uint8_t)clamp(st->note[k] + steps, 1, 127);
            st->flags &= (uint8_t)~SF_CONT(k);
            last_note = st->note[k];
        }
        audilune_hud_popup("STEP NOTE", st->note[ui.note_k], "");
        break;
    case 1:
        if (env_mod) {
            int32_t v = clamp((int32_t)t->p[P_DEC] + accel(EN_K2, steps, 100), 0, 100);
            t->p[P_DEC] = (int16_t)v;
            audilune_hud_popup("ENV DECAY", v, "%");
        } else if (step_on(st)) {
            int32_t f = clamp((int32_t)note_force(st, k) + (steps > 0 ? 1 : -1), 0, 4);
            note_mark();
            note_force_set(st, k, (uint32_t)f);
            audilune_hud_popup("VELOCITY", (f + 1) * 20, "%");
        }
        break;
    case 2:
        if (lfo_mod) {
            int32_t v = clamp((int32_t)t->p[P_LRATE] + accel(EN_K3, steps, 100), 0, 100);
            t->p[P_LRATE] = (int16_t)v;
            audilune_hud_popup("LFO SPEED", v, "");
        } else if (step_on(st)) {
            uint32_t was = note_span(t, ui.cursor), n;
            note_mark();
            fm1_irq_off();
            n = note_span_set(t, ui.cursor, (uint32_t)clamp((int32_t)was + steps, 1, NSTEP));
            fm1_irq_on();
            audilune_hud_popup("GATE LEN", (int32_t)n, " ST");
            if (steps > 0 && n == was)
                ui_message(ui.cursor + n >= (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP) ? "END OF PATTERN"
                           : span_why == 2u ? "MONO: SET VOICE POLY" : span_why == 1u ? "STEP FULL: 4 NOTES" : "NOTE AHEAD");
        }
        break;
    default:
        note_mark();
        t->micro[ui.cursor] = (int8_t)clamp(t->micro[ui.cursor] + steps, MICRO_MIN, MICRO_MAX);
        audilune_hud_popup("NUDGE", t->micro[ui.cursor], "");
        break;
    }
}
UI_SMALL static void project_new(void)
{
    uint32_t i;
    transport_req = 2;
    panic_req = (1u << NTRK) - 1u;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        undo_mark(t, (undo_sess += 4u) | 3u);
        fm1_irq_off();
        if (i < NPART)
            sound_change(t);
        track_defaults(t);
        if (i < NPART) {
            set_engine_of(t, TRK_DEF[i]);
            apply_preset_to(t, TRK_DEF[i]);
        }
        fm1_irq_on();
    }
    TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;
    memset(drum_tune, 0, sizeof drum_tune);
    memset(drum_dec, 0, sizeof drum_dec);
    drum_mute = 0;
    for (i = 0; i < G_COUNT; i++)
        if (i != G_SLOT && i != G_DRCH && i != G_SYNC && i != G_MIDI && i != G_ROUTE && (i < G_CH1 || i > G_CHOTH) &&
            i != G_BEND && i != G_FXCH && i != G_DRMAP && i != G_RMCH && i != G_CLKOUT && i != G_FXFADE)
            song.g[i] = GP[i].def;
    song.solo = 0;
    song.octave = 0;
    sync_reload = 1;
    ui.force = 1;
}

UI_SMALL static void edit_param(uint32_t slot, int32_t steps)
{
    int16_t *vp;
    const page_t *pg = cur_page();
    const param_desc_t *d;
    uint32_t id = pg->id[slot];
    int32_t v;
    if (is_drum(TSEL) && !page_for_drum(pg))
        return;
    if (pg->scope == SC_STEP) {
        step_edit(slot, steps);
        return;
    }
    if (pg->scope == SC_TRK) {
        tracks_edit(slot, steps);
        return;
    }
    if (pg->graph == GR_BROWSE) {
        if (slot == 0u && !is_drum(TSEL)) {
            uint32_t total, cur = preset_pos(&total);
            if (total)
                preset_go((uint32_t)(((int32_t)cur + steps % (int32_t)total + (int32_t)total) % (int32_t)total));
        } else if (slot == 1u && !is_drum(TSEL)) {
            select_engine((TSEL->eng_req + (steps > 0 ? 1u : NENGINES - 1u)) % NENGINES);
        } else if (slot == 3u && !is_drum(TSEL)) {
            fav_toggle(steps > 0);
        }
        return;
    }
    if (pg->graph == GR_USER) {
        static const char *const UP_GO = {"LOAD", "ERASE", "SAVE"};
        if (slot == 0u) {
            ui.uslot = (uint8_t)clamp((int32_t)ui.uslot + steps, 0, UP_SLOTS - 1);
            ui.arm = 0;
            return;
        }
        if (steps <= 0)
            return;
        if (ui.arm != 0xE0u + slot) {
            ui.arm = (uint8_t)(0xE0u + slot);
            ui.arm_t = 90;
            ui_say("AGAIN: ", UP_GO[slot - 1u]);
            return;
        }
        ui.arm = 0;
        up_ui(slot - 1u, ui.uslot);
        return;
    }
    if (pg->graph == GR_SLOTS && slot == 1u) {
        if (steps <= 0)
            return;
        if (ui.arm != 0xE8u) {
            ui.arm = 0xE8u;
            ui.arm_t = 90;
            ui_say("AGAIN: ", "ERASE");
            return;
        }
        ui.arm = 0;
        project_erase((uint32_t)song.g[G_SLOT] - 1u);
        return;
    }
    d = page_desc(pg, slot, &vp);
    if (!d || !vp || d->max == d->min)
        return;
    v = d->names == ESYNC_NAMES || d->names == N_IBITS || d->names == N_IRATE ? names_turn(d, *vp, steps)
                                    : clamp(*vp + accel(EN_K1 + slot, steps, d->names ? 0 : d->max - d->min), d->min, d->max);
    *vp = (int16_t)v;
    if (d->label)
        audilune_hud_popup(d->label, v, "");
    if (pg->scope != SC_GLOBAL && p_lockable(id))
        ui.lock_par = (uint8_t)id;
    if (pg->scope != SC_GLOBAL)
        ml_knob(TSEL, id);
    if (!v)
        return;
    if (pg->scope == SC_GLOBAL && (id == G_LOAD || id == G_SAVE || id == G_CLRSEQ || id == G_INITSND || id == G_NEWPRJ) &&
        ui.arm != id) {
        *vp = 0;
        ui.arm = (uint8_t)id;
        ui.arm_t = 90;
        ui_say("AGAIN: ", d->label);
        return;
    }
    ui.arm = 0;
    if (pg->scope != SC_GLOBAL)
        return;
    switch (id) {
    case G_LOAD:
        *vp = 0;
        project_load((uint32_t)song.g[G_SLOT] - 1u);
        break;
    case G_SAVE:
        *vp = 0;
        project_save((uint32_t)song.g[G_SLOT] - 1u);
        break;
    case G_CLRSEQ:
        *vp = 0;
        undo_mark(TSEL, (undo_sess += 4u) | 3u);
        fm1_irq_off();
        track_defaults_steps(TSEL);
        fm1_irq_on();
        ui_message("PATTERN CLEARED");
        break;
    case G_INITSND:
        *vp = 0;
        set_engine(TSEL->eng_req);
        ui_message("SOUND INIT");
        ui.force = 1;
        break;
    case G_NEWPRJ:
        *vp = 0;
#if FELUCCA_ARRANGER
        if (song.playing && arrangement_enabled) {
            ui_message("STOP THE SONG FIRST");
            break;
        }
#endif
        project_new();
        ui_message("NEW LOOP, A-D KEPT");
        break;
    default:
        break;
    }
}

static uint32_t mi_held;
static uint8_t mi_ok;
UI_SMALL static uint32_t midi_held(void) { return mi_held | mi_held | mi_held | mi_held; }
UI_SMALL static void midi_entry(void)
{
    {
        static uint8_t mono_said;
        if (mono_hint && !mono_said) {
            mono_said = 1;
            ui_message("MONO: SET VOICE POLY");
        }
        mono_hint = 0;
    }
    track_t *t = TSEL;
    int on_page = mi_ok && song.seq_mode && cur_page()->scope == SC_STEP && !is_drum(t) && !song.rec && !rec_wait && !ft_on;
    int rec_arm = song.rec || rec_wait || ft_on;
    uint32_t rec_trk = ft_on ? ft_trk % NTRK : song.sel % NTRK;
    static uint8_t rh_st, rh_trk;
    if (!rec_arm)
        rh_st = 0;
    else if (!rh_st || rh_trk != rec_trk)
        rh_st = 1, rh_trk = (uint8_t)rec_trk;
    mi_ok = 0;
    if (step_in_lost) {
        step_in_lost = 0;
        step_in_r = step_in_w;
        mi_held = mi_held = mi_held = mi_held = 0;
    }
    while (step_in_r != step_in_w) {
        uint32_t ev = step_in_q[step_in_r % STEP_IN_Q], note = ev & 127u, tr = (ev >> 8) % NTRK;
        step_t *st = &t->step[ui.cursor];
        step_in_r++;
        if (!(ev & 0x80u)) {
            mi_held[note >> 5] &= ~(1u << (note & 31u));
            continue;
        }
        if (!on_page) {
            if (rh_st == 1u && tr != rec_trk) {
                char b = {(char)('1' + tr), 0};
                ui_say("MIDI PLAYS TRACK ", b);
                rh_st = 2;
            } else if (rec_arm && tr == rec_trk) {
                rh_st = 2;
            }
            continue;
        }
        if (&trk[tr] != t) {
            if (!song.playing) {
                char b = {(char)('1' + tr), 0};
                ui_say("MIDI PLAYS TRACK ", b);
            }
            continue;
        }
        mi_held[note >> 5] |= 1u << (note & 31u);
        if (!ui.entry_open) {
            ui.entry_open = 1;
            undo_mark(t, (undo_sess += 4u) | 3u);
            st->n = 0;
            st->lvl = st->rat = 0;
            st->flags &= (uint8_t)~(SF_CONT_ALL | SF_LEG);
            st->time = ST_NOTE;
        }
        if (t->p[P_VOICE]) {
            st->note = (uint8_t)note;
            st->n = 1;
        } else if (st->n < 4u) {
            st->note[st->n++] = (uint8_t)note;
        }
    }
    if (!on_page)
        mi_held = mi_held = mi_held = mi_held = 0;
}
UI_SMALL static void seq_entry(uint32_t pressed)
{
    track_t *t = TSEL;
    step_t *st = &t->step[ui.cursor];
    uint32_t k;
    mi_ok = 1;
    if (is_drum(t) || song.rec || rec_wait || ft_on)
        return;
    for (k = 0; k < 27u; k++) {
        uint32_t note;
        if (!((pressed >> k) & 1u))
            continue;
        note = kb_map(t, k);
        if (note == KB_SILENT)
            continue;
        if (!ui.entry_open) {
            ui.entry_open = 1;
            undo_mark(t, (undo_sess += 4u) | 3u);
            st->n = 0;
            st->lvl = st->rat = 0;
            st->flags &= (uint8_t)~(SF_CONT_ALL | SF_LEG);
            st->time = ST_NOTE;
        }
        if (t->p[P_CHORD] && !t->p[P_VOICE]) {
            uint8_t c;
            uint32_t m, j;
            if (kb_kind[k] == KS_NOTE && kb_n[k] && kb_root[k] == note) {
                m = kb_n[k] > 4u ? 4u : kb_n[k];
                memcpy(c, kb_nt[k], m);
            } else {
                m = chord_notes(t, note, c);
            }
            for (j = 0; j < m && st->n < 4u; j++)
                st->note[st->n++] = c[j];
        } else if (t->p[P_VOICE]) {
            st->note = (uint8_t)note;
            st->n = 1;
        } else if (st->n < 4u) {
            st->note[st->n++] = (uint8_t)note;
        }
        last_note = (uint8_t)note;
    }
    if (ui.entry_open && !fm1_in.notes && !midi_held()) {
        if (step_adv_off)
            ui.entry_open = 0;
        else
            cursor_set(ui.cursor + 1);
    }
}

enum { BT_NONE, BT_TAP, BT_HOLD };
UI_SMALL static uint32_t btn_hold_ms(uint32_t *t0, uint32_t label, uint32_t now, int hold_ok, uint32_t hold_ms)
{
    uint32_t tap;
    if ((fm1_in.buttons >> panel.btn[label]) & 1u) {
        if (!*t0)
            *t0 = (now | 1u) & ~2u;
        else if (hold_ok && !(*t0 & 2u) && now - (*t0 & ~3u) > hold_ms * 1000u * FM1_TICKS_PER_US) {
            *t0 |= 2u;
            return BT_HOLD;
        }
        return BT_NONE;
    }
    tap = *t0 && !(*t0 & 2u);
    *t0 = 0;
    return tap ? BT_TAP : BT_NONE;
}

UI_SMALL static void layer_tap(uint32_t layer)
{
    switch (layer) {
    case LY_FX:
        open_family(FAM_FX);
        ui_message("FX PAGE");
        break;
    case LY_ERASE:
        /* EDIT Button Tap: Toggles cleanly between Play Screen and Edit Screen */
        if (!ui.home && (cur_page()->scope == SC_STEP || cur_page()->scope == SC_DRUM)) {
            vis_on = 0;
            go_home();
            ui_message("PLAY SCREEN");
        } else {
            vis_on = 0;
            if (is_drum(TSEL)) {
                studio_open(SC_DRUM);
            } else {
                song.seq_mode = 1;
                open_family(FAM_SEQ);
            }
            ui_message("EDIT SCREEN");
        }
        break;
    case LY_STEP:
        if (on_drum_page()) {
            drum_page = (uint8_t)((drum_page + 1u) % 3u);
            ui.force = 1;
            break;
        }
        if (is_drum(TSEL)) {
            studio_open(SC_DRUM);
            break;
        }
        open_family(FAM_SEQ);
        break;
    case LY_ROLL:
        open_family(FAM_ARP);
        break;
    case LY_SCALE:
        open_family(FAM_SCL);
        break;
    case LY_MIX:
        open_family(FAM_GLO);
        break;
    case LY_SONG:
        if (on_song_page())
            arrangement_save();
        else if (!ui.home && cur_page()->scope == SC_TRK)
            studio_open(SC_SONG);
        else if (on_drum_page())
            studio_open(SC_SONG);
        else
            open_family(FAM_SAVE);
        break;
    default:
        break;
    }
}

static uint8_t home_eat;
UI_SMALL static void layer_unlock(void)
{
    if (ly_lock != LY_PLAY) {
        ly_lock = LY_PLAY;
        ui.force = 1;
    }
}

#define LY_QUIET_MS 250u
static uint32_t ly_quiet_t;
static uint8_t ly_quiet_sel;
UI_SMALL static int ly_quiet(void)
{
    if (ly_quiet_t && fm1_ms - (ly_quiet_t & ~1u) >= LY_QUIET_MS)
        ly_quiet_t = 0;
    return ly_quiet_t != 0;
}

UI_SMALL static int layers_input(uint32_t note_edges, uint32_t *pressed, uint32_t home)
{
    static uint8_t down[LY_COUNT], used[LY_COUNT];
    static uint32_t t0[LY_COUNT];
    uint32_t l, now = fm1_ms, held = LY_PLAY, eat = 0;
    if (ly_lock != LY_PLAY) {
        uint32_t keep = 1u << panel.btn[B_PLAY] | 1u << panel.btn[B_REC] | 1u << panel.btn[B_OCTDN] | 1u << panel.btn[B_OCTUP];
        eat = *pressed & ~keep;
        if (eat) {
            layer_unlock();
            if (eat & 1u << panel.btn[B_HOME])
                home_eat = 1;
            *pressed &= ~eat;
        }
    }
    for (l = LY_FX; l < LY_COUNT; l++) {
        uint32_t d = (fm1_in.buttons & ly_bit[l]) != 0u;
        if (d && !down[l]) {
            t0[l] = now;
            used[l] = (uint8_t)((eat & ly_bit[l]) != 0u);
        }
        if (d && note_edges)
            used[l] = 1;
        if (!d && down[l] && !used[l] && now - t0[l] < TAP_MS && !ui.menu && !ui.confirm)
            layer_tap(l);
        if (!d && down[l] && l == LY_SONG)
            chain_release();
        down[l] = (uint8_t)d;
        if (d && held == LY_PLAY)
            held = l;
    }
    if (held != LY_PLAY && home == BT_TAP && !home_eat) {
        ly_lock = (uint8_t)held;
        used[held] = 1;
        ui.layer = (uint8_t)held;
        ui.force = 1;
    }
    if (held == LY_PLAY && ly_lock != LY_PLAY) {
        held = ly_lock;
        used[held] = 1;
    }
    punch.hold = (uint8_t)(held == LY_FX);
    if (held != LY_PLAY && held != ui.layer && ui.layer != LY_PLAY)
        ui.layer = (uint8_t)held;
    if (held == LY_PLAY) {
        thr_req = 0;
        while (lk_r != lk_w) {
            uint32_t e = lk_q[lk_r % LKQ];
            lk_r++;
            if ((e >> 8) == KB_GRID) {
                if (((e >> 7) & 1u) && grid_keys_on())
                    grid_key(e & 31u);
            } else if (!((e >> 7) & 1u)) {
                layer_key(e >> 8, e & 31u, 0);
            }
        }
        if (ui.layer != LY_PLAY) {
            ly_quiet_t = fm1_ms | 1u;
            ly_quiet_sel = (uint8_t)(ui.layer == LY_FX);
            ui.layer = LY_PLAY;
            ui.step_held = 0;
            ui.force = 1;
        }
        ui.step_sess = 0;
        return 0;
    }
    ui.layer_used = used[held];
    while (lk_r != lk_w) {
        uint32_t e = lk_q[lk_r % LKQ];
        lk_r++;
        used[held] = 1;
        layer_key(e >> 8, e & 31u, (e >> 7) & 1u);
    }
    layer_knobs(held);
    if (ui.layer_used)
        used[held] = 1;
    if (ui.layer == LY_PLAY && (used[held] || now - t0[held] >= SHOW_MS)) {
        ui.layer = (uint8_t)held;
        if (held == LY_STEP && (uint32_t)ui.step_page * 16u >= trk_len(TSEL))
            ui.step_page = 0;
    }
    {
        uint32_t q = 0;
        if (held == LY_FX) {
            q = ((fm1_in.buttons >> panel.btn[B_OCTDN]) & 1u) | ((fm1_in.buttons >> panel.btn[B_OCTUP]) & 1u) << 1;
            if (q)
                used[held] = 1;
        }
        if (q != thr_req)
            ui.force = 1;
        thr_req = (uint8_t)q;
    }
    if (held == LY_ERASE) {
        uint32_t ob = 1u << panel.btn[B_OCTDN], pb = 1u << panel.btn[B_OCTUP];
        static uint32_t prev;
        uint32_t b = fm1_in.buttons & (ob | pb), press = b & ~prev;
        prev = b;
        if (press) {
            used[held] = 1;
            if (press & ob)
                ui_message(undo_swap(0) ? "UNDO" : "NOTHING TO UNDO");
            else
                ui_message(undo_swap(1) ? "REDO" : "NOTHING TO REDO");
        }
    }
    if (held == LY_STEP) {
        uint32_t ob = 1u << panel.btn[B_OCTDN], pb = 1u << panel.btn[B_OCTUP];
        static uint32_t prev;
        uint32_t b = fm1_in.buttons & (ob | pb), press = b & ~prev, pages = (trk_len(TSEL) + 15u) / 16u;
        prev = b;
        if (press) {
            used[held] = 1;
            if ((press & ob) && ui.step_held)
                steps_held_clear();
            else if ((press & pb) && ui.step_held)
                steps_held_fill();
            else
                ui.step_page = (uint8_t)((ui.step_page + ((press & pb) ? 1u : pages - 1u)) % pages);
        }
    }
    return 1;
}
UI_SMALL static void rec_knobs(void)
{
    static const uint8_t LENS = {16u, 32u, 64u};
    int32_t s;
    uint32_t empty = (uint32_t)project_empty(), tempo = !empty || rec_tempo;
    if ((s = panel_enc(EN_K1)) != 0 && empty) {
        rec_tempo = (uint8_t)(s > 0);
        settings_later = 1;
        ui.hot_col = 0, ui.hot_t = 40;
    }
    if ((s = panel_enc(EN_K2)) != 0 && tempo) {
        track_t *t = TSEL;
        int32_t len = t->p[P_SLEN], i, to = len;
        if (s > 0) {
            for (i = 0; i < 3; i++) if (LENS[i] > len) { to = LENS[i]; break; }
        } else {
            for (i = 2; i >= 0; i--) if (LENS[i] < len) { to = LENS[i]; break; }
        }
        t->p[P_SLEN] = (int16_t)to;
        ui.hot_col = 1, ui.hot_t = 40;
    }
    if ((s = panel_enc(EN_K3)) != 0 && tempo) {
        rec_count = (uint8_t)(s > 0);
        settings_later = 1;
        ui.hot_col = 2, ui.hot_t = 40;
    }
    if ((s = panel_enc(EN_K4)) != 0) {
        rec_quant_off = (uint8_t)(s < 0);
        settings_later = 1;
        ui.hot_col = 3, ui.hot_t = 40;
    }
}

/* AUDILUNE RECORD Button:
 * Tap: Arms/disarms live real-time parameter automation & note recording.
 * Hold (1.5s): Context-aware erase (clears active track on Play Screen, or wipes highlighted step on Edit Screen). */
#define AUDILUNE_HOLD_CLEAR_MS 1500u

UI_SMALL static void holds_input(uint32_t pressed, uint32_t now_ms)
{
    static uint8_t prev_rec, prev_wait, eaten, rec_on;
    static uint32_t rec_t0;
    uint32_t rb = 1u << panel.btn[B_REC];
    uint32_t rec_down = (fm1_in.buttons & rb) != 0u;
    if (on_song_page())
        rec_down = 0, pressed &= ~rb;

    if (pressed & rb) {
        eaten = (uint8_t)ft_owns_press();
        prev_rec = song.rec;
        prev_wait = rec_wait;
        rec_t0 = now_ms;
        rec_on = 1;
        rec_toggle();
        if (song.rec || rec_wait)
            ui_message("AUTOMATION ARMED");
        else
            ui_message("AUTOMATION OFF");
    }
    if (rec_down && rec_on && !eaten && !ui.hold_kind && now_ms - rec_t0 >= 650u) {
        song.rec = prev_rec;
        rec_wait = prev_wait;
        ui.msg_t = 0;
        ui.hold_kind = 1;
        ui.hold_t0 = now_ms;
        ui.hold_trk = song.sel;
        ui.force = 1;
    }
    if (ui.hold_kind == 1u && rec_down && now_ms - ui.hold_t0 >= AUDILUNE_HOLD_CLEAR_MS) {
        track_t *t = &trk[ui.hold_trk % NTRK];
        undo_mark(t, (undo_sess += 4u) | 3u);
        fm1_irq_off();
        if (!ui.home && cur_page()->scope == SC_STEP && !is_drum(t)) {
            memset(&t->step[ui.cursor], 0, sizeof(step_t));
            t->micro[ui.cursor] = 0;
            fm1_irq_on();
            ui_message("STEP CLEARED");
        } else {
            char b = "1 CLEARED";
            track_defaults_steps(t);
            t->nheld = 0;
            t->arp_phys = 0;
            fm1_irq_on();
            b = (char)('1' + ui.hold_trk);
            ui_say("TRACK ", b);
        }
        ui.hold_kind = 0;
        rec_on = 0;
        ui.force = 1;
    }
    if (!rec_down) {
        if (ui.hold_kind == 1u) {
            ui.hold_kind = 0;
            ui.force = 1;
        }
        rec_on = 0;
    }
}

UI_SMALL static void remote_poll(void)
{
    static int16_t ch_was = -1;
    static uint8_t usb_was;
    uint32_t l, m = 0;
    int drop = !song.g[G_RMCH] || (ch_was > 0 && song.g[G_RMCH] != ch_was) || (usb_was && !usb.config);
    ch_was = song.g[G_RMCH];
    usb_was = (uint8_t)(usb.config != 0);
    if (drop) {
        fm1_irq_off();
        remote_lbl = remote_up = 0;
        fm1_irq_on();
        fm1_remote_btn = 0;
        return;
    }
    for (l = 0; l < NB; l++) {
        uint32_t b = 1u << panel.btn[l], lb = 1u << l;
        if ((remote_up & lb) && (fm1_in.buttons & b)) {
            fm1_irq_off();
            remote_lbl &= ~lb;
            remote_up &= ~lb;
            fm1_irq_on();
        }
        if (remote_lbl & lb)
            m |= b;
    }
    fm1_remote_btn = m;
}

UI_SMALL static void ui_input(void)
{
    uint32_t pressed = fm1_input_edges(0), notes = fm1_input_note_edges(), now = fm1_ticks(), id, b, k;
    /* AUDILUNE Block 3: HOME Hold (3000 ms) triggers Infinite Algorithmic Compositions Engine */
    uint32_t home = btn_hold_ms(&ui.home_t0, B_HOME, now, 1, 3000u);
    int32_t s;
    int layered;
    enc_hold = 0;
    if (pressed || notes)
        ui_input_ms = fm1_ms;
    remote_poll();
    pc_poll();
    midi_chan_fix();
    fm6_poll();
    ml_poll();
    page_fix();
    midi_entry();

    if (home == BT_HOLD) {
        if (ui.menu) {
            menu_close();
        } else if ((fm1_in.buttons >> panel.btn[B_GLO]) & 1u) {
            /* HOME + GLO hold still opens hardware system calibration / settings menu */
            layer_unlock();
            ui.menu = 1;
            ui.menu_sel = 0;
            ui.confirm = 0;
            ui.force = 1;
            song.seq_mode = 0;
        } else {
            audilune_infinite_on = (uint8_t)!audilune_infinite_on;
            ui.force = 1;
            ui_message(audilune_infinite_on ? "INFINITE GEN: ON" : "INFINITE GEN: OFF");
        }
    }

    kb_grid = (uint8_t)grid_keys_on();
    if (ui.menu) {
        punch.hold = 0;
        thr_req = 0;
        if (!ui.home_t0)
            menu_input(pressed);
        return;
    }
    if (!on_notes_page() && !on_drum_page())
        ui.sheet = 0;
    layered = layers_input(notes, &pressed, home);
    if (home_eat && !((fm1_in.buttons >> panel.btn[B_HOME]) & 1u)) {
        if (home == BT_TAP)
            home = BT_NONE;
        home_eat = 0;
    }
    pressed &= ~(ly_bit[LY_FX] | ly_bit[LY_ERASE] | ly_bit[LY_ROLL] | ly_bit[LY_STEP] | ly_bit[LY_SCALE] | ly_bit[LY_MIX] |
                 ly_bit[LY_SONG]);
    holds_input(pressed, fm1_ms);
    pressed &= ~((on_song_page() ? 0u : 1u << panel.btn[B_REC]) | (1u << panel.btn[B_SAVE]));

    if (layered || ui.hold_kind) {
        /* SELECT + GLO button cycles the 8 AUDILUNE Color Themes */
        if (((fm1_in.buttons >> panel.btn[B_GLO]) & 1u) && (s = panel_enc(EN_SELECT)) != 0) {
            audilune_theme_idx = (uint8_t)((audilune_theme_idx + (s > 0 ? 1u : AUDILUNE_THEME_COUNT - 1u)) % AUDILUNE_THEME_COUNT);
            audilune_hud_text("THEME", AUDILUNE_THEME_NAMES[audilune_theme_idx]);
            ui.force = 1;
        } else if (!punch.hold && (s = panel_enc(EN_SELECT)) != 0) {
            /* Live Pitch Bend when SELECT + PIT key (C#3 / key 1) is held */
            if (fm1_in.notes & (1u << 1)) {
                int32_t bend = clamp((int32_t)song.octave + (s > 0 ? 1 : -1), -3, 3);
                song.octave = (int8_t)bend;
                audilune_hud_popup("PITCH BEND", bend, " OCT");
            } else {
                uint32_t next_trk = (uint32_t)((song.sel + (s > 0 ? 1 : NTRK - 1)) % NTRK);
                track_select(next_trk);
                audilune_hud_popup("TRACK", (int32_t)(next_trk + 1u), "");
            }
        }
        panel_enc(EN_ALGO);
        panel_enc(EN_PRESET);
        if (ui.hold_kind)
            for (k = 0; k < 4u; k++)
                panel_enc(EN_K1 + k);
        pressed &= ~((1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]));
        if (!(pressed & (1u << panel.btn[B_PLAY])))
            return;
        pressed &= 1u << panel.btn[B_PLAY];
        enc_hold = (1u << NE) - 1u;
    } else if (ly_quiet()) {
        for (k = 0; k < 4u; k++)
            panel_enc(EN_K1 + k);
        enc_hold |= 15u << EN_K1;
        if (ly_quiet_sel) {
            panel_enc(EN_SELECT);
            enc_hold |= 1u << EN_SELECT;
        }
    }
    if (rec_wait && !ft_on && !ci_on) {
        rec_knobs();
        enc_hold |= 15u << EN_K1;
    } else if (rec_wait || ft_on) {
        for (k = 0; k < 4u; k++)
            panel_enc(EN_K1 + k);
        enc_hold |= 15u << EN_K1;
    }
#if FELUCCA_ARRANGER
    if (on_song_page()) {
        song_screen_input(pressed, home);
        return;
    }
    if (on_drum_page()) {
        drum_screen_input(pressed, home, notes);
        return;
    }
#endif
    if (home == BT_TAP) {
        if (!vis_on) {
            vis_open();
            ui_message("VISUALIZER");
        } else {
            vis_on = 0;
            go_home();
            ui_message("PLAY SCREEN");
        }
    }
    cursor_fix();
    if (on_notes_page()) {
        uint32_t ev, ob = 1u << panel.btn[B_OCTDN];
        pressed = octp_filter(pressed, &ev);
        if (ev == 2u && !ui.sheet)
            ui.sheet = (uint8_t)(1u + ui.sheet_last);
        else if (ev == 1u && ui.sheet)
            note_sheet_do();
        else if (ev == 1u)
            pressed |= 1u << panel.btn[B_OCTUP];
        if (ui.sheet && (pressed & ob)) {
            ui.sheet = 0;
            pressed &= ~ob;
        }
        if (ev)
            ui.force = 1;
    }
    for (id = 0; id < 14u; id++) {
        if (!((pressed >> id) & 1u))
            continue;
        b = panel_btn_of(id);
        switch (b) {
        case B_PLAY:
#if FELUCCA_ARRANGER
            if (!song.playing && arrangement_enabled && !arr_valid(&arrangement, arrangement_ready())) {
                ui_message("EMPTY SECTION: REC");
                break;
            }
#endif
            if (!ft_owns_press())
                transport_req = song.playing ? 2 : 1;
            break;
        case B_OCTDN:
        case B_OCTUP: {
            uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
            /* AUDILUNE Block 3: OCT- / OCT+ navigates steps horizontally on Edit Screen, shifts octave on Play Screen */
            if (!ui.home && cur_page()->scope == SC_STEP) {
                int32_t len = (int32_t)trk_len(TSEL);
                int32_t dir = (b == B_OCTUP) ? 1 : -1;
                int32_t next_step = ((int32_t)ui.cursor + dir + len) % len;
                cursor_set(next_step);
                audilune_hud_popup("STEP", next_step + 1, "");
                break;
            }
            if (is_drum(TSEL))
                break;
            if ((fm1_in.buttons & both) == both) {
                song.octave = 0;
                audilune_hud_popup("OCTAVE", 0, "");
            } else {
                song.octave += b == B_OCTDN ? (song.octave > -3 ? -1 : 0) : (song.octave < 3 ? 1 : 0);
                audilune_hud_popup("OCTAVE", song.octave, "");
            }
            break;
        }
        case B_ENV:
            open_family(FAM_ENV);
            break;
        case B_LFO:
            open_family(FAM_LFO);
            break;
        default:
            break;
        }
    }
    if (song.seq_mode && cur_page()->scope == SC_STEP && !ui.sheet)
        seq_entry(notes);

    /* AUDILUNE Block 3: PRESETS Encoder cycles sequential preset wheel (Perc -> Bass -> Melody -> Drones -> User -> Poly) */
    if ((s = panel_enc(EN_PRESET)) != 0) {
        if (is_drum(TSEL)) {
            TDRUM->p[P_E0] = (int16_t)clamp(TDRUM->p[P_E0] + s, 0, DRUM_KITS - 1);
            audilune_hud_popup("DRUM KIT", TDRUM->p[P_E0] + 1, "");
        } else {
            uint32_t total, cur = preset_pos(&total);
            if (total) {
                uint32_t next_p = (uint32_t)(((int32_t)cur + s % (int32_t)total + (int32_t)total) % (int32_t)total);
                preset_go(next_p);
                audilune_hud_popup("PRESET", (int32_t)(next_p + 1u), "");
            }
        }
    }

    /* AUDILUNE Block 3: ALGORITHM Encoder sweeps smoothly across the 6 Macro-Genre frameworks */
    if ((s = panel_enc(EN_ALGO)) != 0 && !ft_on) {
        audilune_genre_idx = (uint8_t)((audilune_genre_idx + (s > 0 ? 1u : AUDILUNE_GENRE_COUNT - 1u)) % AUDILUNE_GENRE_COUNT);
        audilune_hud_text("GENRE", AUDILUNE_GENRE_NAMES[audilune_genre_idx]);
        ui.force = 1;
    }

    /* AUDILUNE Block 3: SELECT Encoder cycles 4 Visualizer styles on Visualizer Screen, or Tracks 1-6 on Play/Edit */
    if (vis_shown() && !rec_wait && !ft_on) {
        if ((s = panel_enc(EN_SELECT)) != 0)
            vis_select(s);
        for (k = 0; k < 4u; k++)
            panel_enc(EN_K1 + k);
        return;
    }
    if ((s = panel_enc(EN_SELECT)) != 0 && !rec_wait && !ft_on) {
        uint32_t next_trk = (uint32_t)((song.sel + (s > 0 ? 1 : NTRK - 1)) % NTRK);
        track_select(next_trk);
        audilune_hud_popup("TRACK", (int32_t)(next_trk + 1u), "");
    }

    /* Knobs 1-4: Main Layer Knobs on Play Screen (ui.home), Context/Edit Knobs on Edit & Parameter Pages */
    for (k = 0; k < 4u; k++) {
        const page_t *pg = cur_page();
        int16_t *hv;
        if ((s = panel_enc(EN_K1 + k)) == 0)
            continue;
        if (ui.home || pg->scope == SC_STEP || pg->scope == SC_TRK || page_desc(pg, k, &hv) ||
            (pg->graph == GR_USER && k == 0u)) {
            ui.hot_col = (uint8_t)k;
            ui.hot_t = 40;
        }
        if (ui.home) {
            audilune_main_knobs(k, s);
        } else {
            edit_param(k, s);
        }
    }
}

/* ---------------------------------------------------- panel setup --- */
#define SETUP_IDLE_MS 30000u
UI_SMALL static void panel_setup(void)
{
    uint32_t i, used = 0, t0 = fm1_ms;
    const panel_t old = panel;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 10, 240, &FONT_S, "HARDWARE CALIBRATION", C_WHITE, 1);
    draw_text_box(0, 30, 240, &FONT_S, "TEACH EACH BUTTON AND KNOB", C_GRAY, 1);
    while (fm1_in.buttons) {
        fm1_wdt_feed();
        if (fm1_ms - t0 > SETUP_IDLE_MS)
            goto timeout;
    }
    fm1_input_edges(0);
    for (i = 0; i < NB; i++) {
        uint32_t p = 0, id;
        draw_text_box(0, 80, 240, &FONT_S, "PRESS", C_GRAY, 1);
        draw_text_box(0, 100, 240, &FONT_L, B_NAME[i], C_WHITE, 1);
        t0 = fm1_ms;
        while (!(p & ~used)) {
            fm1_wdt_feed();
            p |= fm1_input_edges(0);
            if (fm1_ms - t0 > SETUP_IDLE_MS)
                goto timeout;
        }
        for (id = 0; id < 14u; id++)
            if (((p & ~used) >> id) & 1u)
                break;
        panel.btn[i] = (uint8_t)id;
        used |= 1u << id;
    }
    used = 0;
    for (i = 0; i < NE; i++) {
        uint32_t e;
        int32_t st = 0;
        draw_text_box(0, 80, 240, &FONT_S, "TURN RIGHT", C_GRAY, 1);
        draw_text_box(0, 100, 240, &FONT_L, E_NAME[i], C_WHITE, 1);
        for (e = 0; e < 7u; e++)
            fm1_enc_take(e);
        t0 = fm1_ms;
        for (;;) {
            fm1_wdt_feed();
            if (fm1_ms - t0 > SETUP_IDLE_MS)
                goto timeout;
            for (e = 0; e < 7u; e++)
                if (!((used >> e) & 1u) && (st = fm1_enc_take(e)) != 0)
                    break;
            if (e < 7u)
                break;
        }
        panel.enc[i] = (uint8_t)e;
        panel.dir[i] = (int8_t)(st > 0 ? 1 : -1);
        used |= 1u << e;
        fm1_delay_ms(300);
        fm1_enc_take(e);
    }
    panel.magic = PANEL_MAGIC;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    layers_init();
    ui.force = 1;
    return;
timeout:
    panel = old;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.force = 1;
    ui_message("SETUP CANCELLED");
}
