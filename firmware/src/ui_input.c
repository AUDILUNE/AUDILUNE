/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Adapted for AUDILUNE Firmware (Block 3: hardware_controls) */

/* ------------------------------------------------ AUDILUNE Helpers --- */
#define AUDILUNE_GENRE_COUNT 6
#define AUDILUNE_THEME_COUNT 8

static const char *const AUDILUNE_THEME_NAMES[AUDILUNE_THEME_COUNT] = {
    "TEAL", "ORANGE", "BURGY", "PURPLE", "PINK", "BLUE", "MINT", "GREY"
};

static uint8_t audilune_genre_idx = 0;
static uint8_t audilune_theme_idx = 0;
static int8_t audilune_drum_octave = 0;
static uint8_t audilune_save_latch = 0, audilune_pit_latch = 0;
static uint8_t rec_hud_pick, rec_hud_mode, rec_hud_usr;

/* Bare-metal HUD popup formatter (no stdio.h needed) for 1.5s status bar linger */
UI_SMALL static void audilune_hud_popup(const char *label, int32_t val, const char *unit)

{

    char buf[30 + 6], num[10 + 4];

    uint32_t p = 0, n = 0;

    int32_t v = val;
    while (label && label[p] && p < 18u) {

        buf[p] = label[p];
        p++;
    }
    buf[p++] = ':';
    buf[p++] = ' ';
    if (v < 0) {
        buf[p++] = '-';
        v = -v;
    }
    if (v == 0) {
        num[n++] = '0';
    } else {
        while (v > 0 && n < 12u) {
            num[n++] = (char)('0' + (v % 10));
            v /= 10;
        }
    }
    while (n > 0)
        buf[p++] = num[--n];
    while (unit && *unit && p < 34u)
        buf[p++] = *unit++;
    buf[p] = 0;
    ui_message(buf);
}

UI_SMALL static void audilune_hud_text(const char *prefix, const char *value)
{
    char buf[36];
    uint32_t p = 0;
    while (prefix && *prefix && p < 16u)
        buf[p++] = *prefix++;
    buf[p++] = ':';
    buf[p++] = ' ';
    while (value && *value && p < 35u)
        buf[p++] = *value++;
    buf[p] = 0;
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
    led_put(nl, panel.btn[B_PLAY], play_led() || (song.playing && !song.rec && ft_on) || (ci_on && ci_u % BEAT_U < BEAT_U / 4u));
    led_put(nl, panel.btn[B_REC], song.rec != 0u || ft_on || (rec_wait && ((fm1_ms / 125u) & 1u)) || (ui.hold_kind == 1u && ((fm1_ms / 60u) & 1u)));
    led_put(nl, panel.btn[B_SAVE], audilune_save_latch == 1u || ((audilune_save_latch == 2u || ui.hold_kind == 2u) && ((fm1_ms / 125u) & 1u)));
    if (ui.layer == LY_FX) {
        led_put(nl, panel.btn[B_OCTDN], ((thr_req | thr_ed) & 1u) != 0u);
        led_put(nl, panel.btn[B_OCTUP], ((thr_req | thr_ed) & 2u) != 0u);
    } else if (is_drum(TSEL) || !TSEL->synth_mode) {
        led_put(nl, panel.btn[B_OCTDN], audilune_drum_octave < 0);
        led_put(nl, panel.btn[B_OCTUP], audilune_drum_octave > 0);
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
#define ACC_SLOW_MS 120u
#define ACC_FAST_MS 45u
UI_SMALL static int32_t accel(uint32_t role, int32_t s, int32_t range)
{
    uint32_t now = fm1_ms & 0xFFFFFFu, st = ui.enc_t[role], up = s > 0, prev_m = (st >> 25) & 7u, a, dt, m = 1u;
    if (knob_accel_off || range <= 16 || !s)
        return s;
    a = (uint32_t)(s < 0 ? -s : s);
    dt = st ? (((now - st) & 0xFFFFFFu) / a) : ACC_SLOW_MS;
    if (!st || ((st >> 24) & 1u) != up || dt >= ACC_SLOW_MS) {
        m = 1u;
    } else if (dt < ACC_FAST_MS) {
        uint32_t target = dt < 16u ? 6u : dt < 28u ? 4u : 2u;
        uint32_t cap = range > 80 ? 6u : range > 40 ? 4u : 2u;
        if (target > cap)
            target = cap;
        m = prev_m < target ? (prev_m == 1u ? 2u : prev_m == 2u ? 4u : 6u) : target;
    } else {
        m = prev_m > 2u ? 2u : 1u;
    }
    ui.enc_t[role] = now | (up << 24) | ((m & 7u) << 25);
    return s * (int32_t)m;
}

/* AUDILUNE v2.3 Main Layer Knobs (Live Play Page):
 * Knob 1: Global Tempo (BPM)
 * Knob 2: Metronome Volume (0% mute to 100% full)
 * Knob 3: Active Track Volume (0% mute to 100% full)
 * Knob 4: Pattern A-F navigation */
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
        int32_t v = clamp((int32_t)song.g[G_CLICK] + accel(EN_K2, steps, 100), 0, 100);
        song.g[G_CLICK] = (int16_t)v;
        if (v == 0)
            audilune_hud_text("METRONOME", "MUTE");
        else
            audilune_hud_popup("METRONOME", v, "%");
        break;
    }
    case 2: {
        if (t->p[P_MUTE])
            t->p[P_MUTE] = 0;
        if (is_drum(t)) {
            int32_t v = clamp((int32_t)song.g[G_DRLVL] + accel(EN_K3, steps, 100), 0, 100);
            song.g[G_DRLVL] = (int16_t)v;
            if (v == 0)
                audilune_hud_text("TRK VOL", "MUTE");
            else
                audilune_hud_popup("TRK VOL", v, "%");
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
        char pat[2] = {(char)('A' + ((song.g[G_SLOT] - 1 + (steps > 0 ? 1 : 5)) % 6)), 0};
        song.g[G_SLOT] = (int16_t)(((song.g[G_SLOT] - 1 + (steps > 0 ? 1 : 5)) % 6) + 1);
        audilune_hud_text("PATTERN", pat);
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
    (void)0;
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
            st->note[0] = last_note;
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
            set_engine_of(t, TRK_DEF[i][0]);
            apply_preset_to(t, TRK_DEF[i][1]);
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
        static const char *const UP_GO[3] = {"LOAD", "ERASE", "SAVE"};
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

static uint32_t mi_held[4];
static uint8_t mi_ok;
UI_SMALL static uint32_t midi_held(void) { return mi_held[0] | mi_held[1] | mi_held[2] | mi_held[3]; }
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
        mi_held[0] = mi_held[1] = mi_held[2] = mi_held[3] = 0;
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
                char b[2] = {(char)('1' + tr), 0};
                ui_say("MIDI PLAYS TRACK ", b);
                rh_st = 2;
            } else if (rec_arm && tr == rec_trk) {
                rh_st = 2;
            }
            continue;
        }
        if (&trk[tr] != t) {
            if (!song.playing) {
                char b[2] = {(char)('1' + tr), 0};
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
            st->note[0] = (uint8_t)note;
            st->n = 1;
        } else if (st->n < 4u) {
            st->note[st->n++] = (uint8_t)note;
        }
    }
    if (!on_page)
        mi_held[0] = mi_held[1] = mi_held[2] = mi_held[3] = 0;
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
            uint8_t c[4];
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
            st->note[0] = (uint8_t)note;
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

UI_SMALL static void audilune_toggle_scr(uint8_t target, uint32_t fam, const char *label)
{
    vis_on = 0;
    if (ui.scr == target && !ui.home) {
        go_home();
        ui_message("LIVE PLAY");
        return;
    }
    ui.scr = target;
    ui.list_sel = 0;
    ui.list_top = 0;
    open_family(fam);
    ui_message(label);
}

UI_SMALL static void layer_tap(uint32_t layer)
{
    switch (layer) {
    case LY_FX:
        vis_on = 0;
        if (ui.scr == UI_FX && !ui.home && !al_pad_list_mode) {
            if (ui.list_sel < 8u) al_pad_idx = ui.list_sel;
            al_pad_list_mode = 1u;
            ui.list_sel = 0;
            ui_message(*(AL_PAD_NAMES + (al_pad_idx & 7u)));
        } else if (ui.scr == UI_FX && !ui.home && al_pad_list_mode) {
            al_pad_list_mode = 0u;
            go_home();
            ui_message("LIVE PLAY");
        } else {
            al_pad_list_mode = 0u;
            ui.scr = UI_FX;
            ui.list_sel = 0;
            ui.list_top = 0;
            open_family(FAM_FX);
            ui_message("EFFECTS");
        }
        ui.force = 1;
        break;
    case LY_ERASE: {
        vis_on = 0;
        ui.home = 0;
        ui.list_sel = 0;
        ui.list_top = 0;
        if (ui.scr == UI_EDIT) {
            ui.scr = UI_CHOP_SRC;
            ui_message("CHOP");
        } else if (ui.scr == UI_CHOP_SRC) {
            ui.scr = UI_CHOP_WAVE;
            ui_message("SAMPLER");
        } else {
            ui.scr = UI_EDIT;
            open_family(FAM_SEQ);
            ui_message("EDIT");
        }
        ui.force = 1;
        break;
    }
    case LY_STEP:
        vis_on = 0;
        if (ui.scr == UI_SEQ && !ui.home && !al_seq_ovw) {
            al_seq_ovw = 1u;
            al_seq_ovw_trk = ui.list_sel < NTRK ? ui.list_sel : song.sel;
            al_seq_ovw_step = 0u;

            ui_message("128-STEP VIEW");
        } else if (ui.scr == UI_SEQ && !ui.home && al_seq_ovw) {
            al_seq_ovw = 0u;

            go_home();
            ui_message("LIVE PLAY");
        } else {
            al_seq_ovw = 0u;
            ui.scr = UI_SEQ;
            ui.list_sel = 0;
            ui.list_top = 0;
            open_family(FAM_SEQ);
            ui_message("SEQ PAGE");
        }
        ui.force = 1;
        break;
        break;
    case LY_ROLL:
        audilune_toggle_scr(UI_ARP, FAM_ARP, "ARP PAGE");
        break;
    case LY_SCALE:
        if (rec_hud_pick) {
            rec_hud_pick = 0;
            if (rec_hud_mode == 2u || rec_hud_mode == 3u) {
                rec_wait = 0u;
                al_smp_cap_arm(rec_hud_usr, rec_hud_mode == 3u);
                ui_message(rec_hud_mode == 3u ? "USB CAPTURE ARMED" : "SAMPLE CAP ARMED");
            } else {
                al_smp_cap_stop();
                ui_message("REC TARGET SET");
            }
            ui.force = 1;
            break;
        }
        if (ui.scr == UI_ALGO) {
            extern uint8_t audilune_vox_genre[];
            uint8_t row = ui.list_sel;
            if (row < AUDILUNE_GENRE_COUNT) {
                TSEL->synth_mode = 1;
                ui.algo_genre[song.sel] = row;
                audilune_vox_genre[song.sel % NTRK] = row;
                audilune_genre_idx = row;
                audilune_hud_text("GENRE", AUDILUNE_GENRE_NAMES[row]);
            } else if (row < AUDILUNE_GENRE_COUNT + DRUM_KITS) {
                uint8_t kidx = (uint8_t)(row - AUDILUNE_GENRE_COUNT);
                TSEL->synth_mode = 0;
                TDRUM->p[P_E0] = (int16_t)kidx;
                audilune_hud_text("DRUM KIT", DRUM_KIT_NAMES[kidx]);
            }
            go_home();
        } else if (ui.scr == UI_SETUP) {
            if (ui.list_sel == 0u) {

                go_home();
                if (ui.session_mode > 0u) {
                    audilune_infinite_on = 1u;
                    if (!song.playing)
                        transport_req = 1;
                    ui_message("GENERATIVE LIVE");
                } else {
                    ui_message("STUDIO LIVE");
                }
            }
        } else if (ui.scr == UI_SEQ) {
            if (al_seq_ovw) {
                track_t *ot = &trk[al_seq_ovw_trk < NTRK ? al_seq_ovw_trk : song.sel];
                uint32_t st = al_seq_ovw_step & 127u;
                undo_mark(ot, (undo_sess += 4u) | 3u);
                fm1_irq_off();
                if (is_drum(ot)) {
                    if (dstep_has(&ot->dstep[st], pen_lane)) dstep_clr(&ot->dstep[st], pen_lane);
                    else { dstep_set(&ot->dstep[st], pen_lane, LV_NORM, 0); ot->seq_active = 1; }
                } else if (step_on(&ot->step[st])) {
                    memset(&ot->step[st], 0, sizeof ot->step[st]);
                } else {
                    memset(&ot->step[st], 0, sizeof ot->step[st]);
                    ot->step[st].note[0] = last_note ? last_note : 60u;
                    ot->step[st].n = 1; ot->step[st].vel = 100; ot->step[st].time = ST_NOTE; ot->seq_active = 1;
                }
                fm1_irq_on();
                sync_reload = 1;
                audilune_hud_popup("STEP", (int32_t)st + 1, "");
            } else if (ui.list_sel >= 7u) {
                ui.seq_grab = 0;
                ui.scr = UI_CHOP_SRC;
                ui.list_sel = 0;
                ui.list_top = 0;
                ui_message("CHOP SHOP");
            } else if (ui.list_sel < 6u) {
                al_seq_ovw = 1u;
                al_seq_ovw_trk = ui.list_sel;
                al_seq_ovw_step = 0u;
                ui_message("128-STEP VIEW");
            } else {
                ui.seq_grab = (uint8_t)(!ui.seq_grab);
                ui_message(ui.seq_grab ? "GRABBED: K4 SHIFT" : "DROPPED");
            }
         } else if (ui.scr == UI_FX) {
            if (!al_pad_list_mode) {
                if (ui.list_sel < 8u) {
                    al_pad_idx = ui.list_sel;
                    al_pad_list_mode = 1u;
                    ui_message(*(AL_PAD_NAMES + (al_pad_idx & 7u)));
                } else {
                    audilune_list_knob(3u, 1);
                }
            } else {
                uint8_t p = (uint8_t)(al_pad_idx & 7u);
                uint8_t bit = (uint8_t)(1u << p);
                if (p == 2u) {
                    al_vec_sel = (uint8_t)((al_vec_sel + 1u) & 3u);
                    *(al_vec_corner + al_vec_sel) = (uint8_t)((*(al_vec_corner + al_vec_sel) + 1u) & 3u);
                }

                al_pad_latch ^= bit;
                audilune_chaos_apply(TSEL);
                ui_message((al_pad_latch & bit) ? "PAD: ON/LATCH" : "PAD: OFF");
            }
        } else if (ui.scr == UI_LANDSCAPE) {
            uint16_t bit = (uint16_t)(1u << (ui.list_sel % 10u));
            al_land_on ^= bit;
            audilune_landscape_apply();
            ui_message((al_land_on & bit) ? "LAYER: ON" : "LAYER: MUTE");
        } else if (ui.scr == UI_EDIT) {
            if (ui.list_sel == 0u) {
                ui_message(undo_swap(ui.edit_redo_dir) ? (ui.edit_redo_dir ? "REDO" : "UNDO") : "NOTHING TO UNDO");
            } else if (ui.list_sel == 1u) {
                undo_mark(TSEL, (undo_sess += 4u) | 3u);
                fm1_irq_off();
                track_defaults_steps(TSEL);
                fm1_irq_on();
                ui_message("TRACK CLEARED");
            }
        } else if (ui.scr == UI_ARP) {
            if (ui.list_sel == 0u) {
                audilune_infinite_on = (uint8_t)!audilune_infinite_on;
                ui_message(audilune_infinite_on ? "AUTO ENGINE: ON" : "AUTO ENGINE: OFF");
            } else if (ui.morph_scope == 4u) {
                audilune_song_dj_trig = 1u;
                ui_message("SONG DJ MORPH");
            }
        } else if (ui.scr == UI_CHOP_WAVE) {
            if (ui.list_sel >= 2u) {
                ui.chop_trim = (uint8_t)((ui.chop_trim + 1u) % 4u);
                ui_message(ui.chop_trim == 1u ? "GRAB WHOLE" : ui.chop_trim == 2u ? "GRAB START [" : ui.chop_trim == 3u ? "GRAB END ]" : "DROPPED");

            }
        } else if (ui.scr == UI_CHOP_SRC) {
            ui.scr = UI_CHOP_WAVE;
            ui.list_sel = 0;
            ui.chop_sel = 0;
            ui.chop_trim = 0;
            ui_message("CHOP SHOP");
        } else {

            ui.sel_trk_mask ^= (uint8_t)(1u << (song.sel % NTRK));
            ui_message((ui.sel_trk_mask & (1u << (song.sel % NTRK))) ? "SEL: LOCKED" : "SEL: UNLOCKED");
        }
        ui.force = 1;
        break;
    case LY_MIX:
        audilune_toggle_scr(UI_GLO, FAM_GLO, "GLOBAL");
        break;
    case LY_SONG:
        audilune_toggle_scr(UI_SAVE, FAM_SAVE, "SAVE VAULT");
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
        uint32_t d = l == LY_SONG ? 0u : ((fm1_in.buttons & ly_bit[l]) != 0u);
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
    uint32_t k;
    for (k = 0; k < 4u; k++)
        panel_enc(EN_K1 + k);
}

UI_SMALL static void audilune_clear_selected(void)

{
    uint32_t i;
    if (ui.scr == UI_SAVE && ui.list_sel < 6u) {
        project_erase((uint32_t)ui.list_sel);
        ui_message("VAULT CLEARED");
        ui.force = 1;
        return;
    }
    if (ui.scr == UI_CHOP_WAVE) {
        if (ui.list_sel < 2u && ui.chop_src_type == 2u) {
            up_ui(1u, ui.chop_src_idx % UP_SLOTS);
            ui_message("USR SLOT CLEARED");
        } else {
            ui_message("SLICE CLEARED");
           }
        ui.force = 1;
        return;
    }
    if ((!ui.home && cur_page()->scope == SC_STEP) ||
        (*(ui.sel_step_mask + 0) | *(ui.sel_step_mask + 1) | *(ui.sel_step_mask + 2) | *(ui.sel_step_mask + 3))) {
        track_t *t = TSEL;
        uint32_t any_step = *(ui.sel_step_mask + 0) | *(ui.sel_step_mask + 1) | *(ui.sel_step_mask + 2) | *(ui.sel_step_mask + 3);
        undo_mark(t, (undo_sess += 4u) | 3u);
        fm1_irq_off();
        for (i = 0; i < NSTEP; i++)
            if (any_step ? ((*(ui.sel_step_mask + (i >> 5)) >> (i & 31u)) & 1u) : (i == ui.cursor)) {
                memset(&t->step[i], 0, sizeof t->step[i]);
                memset(&t->dstep[i], 0, sizeof t->dstep[i]);
                t->micro[i] = 0;
            }
        memset(ui.sel_step_mask, 0, sizeof ui.sel_step_mask);
        fm1_irq_on();
        ui_message(any_step ? "STEPS CLEARED" : "STEP CLEARED");
        ui.force = 1;
        return;
    }
    if (ui.scr == UI_SEQ && ui.list_sel < NTRK) {
        track_t *t = &trk[ui.list_sel];
        undo_mark(t, (undo_sess += 4u) | 3u);
        fm1_irq_off();
        track_defaults_steps(t);
        fm1_irq_on();
        ui_message("SEQ TRK CLEARED");
        ui.force = 1;
        return;

    }
    if (ui.sel_trk_mask) {
        fm1_irq_off();
        for (i = 0; i < NTRK; i++)
            if ((ui.sel_trk_mask >> i) & 1u) {
                undo_mark(&trk[i], (undo_sess += 4u) | 3u);
                track_defaults_steps(&trk[i]);
            }
        fm1_irq_on();
        ui_message("TRACKS CLEARED");
        ui.force = 1;
        return;
    }
    undo_mark(TSEL, (undo_sess += 4u) | 3u);
    fm1_irq_off();
    track_defaults_steps(TSEL);
    fm1_irq_on();
    ui_message("TRACK CLEARED");
    ui.force = 1;
}

UI_SMALL static void holds_input(uint32_t pressed, uint32_t now_ms)
{
    uint32_t rb = 1u << panel.btn[B_REC], rc_down = (fm1_in.buttons & rb) != 0u;
    static uint8_t rc_on, rc_held;
    static uint32_t rc_t0;
    if (pressed & rb) {
        rc_t0 = now_ms;
        rc_on = 1;
        rc_held = 0;
    }
    if (rc_down && rc_on && !rc_held && now_ms - rc_t0 >= 1200u) {
        rc_held = 1;
        ui.hold_kind = 0;
        audilune_clear_selected();
    } else if (rc_down && rc_on && !rc_held && now_ms - rc_t0 >= 500u && !ui.hold_kind) {
        ui.hold_kind = 1u;
        ui.force = 1;
    }
    if (!rc_down && rc_on) {
        if (ui.hold_kind == 1u) {
            ui.hold_kind = 0;
            ui.force = 1;
        }
        if (!rc_held) {
            if (al_cap.state == 1u || al_cap.state == 2u) {
                al_smp_cap_stop();
                rec_hud_pick = 0;
                rec_wait = 0;
                ui_message("CAPTURE FINISHED");
            } else {
                ft_owns_press();
                rec_toggle();
                if ((song.rec || rec_wait) && song.g[G_CLICK] <= 0)
                    song.g[G_CLICK] = 70;
                rec_hud_pick = (uint8_t)(rec_wait != 0u);
                if (!rec_wait)
                    al_smp_cap_stop();
                ui_message(rec_wait ? "REC: ACTIVE TRK" : song.rec ? "REC LIVE" : "REC DISARMED");
            }
            ui.force = 1;
        }
        rc_on = 0;
    }
    {
        static uint8_t sel_on, sel_disarmed, punch_on;

        static uint32_t sel_t0;
        uint32_t scl_b = 1u << panel.btn[B_SCL], scl_down = (fm1_in.buttons & scl_b) != 0u;
        if (scl_down && !sel_on) {
            sel_t0 = now_ms; sel_on = 1; sel_disarmed = 0;
            if (ui.scr == UI_PUNCH && !rec_hud_pick) {
                punch_on = 1;
                audilune_punch_apply(ui.list_sel, 1);
                ui.force = 1;
            }
        }
        if (scl_down && sel_on && !sel_disarmed && now_ms - sel_t0 >= 1000u) {
            sel_disarmed = 1;
            if (audilune_timer_alert) {
                audilune_timer_alert = 0;
                ui.session_timer = 0;
                ui_message("TIMER DISARMED");
                ui.force = 1;
            }
        }
        if (!scl_down) {
            if (punch_on) {
                punch_on = 0;
                audilune_punch_apply(ui.list_sel, 0);
                ui.force = 1;
            }
            sel_on = 0; sel_disarmed = 0;
        }
    }
    {
        static uint8_t sv_on, sv_held;
        static uint32_t sv_t0;
        uint32_t sb = 1u << panel.btn[B_SAVE], sv_down = (fm1_in.buttons & sb) != 0u;

        if (pressed & sb) { sv_t0 = now_ms; sv_on = 1; sv_held = 0; }

        if (sv_down && sv_on && !sv_held && now_ms - sv_t0 >= 3000u) { sv_held = 1; ui.hold_kind = 0; audilune_save_latch = 2u; ui_message("LOAD: TAP OP1-6"); ui.force = 1; }
        else if (sv_down && sv_on && !sv_held && now_ms - sv_t0 >= 650u && !ui.hold_kind) { ui.hold_kind = 2u; ui.force = 1; }
        if (!sv_down && sv_on) { if (ui.hold_kind == 2u) ui.hold_kind = 0; if (!sv_held) { audilune_save_latch = audilune_save_latch ? 0u : 1u; ui_message(audilune_save_latch ? "SAVE: TAP OP1-6" : "SAVE CANCELLED"); } sv_on = 0; ui.force = 1; }
    }
    {
        static uint8_t ed_on, ed_chop, fx_on, fx_p, env_on, env_l;
        static uint32_t ed_t0, fx_t0, env_t0;
        uint32_t eb = ly_bit[LY_ERASE], ed_down = (fm1_in.buttons & eb) != 0u;
        uint32_t fb = ly_bit[LY_FX], fx_down = (fm1_in.buttons & fb) != 0u;
        uint32_t nb = 1u << panel.btn[B_ENV], env_down = (fm1_in.buttons & nb) != 0u;
        if (ed_down && !ed_on) { ed_t0 = now_ms; ed_on = 1; ed_chop = 0; }
        if (ed_down && ed_on && !ed_chop && now_ms - ed_t0 >= 1200u) {
            ed_chop = 1; layer_unlock(); vis_on = 0; ui.home = 0; ui.scr = UI_CHOP_SRC;
            ui.list_sel = 0; ui.list_top = 0; ui_message("CHOP SHOP: SOURCE"); ui.force = 1;
        }
        if (!ed_down) { ed_on = 0; ed_chop = 0; }
        if (fx_down && !fx_on) { fx_t0 = now_ms; fx_on = 1; fx_p = 0; }
        if (fx_down && fx_on && !fx_p && now_ms - fx_t0 >= 700u) {
            fx_p = 1; layer_unlock(); vis_on = 0; ui.home = 0; ui.scr = UI_PUNCH;
            ui.list_sel = 0; ui.list_top = 0; ui_message("PUNCH-IN FX"); ui.force = 1;
        }
        if (!fx_down) { fx_on = 0; fx_p = 0; }
        if (env_down && !env_on) { env_t0 = now_ms; env_on = 1; env_l = 0; }
        if (env_down && env_on && !env_l && now_ms - env_t0 >= 700u) {
            env_l = 1; layer_unlock(); vis_on = 0; ui.home = 0; ui.scr = UI_LANDSCAPE;
            ui.list_sel = 0; ui.list_top = 0; ui_message("LANDSCAPER"); ui.force = 1;
        }
        if (!env_down) { env_on = 0; env_l = 0; }
    }
}

UI_SMALL static void remote_poll(void)

{
    static int16_t ch_was = -1;

    (void)ch_was;
    fm1_remote_btn = 0;
}

UI_SMALL static void ui_input(void)
{
    uint32_t pressed = fm1_input_edges(0), notes = fm1_input_note_edges(), now = fm1_ticks(), id, b, k;
    uint32_t home = btn_hold_ms(&ui.home_t0, B_HOME, now, 1, 3000u);
    int32_t s;

    int layered;
    audilune_layer_mask = ui.sel_trk_mask;
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
    if (pressed && audilune_pit_latch) { audilune_pit_latch = 0; ui.force = 1; }
    if (audilune_save_latch && notes) {
        for (k = 0; k < 6u; k++) if ((notes >> key_of_white(k)) & 1u) { if (audilune_save_latch == 1u) project_save(k); else project_load(k); audilune_save_latch = 0; ui.force = 1; break; }
        notes = 0;
    } else if ((ly_lock == LY_MIX || ((fm1_in.buttons >> panel.btn[B_GLO]) & 1u)) && (notes & (1u << key_of_white(1)))) {
        audilune_pit_latch = 1u; layer_unlock(); notes = 0; ui_message("LIVE PITCH: ON"); ui.force = 1;
    }
    layered = layers_input(notes, &pressed, home);
    if (home_eat && !((fm1_in.buttons >> panel.btn[B_HOME]) & 1u)) { if (home == BT_TAP) home = BT_NONE; home_eat = 0; }
    pressed &= ~(ly_bit[LY_FX] | ly_bit[LY_ERASE] | ly_bit[LY_ROLL] | ly_bit[LY_STEP] | ly_bit[LY_SCALE] | ly_bit[LY_MIX] | ly_bit[LY_SONG]);
    holds_input(pressed, fm1_ms);
    pressed &= ~((on_song_page() ? 0u : 1u << panel.btn[B_REC]) | (1u << panel.btn[B_SAVE]));

    if (layered || ui.hold_kind) {
        if (((fm1_in.buttons >> panel.btn[B_GLO]) & 1u) && (s = panel_enc(EN_SELECT)) != 0) {
            audilune_theme_idx = (uint8_t)((audilune_theme_idx + (s > 0 ? 1u : AUDILUNE_THEME_COUNT - 1u)) % AUDILUNE_THEME_COUNT);
            audilune_hud_text("THEME", AUDILUNE_THEME_NAMES[audilune_theme_idx]);
            ui.force = 1;

        } else if (!punch.hold && (s = panel_enc(EN_SELECT)) != 0) {
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
    if (home == BT_TAP) {
        pressed &= ~(1u << panel.btn[B_HOME]);
        if (ui.scr == UI_LIVE && !vis_on) {
            vis_on = 0;
            ui.scr = UI_VIS;
            vis_open();
            ui_message("VISUALIZER");
        } else {
            vis_on = 0;

            go_home();
            ui_message("LIVE PLAY");
        }
        ui.list_sel = 0;
        ui.list_top = 0;
        ui.force = 1;
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
            if (!ui.home && cur_page()->scope == SC_STEP) {
                int32_t len = (int32_t)trk_len(TSEL);
                int32_t dir = (b == B_OCTUP) ? 1 : -1;
                int32_t next_step = ((int32_t)ui.cursor + dir + len) % len;
                cursor_set(next_step);
                audilune_hud_popup("STEP", next_step + 1, "");
                break;
            }
            if (is_drum(TSEL) || !TSEL->synth_mode) {
                if ((fm1_in.buttons & both) == both) {
                    audilune_drum_octave = 0;
                } else {
                    audilune_drum_octave += b == B_OCTDN ? (audilune_drum_octave > -3 ? -1 : 0)
                                                         : (audilune_drum_octave < 3 ? 1 : 0);
                }
                audilune_hud_popup("DRUM OCT", audilune_drum_octave, "");
                ui.force = 1;
                break;
            }
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
            audilune_toggle_scr(UI_ENV, FAM_ENV, "ENVELOPE");
            break;
        case B_LFO:
            audilune_toggle_scr(UI_LFO, FAM_LFO, "LFO");
            break;
        default:
            break;
         }
    }
    if (song.seq_mode && cur_page()->scope == SC_STEP && !ui.sheet)
        seq_entry(notes);

    if ((s = panel_enc(EN_PRESET)) != 0) {
        ui_input_ms = fm1_ms;
        {
            uint32_t total, cur;
            if (!TSEL->synth_mode) {
                TSEL->synth_mode = 1;
                ui.force = 1;
            }
            cur = preset_pos(&total);
            if (total) {
                uint32_t next_p = (uint32_t)(((int32_t)cur + s % (int32_t)total + (int32_t)total) % (int32_t)total);

                preset_go(next_p);
                audilune_hud_popup("PRESET", (int32_t)(next_p + 1u), "");
            }
        }
    }

    if ((s = panel_enc(EN_ALGO)) != 0 && !ft_on) {
        extern uint8_t audilune_vox_genre[];
        int32_t span = AUDILUNE_GENRE_COUNT + (int32_t)DRUM_KITS;
        int32_t pos;
        ui_input_ms = fm1_ms;
        vis_on = 0;
        if (ui.scr != UI_ALGO) {
            pos = TSEL->synth_mode ? (int32_t)(ui.algo_genre[song.sel] % AUDILUNE_GENRE_COUNT)
                                   : AUDILUNE_GENRE_COUNT + clamp((int32_t)TDRUM->p[P_E0], 0, (int32_t)DRUM_KITS - 1);
            ui.scr = UI_ALGO;
            ui.list_top = 0;
        } else {
            pos = (int32_t)ui.list_sel;
        }
        pos = ((pos + s) % span + span) % span;
        ui.list_sel = (uint8_t)pos;
        if (pos < AUDILUNE_GENRE_COUNT) {
            audilune_vox_genre[song.sel % NTRK] = (uint8_t)pos;
        }
        if (pos < AUDILUNE_GENRE_COUNT)

            audilune_hud_text("GENRE", AUDILUNE_GENRE_NAMES[pos]);
        else
            audilune_hud_text("DRUM KIT", DRUM_KIT_NAMES[pos - AUDILUNE_GENRE_COUNT]);
        ui.force = 1;
    }

    if (vis_shown() && !rec_wait && !ft_on) {
        int32_t k_moved = 0;
        if ((s = panel_enc(EN_SELECT)) != 0) {
            ui_input_ms = fm1_ms;
            ui.vis_style = (uint8_t)((ui.vis_style + (s > 0 ? 1u : 3u)) % 4u);
            vis_select(s);
        }
        for (k = 0; k < 4u; k++)
            if (panel_enc(EN_K1 + k) != 0)
                k_moved = 1;
        if (pressed || notes || k_moved) {
            ui_input_ms = fm1_ms;
            vis_on = 0;
            go_home();

        }
        return;
    } else if (!vis_on && ui.scr == UI_LIVE && !ui.menu && !rec_wait && !ft_on && !song.rec &&
               (fm1_ms - ui_input_ms) >= 60000u) {
        vis_open();
        return;
    }
    if (rec_hud_pick && pressed) {
        rec_hud_pick = 0;
        ui.force = 1;
    }
    if ((s = panel_enc(EN_SELECT)) != 0 && !ft_on) {
        ui_input_ms = fm1_ms;
        if (rec_hud_pick) {
            static const char *const RN[5] = {"ACTIVE TRK", "LOCKED TRKS", "SAMPLE USR", "USB USR", "WHOLE SONG"};
            rec_hud_mode = (uint8_t)((rec_hud_mode + (s > 0 ? 1u : 4u)) % 5u);
            audilune_hud_text("REC", *(RN + rec_hud_mode));
            ui.force = 1;
            return;
        }
        if (audilune_pit_latch) { int32_t p = clamp((int32_t)TSEL->p[P_TRANS] + s, -24, 24); TSEL->p[P_TRANS] = (int16_t)p; audilune_hud_popup("LIVE PITCH", p, " ST"); return; }
        if (ui.scr == UI_FX && al_pad_list_mode) {
            uint32_t p = al_pad_idx & 7u;
            int32_t ny = clamp((int32_t)*(al_pad_y + p) - s * 4, 0, 127);
            *(al_pad_y + p) = (uint8_t)ny;
            audilune_chaos_apply(TSEL);
            audilune_hud_popup("PAD Y", ny * 100 / 127, "%");
            ui.force = 1;
        } else if (ui.scr == UI_SEQ && al_seq_ovw) {
            al_seq_ovw_step = (uint8_t)((al_seq_ovw_step + (s > 0 ? 16u : 112u)) & 127u);
            audilune_hud_popup("STEP", (int32_t)al_seq_ovw_step + 1, "");
            ui.force = 1;
        } else if (ui.scr >= UI_FX) {
            int32_t next_row = (int32_t)ui.list_sel + (s > 0 ? 1 : -1);

            if (next_row < 0) next_row = 0;
            ui.seq_grab = 0;
            ui.chop_trim = 0;
            ui.list_sel = (uint8_t)next_row;
            if (ui.scr == UI_CHOP_WAVE)
                ui.chop_sel = (uint8_t)(ui.list_sel >= 2u ? (ui.list_sel - 2u) & 15u : 0u);
            ui.force = 1;
        } else {
            uint32_t next_trk = (uint32_t)((song.sel + (s > 0 ? 1 : NTRK - 1)) % NTRK);
            track_select(next_trk);
            audilune_hud_popup("TRACK", (int32_t)(next_trk + 1u), "");
        }
    }
    if (ui.scr == UI_CHOP_WAVE && ui.chop_mode == 4u && notes) {
        for (k = 0; k < 16u; k++)
            if ((notes >> key_of_white(k)) & 1u) {
                ui.chop_sel = (uint8_t)k;
                ui.list_sel = (uint8_t)(k + 2u);
                audilune_hud_popup("MANUAL CUT", (int32_t)k + 1, "");
                ui.force = 1;
                break;
            }
    }

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

        if (rec_hud_pick && k == 3u) {
            rec_hud_usr = (uint8_t)((rec_hud_usr + (s > 0 ? 1u : 5u)) % 6u);
            audilune_hud_popup("REC USR", (int32_t)rec_hud_usr + 1, "");
            ui.force = 1;
        } else if (ui.scr == UI_SEQ && al_seq_ovw && k == 3u) {
            al_seq_ovw_step = (uint8_t)((al_seq_ovw_step + (s > 0 ? 1u : 127u)) & 127u);
            audilune_hud_popup("STEP", (int32_t)al_seq_ovw_step + 1, "");
            ui.force = 1;
        } else if (ui.scr == UI_SETUP && k >= 2u) {
            audilune_list_knob(k, s);

        } else if (ui.home || k < 2u) {
            audilune_main_knobs(k, s);
        } else if (ui.scr >= UI_FX) {
            audilune_list_knob(k, s);
        } else {

            edit_param(k, s);
        }
    }
}

/* ---------------------------------------------------- panel setup --- */

#define SETUP_IDLE_MS 30000u
UI_SMALL static void panel_setup(void)
{
    panel.magic = PANEL_MAGIC;
    layers_init();
    ui.force = 1;
}

