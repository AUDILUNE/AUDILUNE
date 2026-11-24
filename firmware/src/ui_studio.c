/* SPDX-License-Identifier: GPL-3.0-only */
/* LIVE screens, 240 x 240, AUDILUNE Block 2 style: pure black background, white high-contrast
 * typography, inverted white-on-black armed automation header, 6-track performance matrix,
 * and persistent bottom info HUD bar with 1.5s live parameter popups. */
static uint8_t drum_page, drum_cursor;   /* drum_page: 0 GRID, 1 KIT, 2 SOUND (2.7: a sound's TUNE, DECAY) */
#define drum_lane pen_lane                 /* (2.7.1) one drum sound picked everywhere: the GRID, SOUND, the SEQ layer, a pad
                                            * played (seq.c pen_lane) */
/* (2.7.1) the black keys pick the drum sound on the GRID page and in the SEQ layer (the pages: OCT- / OCT+), never heard;
 * the same key again: the other sound of its pair. F#3 .. F#5: KICK / KICK 2, SNARE / SNARE 2, RIM, CLAP, HAT / PEDAL,
 * OPEN HAT, LOW TOM / HI TOM, CRASH, RIDE, SHAKER / CONGA, COWBELL */
static const uint8_t DRUM_BLACK[11][2] = {{0, 1}, {2, 8}, {7, 7}, {3, 3}, {4, 6}, {5, 5}, {9, 10}, {11, 11}, {12, 12},
                                          {13, 14}, {15, 15}};
UI_SMALL static int32_t drum_black_of(uint32_t k)       /* key k: its black key 0..10, -1 a white key */
{
    uint32_t i;
    for (i = 0; i < 11u; i++)
        if (PUNCH_BLACK[i] == k)
            return (int32_t)i;
    return -1;
}
UI_SMALL static uint32_t drum_black_key(uint32_t lane)    /* the key of the black key a sound is on */
{
    uint32_t i;
    for (i = 0; i < 11u; i++)
        if (DRUM_BLACK[i][0] == lane || DRUM_BLACK[i][1] == lane)
            return PUNCH_BLACK[i];
    return PUNCH_BLACK[0];
}
UI_SMALL static void drum_black_pick(uint32_t b)          /* black key b: its sound, or the other of its pair */
{
    const uint8_t *g = DRUM_BLACK[b % 11u];
    pen_lane = pen_lane == g[0] && g[1] != g[0] ? g[1] : g[0];
    ui_say("SOUND ", LANE_NAME[pen_lane]);
    ui.force = 1;
}
UI_SMALL static void drum_hear(uint32_t lane) { if (drum_preview) audition_lane(lane); }   /* (2.7.1) menu DRUM PREVIEW */
static void trk_short_name(uint32_t c, char *b);
UI_SMALL static int on_drum_page(void) { return !ui.home && cur_page()->scope == SC_DRUM; }

/* ---------------------------------------------------------------- style --- */
/* TE_G1 .. TE_G4: the greys (tiles, empty steps, labels, secondary text), gfx.c (2.7: on a theme's background) */
#define TE_RED RGB(255, 44, 52)          /* recording, erasing */
/* TE_COL, TE_MID, TE_DIM: the tracks' colours, gfx.c (2.7: a theme may set them) */
#define TE_DRUM TE_COL[3]
#define HUD_ACCENT TE_COL[0]             /* AUDILUNE Block 2: active theme accent for 1px HUD border */

UI_SMALL static void te_disc(int32_t cx, int32_t cy, int32_t r, uint16_t c)     /* filled circle */
{
    int32_t y, x;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++)
            if (x * x + y * y <= r * r + r)
                cv_pset(cx + x, cy + y, c);
}
/* a dial: a 270-degree ring (lit up to the value), a pointer; ratio 0..1000, -1 = a plain ring */
UI_SMALL static void te_dial(int32_t cx, int32_t cy, int32_t r, int32_t ratio, uint16_t c, uint16_t dim)
{
    int32_t i, end = ratio < 0 ? 768 : ratio * 768 / 1000;
    for (i = 0; i <= 768; i += 8) {
        uint32_t a = (uint32_t)(384 + i) & 1023u;
        int32_t co = SINE[(a + 256u) & 1023u], si = SINE[a], k;
        uint16_t col = ratio < 0 || i <= end ? c : dim;
        for (k = r - 2; k <= r; k++)
            cv_pset(cx + ((co * k) >> 15), cy + ((si * k) >> 15), col);
    }
    if (ratio >= 0) {
        uint32_t a = (uint32_t)(384 + end) & 1023u;
        int32_t co = SINE[(a + 256u) & 1023u], si = SINE[a];
        cv_line(cx, cy, cx + ((co * (r - 4)) >> 15), cy + ((si * (r - 4)) >> 15), C_WHITE);
        te_disc(cx, cy, 2, C_WHITE);
    }
}
UI_SMALL static void te_play_icon(int32_t x, int32_t y, int playing)
{
    int32_t i;
    if (playing)
        for (i = 0; i < 12; i++)
            cv_rect(x + i, y + i / 2, 1, 14 - i, song.rec ? C_BLACK : C_WHITE);
    else
        cv_rect(x, y + 1, 12, 12, song.rec ? C_BLACK : TE_G3);
}
UI_SMALL static uint32_t studio_hash(uint32_t h, const char *p)
{ while (*p) h = h * 31u + (uint8_t)*p++; return h; }
UI_SMALL static void te_lower(char *d, const char *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i + 1u < n && s[i]; i++)
        d[i] = s[i] >= 'A' && s[i] <= 'Z' ? (char)(s[i] + 32) : s[i];
    d[i] = 0;
}
UI_SMALL static void te_upper(char *d, const char *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i + 1u < n && s[i]; i++)
        d[i] = s[i] >= 'a' && s[i] <= 'z' ? (char)(s[i] - 32) : s[i];
    d[i] = 0;
}
UI_SMALL static void te_text_c(int32_t cx, int32_t y, const char *s, uint16_t c)   /* centred on cx */
{
    cv_text(cx - text_w(&FONT_S, s) / 2, y, &FONT_S, s, c);
}

static uint8_t rec_hud_pick, rec_hud_mode, rec_hud_usr;
UI_SMALL static const char *al_nth_str(const char *s, uint32_t n);

/* AUDILUNE Block 2: Persistent Bottom Info Bar (HUD Footer) with 1px active theme border
 * and 1.5s live parameter popups when turning any knob */
UI_SMALL static void audilune_hud_bar(const char *const lab[2 + 2], const char *const val[2 + 2], int32_t sect, uint32_t *cache)
{
    char line[18 + 18], nb[6 + 6];
    uint32_t hot = ui.hot_t > 0 && ui.hot_col < 4u && *(*(lab + ui.hot_col)) != 0;
    uint32_t arm_hud = !song.rec && (rec_hud_pick || rec_wait);
    uint32_t sig = studio_hash((uint32_t)song.sel * 17u + (uint32_t)song.g[G_BPM] * 31u +
                               (uint32_t)(sect + 1) * 97u + hot * 5003u + (uint32_t)ui.hot_col * 131u +
                               audilune_timer_alert * 7919u + arm_hud * 104729u +
                               (uint32_t)rec_hud_mode * 313u + (uint32_t)rec_hud_usr * 617u,
                               ui.msg_t ? ui.msg : "");
    if (hot) {
        sig = studio_hash(sig, *(lab + ui.hot_col));
        sig = studio_hash(sig, *(val + ui.hot_col));
    }
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, 26, C_BLACK);
    cv_rect(0, 0, 240, 26, HUD_ACCENT);
    cv_rect(1, 1, 238, 24, C_BLACK);
    if (ui.msg_t) {
        cv_rect(1, 1, 238, 24, C_WHITE);
        te_text_c(120, 6, ui.msg, C_BLACK);
    } else if (audilune_timer_alert) {
        cv_rect(1, 1, 238, 24, C_WHITE);
        te_text_c(120, 6, "TIME UP - HOLD SEL: DISARM", C_BLACK);
    } else if (arm_hud) {
        static const char REC_HUD_STR[43 + 43] =
            "REC: ACTIVE TRK (PLAY)\0REC: LOCKED TRKS\0REC: SAMPLE USR\0REC: USB USR\0REC: WHOLE SONG";
        uint32_t m = rec_hud_mode % 5u;
        str_cpy(line, al_nth_str(REC_HUD_STR, m), sizeof line);
        if (m == 2u || m == 3u) {
            uint32_t p = str_len(line);
            fmt_int(nb, (int32_t)(rec_hud_usr % 6u) + 1);
            str_cpy(line + p, nb, sizeof line - p);
        }
        cv_rect(1, 1, 238, 24, C_WHITE);
        te_text_c(120, 6, line, C_BLACK);
    } else if (hot) {
        char up_l[8 + 8], up_v[8 + 8];
        uint32_t p = 0;
        te_upper(up_l, *(lab + ui.hot_col), sizeof up_l);
        te_upper(up_v, *(val + ui.hot_col), sizeof up_v);
        str_cpy(line, up_l, sizeof line);
        p = str_len(line);
        if (p + 2u < sizeof line) { line[p++] = ':'; line[p++] = ' '; line[p] = 0; }
        str_cpy(line + p, up_v, sizeof line - p);
        te_text_c(120, 6, line, C_WHITE);
    } else {
        uint32_t p = 0;
        char pat = (char)('A' + ((sect >= 0 ? sect : 0) & 3));
        str_cpy(line, "TRK ", sizeof line);
        p = str_len(line);
        fmt_int(nb, (int32_t)song.sel + 1);

        str_cpy(line + p, nb, sizeof line - p);

        p = str_len(line);
        if (p + 8u < sizeof line) {
            line[p++] = ' '; line[p++] = '|'; line[p++] = ' ';
            line[p++] = 'P'; line[p++] = 'A'; line[p++] = 'T'; line[p++] = ' ';

            line[p++] = pat; line[p] = 0;
        }
        if (p + 3u < sizeof line) { line[p++] = ' '; line[p++] = '|'; line[p++] = ' '; line[p] = 0; }
        fmt_int(nb, song.g[G_BPM]);
        str_cpy(line + p, nb, sizeof line - p);
        p = str_len(line);
        str_cpy(line + p, " BPM", sizeof line - p);
        te_text_c(120, 6, line, C_WHITE);
    }
    cv_blit(0, 214);
}

/* the dial strip: KNOB 1..4, label + value under each (a dial with no label: an empty column); a
 * message replaces it */
UI_SMALL static void te_dials(int32_t y0, const char *const lab[4], const char *const val[4], const int32_t ratio[4],
                     uint32_t sig, uint32_t *cache)
{
    uint32_t k;
    sig = studio_hash(sig, ui.msg_t ? ui.msg : "");
    for (k = 0; k < 4u; k++) {
        sig = studio_hash(sig * 7u + (uint32_t)ratio[k] + (ui.hot_t && ui.hot_col == k) * 5003u, lab[k]);
        sig = studio_hash(sig, val[k]);
    }
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, ui.msg_t ? 240u - (uint32_t)y0 : 40u, C_BLACK);
    if (ui.msg_t) {                                     /* a message: a white bar */
        cv_rect(0, 10, 240, 32, C_WHITE);
        cv_text((240 - text_w(&FONT_S, ui.msg)) / 2, 18, &FONT_S, ui.msg, C_BLACK);
        cv_blit(0, (uint32_t)y0);
        return;
    }
    for (k = 0; k < 4u; k++) {
        int32_t cx = 30 + 60 * (int32_t)k;
        if (!lab[k][0])
            continue;
        te_dial(cx, 12, 11, ratio[k], TE_COL[k], TE_DIM[k]);
        te_text_c(cx, 24, lab[k], TE_G3);
    }
    cv_blit(0, (uint32_t)y0);
    cv_begin(240, 16, C_BLACK);                         /* the values: white while turned */
    for (k = 0; k < 4u; k++)
        te_text_c(30 + 60 * (int32_t)k, 0, val[k], ui.hot_t && ui.hot_col == k ? C_WHITE : TE_G4);
    cv_blit(0, (uint32_t)y0 + 40u);
}
UI_SMALL static void studio_open(uint32_t scope)
{
    uint32_t i;
    if (scope == SC_SONG && rec_wait)
        rec_wait = 0;                                   /* (an arm does not follow into the song page) */
    for (i = 0; i < NPAGES; i++) if (PAGES[i].scope == scope) {
        ui.page = (uint8_t)i; ui.home = 0; song.seq_mode = 0; ui.force = 1;
        ui.msg_t = 0; ui.entry_open = 0; return;
    }
}
/* LIVE: REC, on any page. One record arm, on the selected track (it follows ALGORITHM). No
 * click (seq.c): playing, recording starts at once; stopped, REC arms and the first note
 * starts the loop (or, in an empty project, a free take that REC closes: seq.c ft_close).
 * REC again stops recording / cancels the arm (playback continues). */
UI_SMALL static void rec_toggle(void)
{
    if (ft_owns_press())
        return;
    if (song.playing && arrangement_enabled) {
        ui_message("STOP THE SONG FIRST");
        return;
    }
    if (song.rec || rec_wait) {
        song.rec = 0;
        rec_wait = 0;
        ui_message("REC OFF");
        return;
    }
    arrangement_enabled = 0;
    if (song.playing)
        rec_begin();                    /* playing: record now */
    else
        rec_wait = 1;                   /* stopped: the first note starts it */
}

/* the loop position of track t: "bar.beat" in its pattern (1.1 .. ) */
UI_SMALL static void loop_pos(const track_t *t, char *b)
{
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), s = t->seq_idx % len, k;
    fmt_int(b, (int32_t)(s / 16u + 1u));
    k = str_len(b);
    b[k] = '.';
    b[k + 1] = (char)('1' + (s % 16u) / 4u);
    b[k + 2] = 0;
}

/* the header of the live screens: BPM, transport, loop position, beat lights, inverted white when REC armed */
UI_SMALL static void te_header(const char *title, uint16_t tc, uint32_t *cache, int32_t sect)
{
    char b[12], nb[8];
    uint32_t beat = clk_beat, k, playing = song.playing, armed = (song.rec != 0) || (rec_wait != 0);
    int32_t raw_lvl = song.sel == TRK_DRUM && !TDRUM->synth_mode ? song.g[G_DRLVL] : TSEL->p[P_LEVEL];
    uint32_t mst = (uint32_t)clamp(raw_lvl * 100 / 127, 0, 100);
    const char *clk_s = song.g[G_SYNC] == 1 ? "USB" : song.g[G_SYNC] == 2 ? "MIDI" : "INT";
    uint16_t bg = armed ? C_WHITE : C_BLACK;
    uint16_t fg = armed ? C_BLACK : C_WHITE;
    uint16_t dim = armed ? TE_G3 : TE_G2;
    uint32_t sig = studio_hash((uint32_t)song.g[G_BPM] * 7u + playing * 3u + armed * 1999u + mst * 53u +
                               (playing ? beat * 131u + TSEL->seq_idx * 7919u : 0u) + (uint32_t)song.g[G_SYNC] * 77u +
                               arrangement_enabled * 5u + (ui.bpm_t != 0) * 104729u + song.solo * 37u, title);
    sig = sig * 31u + tc;
    sig = sig * 31u + (uint32_t)(sect + 1) * 211u;
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, 40, bg);
    fmt_int(b, song.g[G_BPM]);
    cv_text(4, 3, &FONT_L, b, fg);
    cv_text(4 + text_w(&FONT_L, b) + 3, 18, &FONT_S, "BPM", armed ? C_BLACK : TE_G3);
    te_play_icon(88, 5, (int)playing);
    if (armed) {
        cv_text(104, 4, &FONT_S, song.rec ? "REC" : "ARM", C_BLACK);
    } else if (playing) {
        loop_pos(TSEL, b);
        cv_text(104, 4, &FONT_S, b, fg);
    } else {
        cv_text(104, 4, &FONT_S, "STOP", TE_G3);
    }
    for (k = 0; k < 4u; k++)
        cv_rect(88 + (int32_t)k * 9, 24, 7, 7,
                playing && beat % 4u == k ? (k ? fg : HUD_ACCENT) : dim);
    str_cpy(b, "MST ", sizeof b);
    fmt_int(nb, (int32_t)mst);
    str_cpy(b + 4, nb, sizeof b - 4);
    str_cpy(b + str_len(b), "%", sizeof b - str_len(b));
    cv_text(236 - text_w(&FONT_S, b), 4, &FONT_S, b, fg);
    {
        int32_t rx = 236 - text_w(&FONT_S, clk_s);
        cv_text(rx, 21, &FONT_S, clk_s, armed ? C_BLACK : HUD_ACCENT);
        if (sect >= 0) {
            char sb[2] = {(char)('A' + (sect % 6)), 0};
            int32_t bw = text_w(&FONT_S, sb) + 6, bx = rx - bw - 5;
            cv_rect(bx, 20, bw, 13, armed ? C_BLACK : HUD_ACCENT);
            cv_text(bx + 3, 21, &FONT_S, sb, armed ? C_WHITE : C_BLACK);
        }

    }
    cv_rect(0, 39, 240, 1, armed ? C_BLACK : HUD_ACCENT);
    cv_blit(0, 0);

}
/* --------------------------------------------------------------- TRACKS --- */
/* swing, MPC style: "62%" */
UI_SMALL static void swing_str(char *b, int32_t v)            /* 0 straight .. 100 the most (params.c F_SWING) */
{
    fmt_int(b, clamp(v, 0, 100));
}

/* Persistent 14px Active-Selection Track & Live Step/Waveform Strip (y = 41..55) */
UI_SMALL static void audilune_sel_strip(uint32_t *cache)
{
    uint32_t mask = (ui.sel_trk_mask & 0x3Fu) | (1u << (song.sel % 6u));
    uint32_t i, j, pos = TSEL->seq_idx % 16u, sig = mask * 977u + pos * 31u + song.playing * 7u;
    for (i = 0; i < NTRK; i++)
        if ((mask >> i) & 1u)
            for (j = 0; j < 16u; j++)
                sig = sig * 3u + (uint32_t)trk_step_on(&trk[i], j);
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, 14, C_BLACK);
    for (i = 0; i < 6u; i++) {
        char n[2] = {(char)('1' + i), 0};
        uint32_t on = (mask >> i) & 1u;
        int32_t bx = 2 + (int32_t)i * 13;
        cv_rect(bx, 1, 11, 11, on ? C_WHITE : TE_G1);
        cv_text(bx + 3, 0, &FONT_S, n, on ? C_BLACK : TE_G3);
    }
    for (j = 0; j < 16u; j++) {
        uint32_t hit = 0u;
        for (i = 0; i < NTRK; i++)
            if (((mask >> i) & 1u) && trk_step_on(&trk[i], j))
                hit = 1u;
        cv_rect(84 + (int32_t)j * 6, 3, 4, 7, (song.playing && j == pos) ? C_WHITE : hit ? HUD_ACCENT : TE_G1);
    }
    for (j = 0; j < 26u; j++) {
        uint32_t p = (pos + j) & 15u;
        int32_t h = (song.playing && trk_step_on(TSEL, p)) ? (int32_t)(3 + ((j * 7u + pos) % 7u)) : 1;
        cv_rect(184 + (int32_t)j * 2, 6 - h / 2, 1, h, C_WHITE);
    }
    cv_rect(0, 13, 240, 1, TE_G1);
    cv_blit(0, 41);
}

UI_SMALL static void studio_tracks_draw(void)
{
    static uint32_t head, strip, rows[6], footer;
    static const char *const GEN_SHORT[6] = {"AMBIENT", "DOWNTMP", "GLITCH", "PUNCHY", "EXPERIM", "INDUST"};
    uint32_t i, j, disp_trks = NTRK < 6u ? 6u : NTRK;
    int32_t row_h = (int32_t)(156u / disp_trks);
    int32_t sect = song.playing && live_sec >= 0 ? live_sec : (int32_t)song.g[G_SLOT] - 1;
    audilune_in_arp_page = 0u;
    te_header("AUDILUNE", C_WHITE, &head, sect);
    audilune_sel_strip(&strip);
    for (i = 0; i < disp_trks; i++) {

        uint32_t has_trk = i < NTRK;
        track_t *t = has_trk ? &trk[i] : &trk[0];
        char b[24], e[24];
        uint32_t selected = song.sel == i;

        uint32_t locked = (ui.sel_trk_mask >> i) & 1u;
        uint32_t len = has_trk ? (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP) : 16u;
        uint32_t level = !has_trk ? 0u : (!t->synth_mode && i == TRK_DRUM ? (uint32_t)song.g[G_DRLVL] : (uint32_t)t->p[P_LEVEL]);
        uint32_t silent = !has_trk || trk_silent(t) || !level;
        uint32_t pos = has_trk ? (t->seq_idx % len) : 0u;
        uint32_t rec = has_trk ? ((song.rec >> i) & 1u) : 0u;
        uint32_t solo = has_trk ? ((song.solo >> i) & 1u) : 0u;
        uint32_t bank = song.playing ? pos / 16u : 0u, h;
        uint8_t gidx = ui.algo_genre[i] % 6u;
        uint16_t acc = HUD_ACCENT;
        if (!has_trk) {
            str_cpy(b, "OPEN", sizeof b);
            str_cpy(e, "AMBIENT", sizeof e);
        } else if (!t->synth_mode) {
            uint32_t kidx = (uint32_t)clamp((int32_t)drum_kit(), 0, 31);
            b[0] = 0;
            str_cpy(e, "DRUM KIT: ", sizeof e);
            str_cpy(e + 10, DRUM_KIT_NAMES[kidx], sizeof e - 10);
        } else {
            trk_short_name(i, b);
            b[11] = 0;
            str_cpy(e, GEN_SHORT[gidx], sizeof e);
        }

        h = studio_hash(selected + locked * 31u + gidx * 17u + level * 7u + silent * 997u + rec * 1999u +
                        solo * 4999u + len * 37u + song.playing * 7u + pos * 71u + bank * 13u + has_trk * 3u, b[0] ? b : e);

        if (has_trk)
            for (j = 0; j < len; j++) h = h * 31u + (uint32_t)trk_step_on(t, j);
        if (!ui.force && h == rows[i]) continue;
        rows[i] = h;
        cv_begin(240, (uint32_t)row_h, C_BLACK);
        cv_rect(2, 2, 24, row_h - 4, (selected || locked) ? C_WHITE : TE_G1);
        {
            char n[2] = {(char)('1' + i), 0};
            cv_text(10, (row_h - 14) / 2, &FONT_S, n, (selected || locked) ? C_BLACK : C_WHITE);
        }
        if (b[0]) {
            cv_text(32, 1, &FONT_S, b, selected ? C_WHITE : TE_G4);
            if (32 + text_w(&FONT_S, b) + 4 + text_w(&FONT_S, e) < 182)
                cv_text(32 + text_w(&FONT_S, b) + 4, 1, &FONT_S, e, selected ? acc : TE_G3);
        } else {
            cv_text(32, 1, &FONT_S, e, selected ? acc : C_WHITE);
        }
        if (rec) {

            cv_rect(186, 2, 50, 12, C_WHITE);
            cv_text(199, 1, &FONT_S, "REC", C_BLACK);
       } else if (solo) {
            cv_rect(186, 2, 50, 12, C_WHITE);
            cv_text(196, 1, &FONT_S, "SOLO", C_BLACK);
        } else if (silent) {

            cv_text(204, 1, &FONT_S, "MUTE", TE_G3);
        } else {
            char pb[1 + 1] = {(char)('A' + ((sect >= 0 ? sect : 0) % 6)), 0};
            cv_rect(222, 2, 14, 12, selected ? acc : TE_G1);
            cv_text(225, 1, &FONT_S, pb, selected ? C_BLACK : C_WHITE);
        }
        for (j = 0; j < 16u; j++) {
            uint32_t p = bank * 16u + j, on = 0;
            if (!has_trk) {
                on = 1u;
            } else if (len <= 16u) {

                if (p < len) on = trk_step_on(t, p) ? 2u : 1u;
            } else {
                uint32_t a = j * len / 16u, z = (j + 1u) * len / 16u, k;
                if (song.playing) {
                    if (p < len) on = trk_step_on(t, p) ? 2u : 1u;
                } else {
                    on = 1;
                    for (k = a; k < z; k++) if (trk_step_on(t, k)) on = 2;
                }
            }
            cv_rect(32 + (int32_t)j * 10, row_h - 11, 8, 7,
                    !on ? C_BLACK : on == 2u ? (silent ? TE_G3 : selected ? C_WHITE : acc) : TE_G1);
            if (has_trk && song.playing && p == pos)
                cv_rect(32 + (int32_t)j * 10, row_h - 3, 8, 2, C_WHITE);
        }
        {
            int32_t mw = silent ? 0 : (int32_t)level * 38 / 127, mx;
            cv_rect(198, row_h - 10, 38, 5, TE_G1);
            cv_rect(198, row_h - 12, 1, 2, TE_G3);
            cv_rect(217, row_h - 12, 1, 2, TE_G3);
            cv_rect(235, row_h - 12, 1, 2, TE_G3);
            for (mx = 0; mx < mw; mx += 2)
                cv_rect(198 + mx, row_h - 10, 1, 5, selected ? C_WHITE : acc);
        }
        cv_blit(0, 56u + i * (uint32_t)row_h);
    }
    {
        static const char *const HUD_L[4] = {"BPM", "CLICK", "VAL", "OPT"};
        static const char *const HUD_V[4] = {"", "", "", ""};

        audilune_hud_bar(HUD_L, HUD_V, sect, &footer);
    }
}

/* ---------------------------------------------------------------- DRUMS --- */
static const char *const LV_NAME[4] = {"norm", "ghost", "soft", "hard"};
UI_SMALL static uint32_t lvl_rank(uint32_t lvl);                 /* (below) ghost 0, soft 1, norm 2, hard 3 */
UI_SMALL static uint16_t lvl_col(uint32_t lvl)                   /* a hit's colour by its level */
{
    return lvl == LV_GHOST ? TE_DIM[3] : lvl == LV_SOFT ? TE_MID[3] : lvl == LV_HARD ? C_WHITE : TE_DRUM;
}
static uint16_t pad_lit[DRUM_LANES];                   /* pads and key LEDs: frames left lit */
static uint8_t key_lit[27];                             /* menu NOTES, a synth track: frames its key stays lit */

static uint8_t key_lit_trk = 0xFF;                      /* (the track key_lit is for) */
UI_SMALL static void pads_tick(void)                             /* once a frame: the hits since the last one */
{
    uint32_t i, hits, n[NPART][4], k, w;
    fm1_irq_off();
    hits = drums.hits;
    drums.hits = 0;
    for (i = 0; i < NPART; i++)
        for (w = 0; w < 4u; w++) {
            n[i][w] = note_hits[i][w];
            note_hits[i][w] = 0;
        }
    fm1_irq_on();
    for (i = 0; i < DRUM_LANES; i++) {
        if ((hits >> i) & 1u) pad_lit[i] = 6;
        else if (pad_lit[i]) pad_lit[i]--;
    }
    /* a synth track: each note it started lights its key 6 frames (~0.1 s), so a short sequencer note
     * (a 1/16 at GATE 50 %: 60..80 ms) is seen even when it ends between two frames */
    if (song.sel != key_lit_trk) {
        memset(key_lit, 0, sizeof key_lit);
        key_lit_trk = song.sel;
    }
    for (k = 0; k < 27u; k++)
        if (key_lit[k])
            key_lit[k]--;
    if (song.sel < NPART && (n[song.sel][0] | n[song.sel][1] | n[song.sel][2] | n[song.sel][3]))
        for (k = 0; k < 27u; k++) {
            uint32_t note = kb_map(TSEL, k);
            if (note < 128u && (n[song.sel][note >> 5] >> (note & 31u)) & 1u)
                key_lit[k] = 6;
        }
}
UI_SMALL static void drum_screen_draw(void)
{
}
/* the level order of the knobs: ghost < soft < norm < hard */

static const uint8_t LV_UP[4] = {LV_GHOST, LV_SOFT, LV_NORM, LV_HARD};
UI_SMALL static uint32_t lvl_rank(uint32_t lvl) { return lvl == LV_GHOST ? 0u : lvl == LV_SOFT ? 1u : lvl == LV_NORM ? 2u : 3u; }

/* the GRID page shown, no layer held: its keys are steps (seq.c kb_grid) */
UI_SMALL static int grid_keys_on(void) { return on_drum_page() && !drum_page && song.sel == TRK_DRUM && !ui.menu && !ui.sheet; }
/* a key down on the GRID page: the white keys are the 16 steps of this page of steps for the sound of
 * KNOB 1 — an empty one is set (NORM; 2.7.1: silent, for live), a set one cleared. 2.7.1: the black keys pick the sound
 * (drum_black_pick), OCT- / OCT+ the page (steps 1-16, 17-32, 33-48, 49-64), as in the SEQ layer. The cursor follows */
UI_SMALL static void grid_key(uint32_t k)
{
    uint32_t len = trk_len(TDRUM), idx;
    int32_t w = punch_key(k), b = drum_black_of(k);
    dstep_t *st;
    if (w < 0) {
        if (b >= 0)
            drum_black_pick((uint32_t)b);
        return;
    }
    idx = drum_cursor / 16u * 16u + (uint32_t)w;
    if (idx >= len)
        return;
    drum_cursor = (uint8_t)idx;
    if (song.playing && arrangement_enabled) {
        ui_message("STOP THE SONG FIRST");
        return;
    }
    undo_mark(TDRUM, ui.step_sess ? ui.step_sess : (ui.step_sess = (undo_sess += 4u) | 3u));
    st = &TDRUM->dstep[idx];
    fm1_irq_off();
    if (dstep_has(st, drum_lane)) {
        dstep_clr(st, drum_lane);
        if (!dstep_mask(st)) {                          /* an empty step keeps no lock, no nudge, no condition */
            lock_del(TDRUM, idx, P_COUNT);
            TDRUM->micro[idx] = 0;
            step_fill_set(TDRUM, idx, FC_NORM);
        }
    } else {
        dstep_set(st, drum_lane, LV_NORM, 0);
        TDRUM->seq_active = 1;
    }
    fm1_irq_on();
    sync_reload = 1;                                    /* (2.7.1: not heard: the pattern plays it) */
}

/* (2.7.2) the GRID's sheet (OCT+ held), as the NOTES page's: the hit of KNOB 1's sound on KNOB 2's step. RATCHET (its
 * hits in the step), NUDGE and FILL (the step's, every sound in it), MOVE (KNOB 2: a step a detent), COPY (OCT+: to the
 * next step, then MOVE moves the copy), DELETE (OCT+) */
UI_SMALL static int drum_hit_shift(int32_t dir, int copy)   /* the hit a step on / back (copy: the next step); 0: no room */
{
    uint32_t len = trk_len(TDRUM), to = (uint32_t)((int32_t)drum_cursor + (copy ? 1 : dir));
    dstep_t *st = &TDRUM->dstep[drum_cursor];
    if (!dstep_has(st, drum_lane) || (!copy && dir < 0 && !drum_cursor) || to >= len ||
        dstep_has(&TDRUM->dstep[to], drum_lane))
        return 0;
    fm1_irq_off();
    dstep_set(&TDRUM->dstep[to], drum_lane, dstep_lvl(st, drum_lane), dstep_rat(st, drum_lane));
    if (!copy)
        dstep_clr(st, drum_lane);
    fm1_irq_on();
    drum_cursor = (uint8_t)to;
    return 1;
}
UI_SMALL static void drum_sheet_knob(uint32_t k, int32_t s)
{
    dstep_t *st = &TDRUM->dstep[drum_cursor];
    uint32_t row = ui.sheet - 1u;
    if (k == 0u) {
        ui.sheet = (uint8_t)(1 + clamp((int32_t)row + s, 0, 5));
        ui.sheet_last = (uint8_t)(ui.sheet - 1u);          /* (2.8.1) reopen on this row next time */
        return;
    }
    if (k != 1u)
        return;
    undo_mark(TDRUM, (undo_sess += 4u) | 3u);
    fm1_irq_off();
    if (row == 0u && dstep_has(st, drum_lane))
        dstep_set(st, drum_lane, dstep_lvl(st, drum_lane), (uint32_t)clamp((int32_t)dstep_rat(st, drum_lane) + s, 0, 3));
    else if (row == 1u)
        TDRUM->micro[drum_cursor] = (int8_t)clamp(TDRUM->micro[drum_cursor] + s, MICRO_MIN, MICRO_MAX);
    else if (row == 2u)
        step_fill_set(TDRUM, drum_cursor, (uint32_t)clamp((int32_t)step_fill(TDRUM, drum_cursor) % 3 + (s > 0 ? 1 : -1), 0, 2));
    fm1_irq_on();
    if (row == 3u)
        for (; s; s += s > 0 ? -1 : 1)
            if (!drum_hit_shift(s > 0 ? 1 : -1, 0)) {
                ui_message(dstep_has(&TDRUM->dstep[drum_cursor], drum_lane) ? "NO ROOM" : "NO HIT");
                break;
            }
    sync_reload = 1;
}
UI_SMALL static void drum_sheet_do(void)                 /* OCT+ tapped in the sheet */
{
    undo_mark(TDRUM, (undo_sess += 4u) | 3u);
    if (ui.sheet == 5u) {
        if (drum_hit_shift(1, 1)) {
            ui.sheet = 4;                                 /* (MOVE: KNOB 2 moves the copy) */
            ui_message("COPIED");
        } else {
            ui_message(dstep_has(&TDRUM->dstep[drum_cursor], drum_lane) ? "NO ROOM" : "NO HIT");
        }
    } else if (ui.sheet == 6u) {
        fm1_irq_off();
        dstep_clr(&TDRUM->dstep[drum_cursor], drum_lane);
        fm1_irq_on();
        ui.sheet = 0;
        ui_message("HIT DELETED");
    }
    sync_reload = 1;
}
UI_SMALL static void drum_screen_input(uint32_t pressed, uint32_t home, uint32_t notes)
{
    (void)pressed; (void)home; (void)notes;
    return;
    uint32_t k, b;
    int32_t s;
    if (song.sel != TRK_DRUM || home == 1u) {
        go_home();
        return;
    }
    if (!drum_page) {                                  /* (2.7.2) OCT+ held: the hit's sheet; tapped: the page */
        uint32_t ev, ob = 1u << panel.btn[B_OCTDN];
        pressed = octp_filter(pressed, &ev);
        if (ev == 2u && !ui.sheet)

            ui.sheet = (uint8_t)(1u + ui.sheet_last);      /* (2.8.1) reopen on the row last used */
        else if (ev == 1u && ui.sheet)
            drum_sheet_do();
        else if (ev == 1u)
            pressed |= 1u << panel.btn[B_OCTUP];
        if (ui.sheet && (pressed & ob)) {
            ui.sheet = 0;
            pressed &= ~ob;
        }
        if (ev)
            ui.force = 1;
    } else {
        ui.sheet = 0;
    }
    for (k = 0; k < NB; k++) if ((pressed >> panel.btn[k]) & 1u) {
        b = k;
        if ((b == B_OCTDN || b == B_OCTUP) && !drum_page) {   /* (2.7.1) GRID: OCT- / OCT+ the page of steps */
            uint32_t len = trk_len(TDRUM), pages = (len + 15u) / 16u, pg = drum_cursor / 16u, idx;
            pg = (pg + (b == B_OCTUP ? 1u : pages - 1u)) % pages;
            idx = pg * 16u + drum_cursor % 16u;
            drum_cursor = (uint8_t)(idx < len ? idx : len - 1u);
            ui.force = 1;
            continue;
        }
        if (b == B_PLAY) {
            if (ft_owns_press()) ;
            else if (!song.playing && arrangement_enabled && !arr_valid(&arrangement, arrangement_ready())) ui_message("EMPTY SECTION: REC");
            else transport_req = song.playing ? 2 : 1;
        } else if (b == B_SEQ || b == B_EDIT) {
            drum_page = (uint8_t)((drum_page + 1u) % 3u);   /* GRID -> KIT -> SOUND (2.7) */
            ui.msg_t = 0;
            ui.force = 1;
        } else if (b == B_SAVE) {
            studio_open(SC_SONG);
            return;
        }
    }
    if ((s = panel_enc(EN_SELECT)) && (uint32_t)clamp((int32_t)drum_page + s, 0, 2) != drum_page) {
        drum_page = (uint8_t)clamp((int32_t)drum_page + s, 0, 2);
        ui.msg_t = 0;
        ui.force = 1;
    }
    if (drum_page == 2u && notes) {
        for (k = 0; k < 32u; k++)
            if ((notes >> k) & 1u) {
                drum_lane = (uint8_t)lane_of_key(k);
                ui.force = 1;
            }
    }
    if ((s = panel_enc(EN_ALGO)) && !ft_on) {
        track_select((uint32_t)clamp((int32_t)song.sel + s, 0, 3));
        if (song.sel != TRK_DRUM) {
            go_home();
            return;
        }
    }
    if ((s = panel_enc(EN_PRESET))) TDRUM->p[P_E0] = (int16_t)clamp(TDRUM->p[P_E0] + s, 0, DRUM_KITS - 1);
    for (k = 0; k < 4u; k++) if ((s = panel_enc(EN_K1 + k))) {
        ui.hot_col = (uint8_t)k;
        ui.hot_t = 40;
        if (!drum_page && ui.sheet) {
            drum_sheet_knob(k, s);
        } else if (!drum_page) {
            dstep_t *st = &TDRUM->dstep[drum_cursor];
            if (k == 0) {
                uint8_t l = (uint8_t)clamp(drum_lane + s, 0, DRUM_LANES - 1);
                if (l != drum_lane) drum_hear(l);
                drum_lane = l;
            }
            if (k == 1) {
                uint8_t c = (uint8_t)clamp(drum_cursor + s, 0, TDRUM->p[P_SLEN] - 1);
                if (c != drum_cursor && drum_preview) audition_step(&TDRUM->dstep[c]);
                drum_cursor = c;
            }
            if (k >= 2) {
                if (song.playing && arrangement_enabled) { ui_message("STOP THE SONG FIRST"); continue; }
                undo_mark(TDRUM, ui.step_sess ? ui.step_sess : (ui.step_sess = (undo_sess += 4u) | 3u));
                fm1_irq_off();
                if (k == 2) {
                    if (s > 0) dstep_set(st, drum_lane, LV_NORM, 0);
                    else dstep_clr(st, drum_lane);
                } else if (dstep_has(st, drum_lane)) {
                    uint32_t r = (uint32_t)clamp((int32_t)lvl_rank(dstep_lvl(st, drum_lane)) + (s > 0 ? 1 : -1), 0, 3);
                    dstep_set(st, drum_lane, LV_UP[r], dstep_rat(st, drum_lane));
                }
                fm1_irq_on();
                sync_reload = 1;
            }
        } else if (drum_page == 2u) {
            if (k == 0) {
                uint8_t l = (uint8_t)clamp(drum_lane + s, 0, DRUM_LANES - 1);
                if (l != drum_lane) drum_hear(l);
                drum_lane = l;
            }
            if (k == 1 || k == 2) {
                int8_t *p = k == 1 ? &drum_tune[drum_lane] : &drum_dec[drum_lane];
                int32_t lo = k == 1 ? DRUM_TUNE_MIN : DRUM_DEC_MIN, hi = k == 1 ? DRUM_TUNE_MAX : DRUM_DEC_MAX;
                int8_t nv = (int8_t)clamp(*p + s, lo, hi);
                if (nv != *p) {
                    *p = nv;
                    if (!song.playing)
                        audition_lane(drum_lane);
                }
            }
            if (k == 3) song.g[G_DRLVL] = (int16_t)clamp(song.g[G_DRLVL] + s, 0, 127);
        } else {
            if (k == 0) TDRUM->p[P_E0] = (int16_t)clamp(TDRUM->p[P_E0] + s, 0, DRUM_KITS - 1);
            if (k == 1) song.g[G_DRLVL] = (int16_t)clamp(song.g[G_DRLVL] + s, 0, 127);
            if (k == 2) song.g[G_DRREV] = (int16_t)clamp(song.g[G_DRREV] + s, 0, 127);
            if (k == 3) TDRUM->p[P_PAN] = (int16_t)clamp(TDRUM->p[P_PAN] + s, -64, 63);
        }
    }
}
/* ---------------------------------------------------------------- REC --- */
/* a big seven-segment digit, TE style: w x h, segments t thick */
UI_SMALL static void te_digit(int32_t x, int32_t y, int32_t w, int32_t h, int32_t t, uint32_t d, uint16_t c)
{
    static const uint8_t SEG[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};   /* gfedcba */
    uint32_t m = SEG[d % 10u];
    int32_t hh = h / 2;
    if (m & 0x01) cv_rect(x + t, y, w - 2 * t, t, c);                       /* a */
    if (m & 0x02) cv_rect(x + w - t, y + t, t, hh - t, c);                  /* b */
    if (m & 0x04) cv_rect(x + w - t, y + hh, t, hh - t, c);                 /* c */
    if (m & 0x08) cv_rect(x + t, y + h - t, w - 2 * t, t, c);               /* d */
    if (m & 0x10) cv_rect(x, y + hh, t, hh - t, c);                         /* e */
    if (m & 0x20) cv_rect(x, y + t, t, hh - t, c);                          /* f */
    if (m & 0x40) cv_rect(x + t, y + hh - t / 2, w - 2 * t, t, c);          /* g */
}

static uint8_t rec_shown;
/* the four tracks, compact, from y0: the one that records is framed in red */
UI_SMALL static void rec_rows(uint32_t rt, uint32_t take, uint32_t y0, uint32_t *cache)
{
    uint32_t sig = rt * 3u + rec_wait + take * 5u + drum_kit() * 977u + y0, i, j;
    for (i = 0; i < NTRK; i++) {
        char b[16];
        if (i == TRK_DRUM) str_cpy(b, DRUM_KIT_NAMES[drum_kit()], sizeof b);
        else trk_short_name(i, b);
        sig = studio_hash(sig, b) + (uint32_t)trk[i].p[P_SLEN] * 31u;
        for (j = 0; j < NSTEP; j++) sig = sig * 3u + (uint32_t)trk_step_on(&trk[i], j);
    }
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, 76, C_BLACK);
    for (i = 0; i < NTRK; i++) {
        const track_t *t = &trk[i];
        uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, 64), armed = i == rt;
        int32_t y = (int32_t)i * 19;
        char b[8 + 8];
        if (armed) cv_rect(0, y, 240, 18, TE_RED), cv_rect(1, y + 1, 238, 16, C_BLACK);
        cv_rect(4, y + 3, 12, 12, armed ? TE_COL[i] : TE_DIM[i]);
        if (i == TRK_DRUM) str_cpy(b, DRUM_KIT_NAMES[drum_kit()], sizeof b);
        else trk_short_name(i, b);
        b[5 + 5] = 0;
        cv_text(22, y + 1, &FONT_S, b, armed ? C_WHITE : TE_G3);
        for (j = 0; j < 16u; j++) {
            uint32_t a = j * len / 16u, z = (j + 1u) * len / 16u, k, on = 0;
            if (z == a) z = a + 1u;
            for (k = a; k < z && k < NSTEP; k++) if (trk_step_on(t, k)) on = 1;
            cv_rect(106 + (int32_t)j * 8, y + 5, 6, 8, on ? (armed ? TE_COL[i] : TE_DIM[i]) : TE_G1);
        }
    }
    cv_blit(0, y0);
}
/* REC armed (stopped: waiting for the first note, or PLAY), its count-in, or a free take running
 * (seq.c). Armed, the dials say how it records: KNOB 1 MODE free / tempo (an empty project),
 * KNOB 2 LENGTH (1, 2 or 4 bars), KNOB 3 START note / count, KNOB 4 QUANT on / off (2.7) (ui_input.c rec_knobs). The four tracks
 * stay in view; the REC LED blinks while armed, is lit during the take. */
UI_SMALL static void rec_screen_draw(void)
{
    return;
    static uint32_t head, body, rows, foot, dials;
    static uint8_t layout;
    uint32_t sig, take = ft_on, empty = take || project_empty(), rt = take ? ft_trk % NTRK : song.sel;
    uint32_t blink = (fm1_ms / 250u) & 1u, secs = take ? ft_t * CTL / FS : 0u, bars = 0, bpm = 0;
    uint32_t lay = take ? 1u : 2u, free = empty && !rec_tempo && !take, count = ci_on;
    if (take)
        bars = ft_fit(ft_t, &bpm);
    if (!rec_shown || lay != layout) {
        lcd_fill(0, 0, 240, 240, C_BLACK);
        ui.force = 1;
    }
    rec_shown = 1;
    layout = (uint8_t)lay;
    te_header(take ? "free take" : count ? "count-in" : "rec ready", TE_RED, &head, -1);
    if (take) {                                         /* free take: the time, the loop it makes */
        sig = 1000003u + rt * 7919u + blink * 31u + secs * 131u + bars * 17u + bpm * 3u;
        if (ui.force || sig != body) {

            char b[24];
            uint32_t n = secs > 99u ? 99u : secs;
            body = sig;
            cv_begin(240, 106, C_BLACK);
            te_disc(18, 30, 9, blink ? TE_RED : TE_DIM[3]);
            if (n >= 10u)
                te_digit(36, 4, 32, 56, 7, n / 10u, C_WHITE);
            te_digit(76, 4, 32, 56, 7, n % 10u, C_WHITE);
            cv_text(112, 44, &FONT_S, "s", TE_G3);
            cv_rect(132, 6, 1, 54, TE_G1);
            if (bars) {
                fmt_int(b, (int32_t)bars);
                cv_text(146, 4, &FONT_L, b, TE_COL[rt & 3u]);
                cv_text(146 + text_w(&FONT_L, b) + 6, 18, &FONT_S, bars == 1u ? "bar" : "bars", TE_G3);
                fmt_int(b, (int32_t)bpm);
                cv_text(146, 42, &FONT_S, b, C_WHITE);
                cv_text(146 + text_w(&FONT_S, b) + 4, 42, &FONT_S, "bpm", TE_G3);
            } else {
                cv_text(146, 4, &FONT_L, "-", TE_G3);
            }
            te_text_c(120, 80, "press rec on the 1", TE_RED);
            cv_blit(0, 42);
        }
        rec_rows(rt, take, 148u, &rows);
        if (ui.force || foot != 1u) {
            foot = 1u;
            cv_begin(240, 16, C_BLACK);
            cv_text(4, 0, &FONT_S, "rec: close", TE_G3);
            cv_text(236 - text_w(&FONT_S, "play: drop"), 0, &FONT_S, "play: drop", TE_G3);
            cv_blit(0, 224);
        }
        return;
    }
    foot = 0;
    /* armed / counting in: what happens next */
    sig = 2000003u + empty * 7u + free * 11u + rec_count * 13u + count * 17u + ci_beat * 19u + blink * 31u +
          rec_quant_off * 37u;
    if (ui.force || sig != body) {
        static const char *const L1[3] = {"play freely", "play a note", "press play"};
        static const char *const L2[3] = {"then rec on the 1", "it starts the loop", "4 clicks, then rec"};
        static const char *const L3[3] = {"the tempo follows you", "play: go  rec: cancel", "rec: cancel"};
        uint32_t m = free ? 0u : rec_count ? 2u : 1u;
        body = sig;
        cv_begin(240, 64, C_BLACK);
        if (count) {                                    /* the count-in: 4, 3, 2, 1 */
            te_digit(96, 4, 30, 54, 6, 4u - (ci_beat > 3u ? 3u : ci_beat), C_WHITE);
            te_disc(60, 31, 12, TE_RED);
            cv_text(144, 14, &FONT_S, "count-in", TE_G4);
            cv_text(144, 34, &FONT_S, "rec: cancel", TE_G3);
        } else {
            te_disc(28, 30, 18, blink ? TE_RED : TE_DIM[3]);
            te_disc(28, 30, 7, C_BLACK);
            cv_text(60, 6, &FONT_S, L1[m], C_WHITE);
            cv_text(60, 24, &FONT_S, L2[m], TE_G4);
            cv_text(60, 42, &FONT_S, L3[m], TE_G3);
        }
        cv_blit(0, 42);
    }
    rec_rows(rt, 0u, 106u, &rows);
    {   /* the dials: how it records */
        static char v[3][10];
              static const char *const LAB_E[4] = {"mode", "length", "start", "quant"};
        static const char *const LAB_F[4] = {"mode", "", "", "quant"};
        static const char *const LAB_N[4] = {"", "length", "start", "quant"};
        static const char *const LAB_0[4] = {"", "", "", ""};
        const char *val[4] = {v[0], v[1], v[2], rec_quant_off ? "off" : "on"};
        int32_t ratio[4] = {0, 0, 0, 0};
        uint32_t len = (uint32_t)clamp(TSEL->p[P_SLEN], 1, 64);
        str_cpy(v[0], rec_tempo ? "tempo" : "free", sizeof v[0]);
        if (len % 16u == 0u) {
            fmt_int(v[1], (int32_t)(len / 16u));
            str_cpy(v[1] + str_len(v[1]), len == 16u ? " bar" : " bars", 6);
        } else {
            fmt_int(v[1], (int32_t)len);
            str_cpy(v[1] + str_len(v[1]), " st", 4);
        }
        str_cpy(v[2], rec_count ? "count" : "note", sizeof v[2]);
        ratio[0] = rec_tempo ? 1000 : 0;
        ratio[1] = (int32_t)(len - 1u) * 1000 / 63;
        ratio[2] = rec_count ? 1000 : 0;
        ratio[3] = rec_quant_off ? 0 : 1000;
        {
            const char *const *lab = count ? LAB_0 : !empty ? LAB_N : free ? LAB_F : LAB_E;
            uint32_t k;
            for (k = 0; k < 4u; k++)
                if (!lab[k][0])
                    val[k] = "";
            te_dials(184, lab, val, ratio, rt * 7u + count * 3u, &dials);
        }
    }
}

/* ------------------------------------------- AUDILUNE 14-PAGE LISTS --- */
static const char *const AL_FX_ROWS[] = {
    "TIME / GROOVE", "PITCH / FORMANT", "STUTTER / GLITCH", "GRANULAR / FRZ",
    "SPATIAL DELAY", "LUSH REVERB", "MOD / STEREO", "FILTER / EQ",
    "DRIVE / LO-FI", "PUNCH / DUCK"
};
static const char *const AL_PAD_NAMES[] = {
    "DELAY PAD", "REVERB PAD", "4-CORNER VECTOR", "DRIVE & GRIT",
    "MODULATION PAD", "PITCH & TIME", "GRANULAR CLOUD", "HARMONIC STRUM"
};
static const char *const AL_FREQ_NAMES[] = {"AUTO", "OFF", "LOW", "MED"};
static const char *const AL_SCALE_NAMES[] = {
    "CHROMATIC", "MAJOR", "NAT MINOR", "HARM MINOR", "MEL MINOR", "DORIAN",
    "PHRYGIAN", "LYDIAN", "MIXOLYDIAN", "MAJ PENT", "MIN PENT", "BLUES",
    "HIRAJOSHI", "IN-SEN", "PHRYG DOM", "DBL HARM", "WHOLE TONE", "DIM H-W",
    "PROMETHEUS", "ENIGMATIC", "TRITONE HEX", "LUNAR ECLPS"
};
static const char *const AL_EDIT_ROWS[] = {
    "UNDO / REDO", "CLEAR", "COPY", "PASTE", "DUPLICATE", "QUANTIZE",
    "HUMANIZE", "NUDGE", "TRANSPOSE", "VELOCITY", "GATE", "PROBABILITY",
    "RATCHET", "REVERSE", "ROOT NOTE", "SCALE TYPE"
};
static const char AL_PUNCH_STR[] =
    "1/16 STUTTER\0TAPE/VNL BRAKE\0BUFFER REVERSE\0VINYL SPINBACK\0"
    "HALF-TIME WARP\0SUB-OCTAVE SLAM\0OCTAVE-UP LIFT\0TRANCE GATE\0"
    "TIME CRYSTAL\0GRAVITY SINGUL\0VOWEL MUTATOR\0SPECTRAL FREEZE\0"
    "CIRCUIT BITE\0REV ECHO BLOOM\0DIM PHASE TEAR\0QUANTUM PROB";
static const char *const AL_PUNCH_DIVS[] = {"FREE", "1/4", "1/8", "1/16", "1/32", "TRIP"};
static const char AL_LAND_STR[] =
    "HARMONIC DRONE\0WEATHER\0WATER & SEA\0EARTH & FIRE\0BIRDS & INSECT\0"
    "CREATURE & BIO\0HUMAN & CROWD\0TRAFFIC & CITY\0MACHINES & SPC\0SPACE & MOTION";
static const char AL_LAND_SUB_STR[] =
    "OFF\0WARM FIFTHS\0SUB MONOLITH\0SHIMMER PAD\0MYSTIC CHORD\0TAPE ORGAN\0"
    "LIGHT RAIN\0HEAVY STORM\0HIGH WIND\0HOWLING GALE\0DIST THUNDER\0HAIL GLASS\0"
    "OCEAN SURF\0DEEP UNDERWTR\0FOREST CREEK\0WATERFALL\0HARBOR WAVES\0DRIPPING CAVE\0"
    "CRACKLE FIRE\0RUMBLE EARTH\0CRUNCH GRAVEL\0DRY LEAVES\0ICE CREAKING\0VOLCANIC HUM\0"
    "DAWN CHORUS\0NIGHT CRICKET\0SUMMER CICADA\0DISTANT OWLS\0SEAGULLS\0SWAMP FROGS\0"
    "WHALE SONG\0DEEP PURR\0WOLF HOWL\0BAT SONAR\0HEARTBEAT\0SWARM BUZZ\0"
    "CAFE MURMUR\0DIST CHATTER\0FOOTSTEPS\0WHISPERING\0SUBWAY HALL\0APPLAUSE WASH\0"
    "DIST HIGHWAY\0RAINY STREET\0PASSING CARS\0TRAIN RAILS\0SIREN FAR OFF\0CITY HUM\0"
    "TAPE HISS\0VINYL CRACKLE\0CLOCKWORK TCK\0HEAVY MACHINE\0METAL CLANG\0RADIO STATIC\0SOLAR WIND\0STARSHIP HUM\0"
    "STATIC CENTER\0SLOW DRIFT\0WIDE BINAURAL\0WIND GUSTS\0DEEP CAVERN\0INFINITE WASH";
UI_SMALL static const char *al_nth_str(const char *s, uint32_t n)
{
    while (n--) while (*s++) ;
    return s;
}
UI_SMALL static const char *al_land_sub_name(uint32_t row, uint32_t sub)
{
    uint32_t base = row < 8u ? row * 6u : row == 8u ? 48u : 56u;
    uint32_t cnt = row == 8u ? 8u : 6u;
    return al_nth_str(AL_LAND_SUB_STR, base + (sub % cnt));
}
static uint8_t al_punch_div[8 + 8], al_punch_val[8 + 8], al_punch_active, al_punch_slot;
static uint16_t al_land_on;
static uint8_t al_land_sub[5 + 5], al_land_vol[5 + 5];
static const char *const AL_ENV_ROWS[] = {"ATTACK", "DECAY", "SUSTAIN", "RELEASE"};
static const char *const AL_LFO_ROWS[] = {"WAVEFORM", "RATE", "DEPTH", "DESTINATION"};
static const char *const AL_GLO_ROWS[] = {"COLOR THEME", "FREQ LAYER", "HELPER NOTES", "MIDI CH", "MIDI ROUTE", "CLOCK SYNC", "USB AUDIO", "STUDIO SETUP"};
static const char *const AL_SETUP_ROWS[] = {
    "READY -> PLAY SCREEN", "SONG VAULT", "SESSION MODE", "TIMING / GRAVITY",
    "FREQ LAYER", "HELPER NOTES", "AUTO POWER OFF", "SESSION TIMER",
    "TEMPO", "ROOT NOTE", "SCALE TYPE", "PATTERN LENGTH", "MIDI / CLOCK SYNC"
};
static const char *const AL_SESS_NAMES[] = {"STUDIO", "SLEEP", "MEDITATE", "STUDY FOCUS", "CHILL", "DANCE"};
static const char *const AL_TIMER_NAMES[] = {"OFF", "15 MIN", "30 MIN", "45 MIN", "60 MIN", "90 MIN", "120 MIN"};
static const char *const AL_NOTE_NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const AL_CHOP_MODES[] = {"4 EQUAL", "8 EQUAL", "16 EQUAL", "AUTO TRANSIENT", "MANUAL"};
static uint8_t al_fx_sub[5 + 5], al_fx_val[5 + 5], al_chop_chrom;
static uint8_t al_pad_list_mode;
static uint8_t al_pad_z[4 + 4] = {64, 64, 64, 64, 64, 64, 64, 64}, al_vec_corner[2 + 2] = {0, 1, 2, 3};
static uint8_t al_vec_sel, al_seq_ovw, al_seq_ovw_trk, al_seq_ovw_step;
static uint8_t al_gravity = 50u, al_helper_notes = 1u, al_chop_sens = 50u, al_chop_trim_s = 0u, al_chop_trim_e = 100u;
UI_SMALL static void audilune_hud_popup(const char *label, int32_t val, const char *unit);
UI_SMALL static void audilune_hud_text(const char *prefix, const char *value);
static uint8_t audilune_theme_idx, al_arp_mode[3 + 3], al_clip_ok;
UI_SMALL static uint32_t audilune_page_rows(uint8_t scr)
{
    if (scr == UI_FX) return al_pad_list_mode ? 1u : 18u;

    if (scr == UI_PUNCH) return 16u;
    if (scr == UI_ENV || scr == UI_LFO) return 4u;
    if (scr == UI_LANDSCAPE) return 10u;
    if (scr == UI_EDIT) return 16u;
    if (scr == UI_ARP) return 7u;
    if (scr == UI_GLO) return 8u;
    if (scr == UI_SAVE) return 6u;
    if (scr == UI_SEQ) return 8u;
    if (scr == UI_CHOP_SRC) return 4u;
    if (scr == UI_CHOP_WAVE) return 21u;
    if (scr == UI_ALGO) return 6u + DRUM_KITS;

    if (scr == UI_SETUP) return 13u;
    return 0u;
}

UI_SMALL static void audilune_fx_apply(track_t *t, uint32_t row, uint8_t sub, uint8_t val)
{
    uint32_t rec_on = song.playing && ((song.rec >> song.sel) & 1u);
    uint32_t step = t->seq_idx % trk_len(t);
    int32_t pid = -1;

    if (row == 0u || row == 2u || row == 3u) {

        static const int8_t T_PF[4 + 4] = {PX_HALF, PX_LOOP8, PX_STOP, PX_REV, PX_OCT, PX_RWD, PX_LOOP8T, PX_STUT};
        static const int8_t G_PF[4 + 4] = {PX_LOOP16, PX_LOOP32, PX_LOOP8T, PX_STUT, PX_CRUSH, PX_DOWN, PX_GATE, PX_FREEZE};
        punch.req = val ? (row == 0u ? *(T_PF + (sub & 7u)) : row == 2u ? *(G_PF + (sub & 7u)) : PX_FREEZE) : -1;
    } else if (row == 1u) {
        static int8_t last_st;

        uint32_t has_step = (*(ui.sel_step_mask + 0) | *(ui.sel_step_mask + 1) | *(ui.sel_step_mask + 2) | *(ui.sel_step_mask + 3)) != 0u;
        uint32_t trk_mask = ui.sel_trk_mask ? ui.sel_trk_mask : (1u << song.sel);
        uint32_t ti, si, nj, len = trk_len(t);
        if (sub < 4u) {
            int32_t st = clamp(((int32_t)val - 55) * 24 / 55, -24, 24);
            int32_t delta = st - (int32_t)last_st;
            last_st = (int8_t)st;
            if (has_step && !is_drum(t)) {

                for (si = 0; si < len; si++)
                    if (((ui.sel_step_mask[si >> 5] >> (si & 31u)) & 1u) && step_on(&t->step[si]))
                        for (nj = 0; nj < t->step[si].n && nj < 4u; nj++)
                            t->step[si].note[nj] = (uint8_t)clamp((int32_t)t->step[si].note[nj] + delta, 0, 127);
            } else {
                song.g[G_TUNE] = (int16_t)st;
                for (ti = 0; ti < NTRK; ti++)
                    if ((trk_mask >> ti) & 1u && song.playing && ((song.rec >> ti) & 1u) && !is_drum(&trk[ti])) {
                        uint32_t cs = trk[ti].seq_idx % trk_len(&trk[ti]);
                        if (step_on(&trk[ti].step[cs]))
                            for (nj = 0; nj < trk[ti].step[cs].n && nj < 4u; nj++)
                                trk[ti].step[cs].note[nj] = (uint8_t)clamp((int32_t)trk[ti].step[cs].note[nj] + delta, 0, 127);
                    }
                if (is_drum(t))
                    for (si = 0; si < DRUM_LANES; si++)
                        drum_tune[si] = (int8_t)clamp(st, DRUM_TUNE_MIN, DRUM_TUNE_MAX);
            }
        } else {
            int32_t drift = clamp((int32_t)val * 127 / 110, 0, 127);
            int16_t rate = (int16_t)(10 + (sub & 3u) * 8);
            if (has_step) {
                for (si = 0; si < len; si++)
                    if ((ui.sel_step_mask[si >> 5] >> (si & 31u)) & 1u)
                        lock_set(t, si, P_LD_PIT, (int16_t)drift);
            } else {
                for (ti = 0; ti < NTRK; ti++)
                    if ((trk_mask >> ti) & 1u) {
                        trk[ti].p[P_LWAVE] = 0;

                        trk[ti].p[P_LRATE] = rate;
                        trk[ti].p[P_LD_PIT] = (int16_t)drift;
                        if (song.playing && ((song.rec >> ti) & 1u))
                            lock_set(&trk[ti], trk[ti].seq_idx % trk_len(&trk[ti]), P_LD_PIT, (int16_t)drift);
                    }
            }
        }
    } else if (row == 4u) {
        pid = P_DLY;
        song.g[G_DTIME] = (int16_t)((sub & 7u) * 2 + 1);
        t->p[P_DLY] = (int16_t)((int32_t)val * 140 / 110);
    } else if (row == 5u) {
        pid = P_REV;
        song.g[G_RSIZE] = (int16_t)(40 + (sub & 7u) * 12);
        t->p[P_REV] = (int16_t)((int32_t)val * 140 / 110);
     } else if (row == 6u) {
        static const uint8_t M_IT[4 + 4] = {IT_CHORUS, IT_CHORUS, IT_FLANGER, IT_PHASER, IT_CHORUS, IT_FLANGER, IT_PHASER, IT_CHORUS};
        pid = P_IMIX;
        t->p[P_ITYPE] = *(M_IT + (sub & 7u));
        t->p[P_IMIX] = (int16_t)((int32_t)val * 127 / 110);
    } else if (row == 7u) {
        pid = P_TFLT;
        t->p[P_TFLT] = (int16_t)((sub & 1u) ? ((int32_t)val * 63 / 110) : -((int32_t)val * 64 / 110));
    } else if (row == 8u) {
        pid = P_DIST;
        t->p[P_DIST] = (int16_t)((int32_t)val * 127 / 110);
        song.g[G_DUST] = (int16_t)((sub >= 4u) ? ((int32_t)val * 127 / 110) : 0);
    } else if (row == 9u) {
        song.g[G_DUCK] = (int16_t)((int32_t)val * 127 / 110);
    }
    if (rec_on && pid >= 0)
        lock_set(t, step, (uint32_t)pid, t->p[pid]);
}

UI_SMALL static void audilune_sel_vol(int32_t s)
{
    track_t *t = TSEL;
    uint32_t len = trk_len(t), i, j, has_step = (*(ui.sel_step_mask + 0) | *(ui.sel_step_mask + 1) | *(ui.sel_step_mask + 2) | *(ui.sel_step_mask + 3)) != 0u;
    if (has_step) {
        int32_t last_v = 0;
        for (i = 0; i < len; i++) {
            if (!((ui.sel_step_mask[i >> 5] >> (i & 31u)) & 1u)) continue;
            if (is_drum(t)) {
                for (j = 0; j < DRUM_LANES; j++)
                    if (dstep_has(&t->dstep[i], j)) {
                        uint32_t r = (uint32_t)clamp((int32_t)lvl_rank(dstep_lvl(&t->dstep[i], j)) + (s > 0 ? 1 : -1), 0, 3);
                        dstep_set(&t->dstep[i], j, LV_UP[r], dstep_rat(&t->dstep[i], j));
                        last_v = (int32_t)(r + 1u) * 25;
                    }
            } else if (step_on(&t->step[i])) {
                int32_t nv = clamp((int32_t)(t->step[i].vel ? t->step[i].vel : 96) + s * 4, 1, 127);
                t->step[i].vel = (uint8_t)nv;
                last_v = nv * 100 / 127;
            }
        }
        audilune_hud_popup("STEP LVL", last_v, "%");
    } else {
        uint32_t m = (ui.sel_trk_mask & 0x3Fu) | (1u << (song.sel % 6u));
        int32_t lv = 0;
        for (i = 0; i < NTRK; i++)
            if ((m >> i) & 1u) {
                int16_t *vp = (!trk[i].synth_mode && i == TRK_DRUM) ? &song.g[G_DRLVL] : &trk[i].p[P_LEVEL];
                *vp = (int16_t)clamp((int32_t)(*vp) + s * 3, 0, 127);
                if (i == song.sel) lv = (int32_t)(*vp) * 100 / 127;
            }
        audilune_hud_popup("SEL LVL", lv, "%");
    }
}

UI_SMALL static void audilune_gravity_apply(uint8_t gv)
{
    track_t *t = TSEL;
    uint32_t len = trk_len(t), i, has_step = (*(ui.sel_step_mask + 0) | *(ui.sel_step_mask + 1) | *(ui.sel_step_mask + 2) | *(ui.sel_step_mask + 3)) != 0u;
    int32_t off = ((int32_t)gv - 50) * MICRO_MAX / 50;
    for (i = 0; i < len; i++) {
        if (has_step && !((ui.sel_step_mask[i >> 5] >> (i & 31u)) & 1u)) continue;
        int32_t spread = gv <= 50u ? 0 : ((int32_t)((i * 37u + 11u) % 25u) - 12) * ((int32_t)gv - 50) / 50;
        t->micro[i] = (int8_t)clamp(has_step ? off : spread, MICRO_MIN, MICRO_MAX);
    }
    sync_reload = 1;
}

UI_SMALL static void audilune_punch_apply(uint32_t slot, int on)
{
    static const int8_t P_MAP[] = {
        PX_LOOP16, PX_STOP, PX_REV, PX_RWD, PX_HALF, PX_DOWN, PX_OCT, PX_GATE,
        PX_LOOP32, PX_LOOP8T, PX_STUT, PX_FREEZE, PX_CRUSH, PX_REV, PX_LOOP8, PX_STUT
    };
    uint32_t idx = slot & 15u;
    al_punch_slot = (uint8_t)idx;
    al_punch_active = (uint8_t)(on ? 1u : 0u);
    punch.req = on ? *(P_MAP + idx) : -1;
}

UI_SMALL static void audilune_landscape_apply(void)
{
    uint32_t i, active = 0u, tot_vol = 0u;
    for (i = 0u; i < 10u; i++) {
        if (((al_land_on >> i) & 1u) && al_land_vol[i] > 0u) {
            active++;
            tot_vol += al_land_vol[i];
        }
    }
    song.g[G_DUST] = (int16_t)clamp((int32_t)(tot_vol / (active ? active : 1u)) * 127 / 100, 0, 127);
    if ((al_land_on & 1u) && al_land_sub[0] > 0u) {
        TDRUM->p[P_REV] = (int16_t)clamp((int32_t)al_land_vol[0] * 120 / 100, 0, 127);
    }
}

UI_SMALL static void audilune_chaos_apply(track_t *t);
UI_SMALL static void audilune_list_knob(uint32_t k, int32_t s)
{
    uint32_t rows = audilune_page_rows(ui.scr);
    uint32_t row = rows ? (uint32_t)ui.list_sel % rows : 0u;
    track_t *t = TSEL;
    if (k < 2u || !rows)
        return;
    ui.hot_col = (uint8_t)k;
    ui.hot_t = 40;
    if (ui.scr == UI_PUNCH) {
        if (k == 3u) {
            al_punch_div[row] = (uint8_t)((al_punch_div[row] + (s > 0 ? 1u : 5u)) % 6u);
            audilune_hud_text("GRID", *(AL_PUNCH_DIVS + al_punch_div[row]));
        } else {
            int32_t nv = clamp((int32_t)al_punch_val[row] + s * 4, 0, 110);
            al_punch_val[row] = (uint8_t)nv;
            audilune_hud_popup(al_nth_str(AL_PUNCH_STR, row), nv, "%");
        }
    } else if (ui.scr == UI_LANDSCAPE) {
        if (k == 3u) {
            al_land_sub[row] = (uint8_t)((al_land_sub[row] + (s > 0 ? 1u : 7u)) & 7u);
            audilune_landscape_apply();
            audilune_hud_text(al_nth_str(AL_LAND_STR, row), al_land_sub_name(row, al_land_sub[row]));
        } else {
            int32_t nv = clamp((int32_t)al_land_vol[row] + s * 4, 0, 100);
            al_land_vol[row] = (uint8_t)nv;
            if (nv > 0) al_land_on |= (uint16_t)(1u << row);
            audilune_landscape_apply();
            audilune_hud_popup(al_nth_str(AL_LAND_STR, row), nv, "%");
        }
    } else if (ui.scr == UI_FX && al_pad_list_mode) {
        uint32_t p = al_pad_idx & 7u;
        if (k == 3u && p == 5u) {
            al_gravity = (al_gravity == 0u) ? 50u : 0u;

            al_pad_x[p] = al_gravity ? 127u : 0u;
            audilune_gravity_apply(al_gravity);
            audilune_hud_text("GRAVITY", al_gravity ? "UNQUANTIZED" : "QUANTIZED");
        } else if (k == 3u) {
            al_pad_x[p] = (uint8_t)clamp((int32_t)al_pad_x[p] + s * 4, 0, 127);
             audilune_chaos_apply(t);
            audilune_hud_popup("PAD X", (int32_t)al_pad_x[p] * 100 / 127, "%");
        } else if (p == 5u) {
            al_gravity = (uint8_t)clamp((int32_t)al_gravity + s * 4, 0, 100);
            al_pad_z[p] = (uint8_t)(al_gravity * 127u / 100u);
            audilune_gravity_apply(al_gravity);
            audilune_hud_popup("GRAVITY", (int32_t)al_gravity, "%");
        } else {
            al_pad_z[p] = (uint8_t)clamp((int32_t)al_pad_z[p] + s * 4, 0, 127);
            audilune_chaos_apply(t);
            audilune_hud_popup("WET/MIX", (int32_t)al_pad_z[p] * 110 / 127, "%");
        }
    } else if (ui.scr == UI_FX) {
        if (row < 8u) {
            if (k == 2u) audilune_sel_vol(s);
        } else {
            uint32_t fxr = row - 8u;
            if (k == 3u) {
                al_fx_sub[fxr] = (uint8_t)((al_fx_sub[fxr] + (s > 0 ? 1u : 7u)) & 7u);
                if (fxr == 1u && al_fx_sub[fxr] < 4u && al_fx_val[fxr] == 0u) al_fx_val[fxr] = 55u;
                audilune_fx_apply(t, fxr, al_fx_sub[fxr], al_fx_val[fxr]);
                audilune_hud_popup(AL_FX_ROWS[fxr], (int32_t)(al_fx_sub[fxr] + 1u), "");
            } else {
                int32_t nv = clamp((int32_t)al_fx_val[fxr] + s * 4, 0, 110);
                al_fx_val[fxr] = (uint8_t)nv;
                audilune_fx_apply(t, fxr, al_fx_sub[fxr], al_fx_val[fxr]);
                audilune_hud_popup(AL_FX_ROWS[fxr], nv, "%");
            }

        }
    } else if (ui.scr == UI_ENV && k == 2u) {
        uint32_t pid = P_ATK + row;
        t->p[pid] = (int16_t)clamp(t->p[pid] + s * 3, 0, 127);
        audilune_hud_popup(AL_ENV_ROWS[row], t->p[pid] * 100 / 127, "%");
    } else if (ui.scr == UI_LFO) {
        static const uint32_t LFO_P[2 + 2] = {P_LWAVE, P_LRATE, P_LD_PIT, P_LD_FLT};
        uint32_t pid = *(LFO_P + row);
        t->p[pid] = (int16_t)clamp(t->p[pid] + s * (k == 2u ? 3 : 1), 0, 127);
        audilune_hud_popup(AL_LFO_ROWS[row], t->p[pid], "");
    } else if (ui.scr == UI_EDIT) {

        enum { CLIP_STEPS = 32u };


        static uint8_t clip_drum;
        static int16_t clip_len;
        static union { step_t st[CLIP_STEPS + 0]; dstep_t dst[CLIP_STEPS + 0]; } clip_buf;
        static int8_t clip_micro[CLIP_STEPS + 0];

        static uint8_t clip_fill[CLIP_STEPS / 4];

        uint32_t len = trk_len(t), i, j, has_sel = (*(ui.sel_step_mask + 0) | *(ui.sel_step_mask + 1) | *(ui.sel_step_mask + 2) | *(ui.sel_step_mask + 3)) != 0u;
        int32_t dir = s > 0 ? 1 : -1;
        if (row == 0u && k == 3u) {
            ui.edit_redo_dir = (uint8_t)(!ui.edit_redo_dir);
            audilune_hud_text("ACTION", ui.edit_redo_dir ? "REDO" : "UNDO");
        } else if (row == 2u) {
            al_clip_ok = 1u;
            clip_drum = (uint8_t)is_drum(t);
            clip_len = t->p[P_SLEN];
            if (clip_drum)
                memcpy(clip_buf.dst, t->dstep, sizeof clip_buf.dst);
            else
                memcpy(clip_buf.st, t->step, sizeof clip_buf.st);
            memcpy(clip_micro, t->micro, sizeof clip_micro);
            memcpy(clip_fill, t->fill, sizeof clip_fill);
            audilune_hud_text("COPY", "PATTERN");
        } else if (row == 3u) {
            if (!al_clip_ok) {
                audilune_hud_text("PASTE", "EMPTY");
            } else {
                undo_mark(t, (undo_sess += 4u) | 3u);
                fm1_irq_off();
                if (is_drum(t) && clip_drum)
                    memcpy(t->dstep, clip_buf.dst, sizeof clip_buf.dst);
                else if (!is_drum(t) && !clip_drum)
                    memcpy(t->step, clip_buf.st, sizeof clip_buf.st);
                memcpy(t->micro, clip_micro, sizeof clip_micro);

                memcpy(t->fill, clip_fill, sizeof clip_fill);
                t->p[P_SLEN] = clip_len;
                t->seq_active = 1;
                fm1_irq_on();
                sync_reload = 1;
                audilune_hud_text("PASTE", "DONE");
            }
        } else if (row == 4u) {
            if (len * 2u > NSTEP) {
                audilune_hud_text("DUPLICATE", "MAX 128");

            } else {
                undo_mark(t, (undo_sess += 4u) | 3u);
                fm1_irq_off();
                for (i = 0; i < len; i++) {
                    t->step[len + i] = t->step[i];
                    t->dstep[len + i] = t->dstep[i];
                    t->micro[len + i] = t->micro[i];
                    step_fill_set(t, len + i, step_fill(t, i));
                }

                t->p[P_SLEN] = (int16_t)(len * 2u);
                fm1_irq_on();
                sync_reload = 1;
                audilune_hud_popup("LEN", (int32_t)(len * 2u), " ST");
            }
        } else if (row == 5u) {
            undo_mark(t, (undo_sess += 4u) | 3u);
            fm1_irq_off();
            for (i = 0; i < len; i++)
                if (!has_sel || ((ui.sel_step_mask[i >> 5] >> (i & 31u)) & 1u))
                    t->micro[i] = 0;
            fm1_irq_on();
            sync_reload = 1;
            audilune_hud_text("QUANTIZE", "100%");
        } else if (row == 6u) {
            undo_mark(t, (undo_sess += 4u) | 3u);
            fm1_irq_off();
            for (i = 0; i < len; i++) {
                if (has_sel && !((ui.sel_step_mask[i >> 5] >> (i & 31u)) & 1u))
                    continue;
                int32_t jit = (int32_t)((i * 37u + 13u) % 15u) - 7;
                t->micro[i] = (int8_t)clamp((int32_t)t->micro[i] + jit, MICRO_MIN, MICRO_MAX);
                if (!is_drum(t) && step_on(&t->step[i]))
                    t->step[i].vel = (uint8_t)clamp((int32_t)(t->step[i].vel ? t->step[i].vel : 96) + jit * 2, 24, 127);
            }
            fm1_irq_on();
            sync_reload = 1;
            audilune_hud_text("HUMANIZE", "+VAR");
        } else if (row == 7u) {
            undo_mark(t, (undo_sess += 4u) | 3u);
            fm1_irq_off();
            for (i = 0; i < len; i++)
                if (!has_sel || ((ui.sel_step_mask[i >> 5] >> (i & 31u)) & 1u))
                    t->micro[i] = (int8_t)clamp((int32_t)t->micro[i] + dir * 2, MICRO_MIN, MICRO_MAX);
            fm1_irq_on();
            sync_reload = 1;
            audilune_hud_popup("NUDGE", dir * 2, "");
        } else if (row == 8u) {
            undo_mark(t, (undo_sess += 4u) | 3u);
            fm1_irq_off();
            if (is_drum(t)) {
                for (i = 0; i < DRUM_LANES; i++)
                    drum_tune[i] = (int8_t)clamp((int32_t)drum_tune[i] + dir, DRUM_TUNE_MIN, DRUM_TUNE_MAX);
            } else {
                for (i = 0; i < len; i++)
                    if ((!has_sel || ((ui.sel_step_mask[i >> 5] >> (i & 31u)) & 1u)) && step_on(&t->step[i]))
                        for (j = 0; j < t->step[i].n && j < 4u; j++)
                            t->step[i].note[j] = (uint8_t)clamp((int32_t)t->step[i].note[j] + dir, 0, 127);
            }
            fm1_irq_on();
            sync_reload = 1;
            audilune_hud_popup("TRANSPOSE", dir, " ST");
        } else if (row == 9u) {
            undo_mark(t, (undo_sess += 4u) | 3u);
            fm1_irq_off();
            for (i = 0; i < len; i++) {
                if (has_sel && !((ui.sel_step_mask[i >> 5] >> (i & 31u)) & 1u))
                    continue;
                if (is_drum(t)) {
                    for (j = 0; j < DRUM_LANES; j++)
                        if (dstep_has(&t->dstep[i], j)) {
                            uint32_t r = (uint32_t)clamp((int32_t)lvl_rank(dstep_lvl(&t->dstep[i], j)) + dir, 0, 3);
                            dstep_set(&t->dstep[i], j, LV_UP[r], dstep_rat(&t->dstep[i], j));
                        }
                } else if (step_on(&t->step[i])) {
                    int32_t nv = clamp((int32_t)(t->step[i].vel ? t->step[i].vel : 96) + dir * 6, 16, 127);
                    t->step[i].vel = (uint8_t)nv;
                }
            }
            fm1_irq_on();
            sync_reload = 1;
            audilune_hud_popup("VELOCITY", dir * 6, "");
        } else if (row == 10u) {
            t->p[P_SGATE] = (int16_t)clamp(t->p[P_SGATE] + dir * 4, 1, 127);
            audilune_hud_popup("GATE", t->p[P_SGATE] * 100 / 127, "%");
        } else if (row == 11u) {
            t->p[P_APROB] = (int16_t)clamp(t->p[P_APROB] + dir * 4, 0, 127);
            audilune_hud_popup("PROB", t->p[P_APROB] * 100 / 127, "%");
        } else if (row == 12u) {
            uint32_t nr = 0;
            undo_mark(t, (undo_sess += 4u) | 3u);
            fm1_irq_off();
            for (i = 0; i < len; i++) {
                if (has_sel && !((ui.sel_step_mask[i >> 5] >> (i & 31u)) & 1u))
                    continue;
                if (is_drum(t)) {
                    for (j = 0; j < DRUM_LANES; j++)
                        if (dstep_has(&t->dstep[i], j)) {
                            nr = (uint32_t)clamp((int32_t)dstep_rat(&t->dstep[i], j) + dir, 0, 3);
                            dstep_set(&t->dstep[i], j, dstep_lvl(&t->dstep[i], j), nr);
                        }
                } else if (step_on(&t->step[i])) {
                    nr = (uint32_t)clamp((int32_t)(t->step[i].rat & 3u) + dir, 0, 3);
                    t->step[i].rat = (uint8_t)(nr * 0x55u);
                }
            }
            fm1_irq_on();
            sync_reload = 1;
            audilune_hud_popup("RATCHET", (int32_t)nr + 1, "X");
        } else if (row == 13u) {
            undo_mark(t, (undo_sess += 4u) | 3u);
            fm1_irq_off();
            for (i = 0; i < len / 2u; i++) {
                uint32_t ri = len - 1u - i;
                step_t ts = t->step[i];
                dstep_t ds = t->dstep[i];
                int8_t tm = t->micro[i];
                uint32_t tf = step_fill(t, i);
                t->step[i] = t->step[ri];
                t->step[ri] = ts;
                t->dstep[i] = t->dstep[ri];
                t->dstep[ri] = ds;
                t->micro[i] = t->micro[ri];
                t->micro[ri] = tm;
                step_fill_set(t, i, step_fill(t, ri));
                step_fill_set(t, ri, tf);
            }
            fm1_irq_on();
            sync_reload = 1;
            audilune_hud_text("REVERSE", "DONE");
        } else if (row == 14u) {
            int32_t nr = (t->p[P_ROOT] + (s > 0 ? 1 : 11)) % 12;

            t->p[P_ROOT] = (int16_t)nr;
            audilune_hud_text("ROOT", *(AL_NOTE_NAMES + nr));
        } else if (row == 15u) {
            int32_t ns = (t->p[P_SCALE] + (s > 0 ? 1 : 21)) % 22;
            t->p[P_SCALE] = (int16_t)ns;
            audilune_hud_text("SCALE", *(AL_SCALE_NAMES + ns));
        }
    } else if (ui.scr == UI_GLO) {
        if (k == 2u) {
            audilune_sel_vol(s);
        } else if (row == 0u) {
            static const char *const AL_THM_H[] = {"TEAL", "ORANGE", "BURGUNDY", "PURPLE", "PINK", "BLUE", "MINT", "GREY"};
            audilune_theme_idx = (uint8_t)((audilune_theme_idx + (s > 0 ? 1u : 7u)) & 7u);
            palette_set(audilune_theme_idx);
            audilune_hud_text("THEME", *(AL_THM_H + audilune_theme_idx));
        } else if (row == 1u) {
            audilune_freq_layer = (uint8_t)((audilune_freq_layer + (s > 0 ? 1u : 3u)) & 3u);
            audilune_hud_text("FREQ LYR", *(AL_FREQ_NAMES + audilune_freq_layer));
        } else if (row == 2u) {
            al_helper_notes = (uint8_t)(!al_helper_notes);
            audilune_hud_text("HELPER", al_helper_notes ? "ON" : "OFF");
        } else if (row == 5u) {
            song.g[G_SYNC] = (int16_t)((song.g[G_SYNC] + (s > 0 ? 1 : 2)) % 3);
            audilune_hud_text("SYNC", song.g[G_SYNC] == 1 ? "USB" : song.g[G_SYNC] == 2 ? "MIDI" : "INT");
        } else if (row == 7u) {
            audilune_blank_studio_init();
            audilune_hud_text("STUDIO", "SETUP");
        }
    } else if (ui.scr == UI_SAVE) {
        if (ui.chop_mode != 0xFF && ui.list_top == 1u) {
            if (row == 0u) {
                ui.chop_save_usr = (uint8_t)((ui.chop_save_usr + (s > 0 ? 1u : 6u)) % 7u);
                audilune_hud_popup("VAULT USR", (int32_t)ui.chop_save_usr, "");
            } else {
                ui.chop_load_trk = (uint8_t)((ui.chop_load_trk + (s > 0 ? 1u : 6u)) % 7u);
                audilune_hud_popup("LOAD TRK", (int32_t)ui.chop_load_trk, "");
            }
        } else if (k == 2u) {
            audilune_sel_vol(s);
        } else {
            ui.save_vault = (uint8_t)(row % 6u);
            if (k == 3u) ui.save_op = (uint8_t)(!ui.save_op);
            audilune_hud_text(ui.save_op ? "LOAD SLOT" : "SAVE SLOT", "READY");
        }
    } else if (ui.scr == UI_ARP) {
        static const char *const AL_SCOPE_H[] = {"SONG", "TRACK", "STEP", "PART A->B", "SONG DJ"};
        if (k == 2u || row == 3u) {

            int32_t nm = clamp((int32_t)audilune_gen_morph + s * 4, 0, 100);
            audilune_gen_morph = (uint8_t)nm;
            audilune_hud_popup("MORPH", nm, "%");
        } else if (row == 0u) {
            audilune_infinite_on = (uint8_t)(!audilune_infinite_on);
            audilune_hud_text("ENGINE", audilune_infinite_on ? "ON" : "OFF");
        } else if (row == 1u) {
            ui.session_mode = (uint8_t)((ui.session_mode + (s > 0 ? 1u : 5u)) % 6u);
            audilune_session_mode = ui.session_mode;
            audilune_hud_text("MODE", *(AL_SESS_NAMES + ui.session_mode));
        } else if (row == 2u) {
            audilune_gen_scope = (uint8_t)((audilune_gen_scope + (s > 0 ? 1u : 4u)) % 5u);
            ui.morph_scope = audilune_gen_scope;
            audilune_hud_text("SCOPE", *(AL_SCOPE_H + audilune_gen_scope));
        } else if (row == 6u) {
            audilune_freq_layer = (uint8_t)((audilune_freq_layer + (s > 0 ? 1u : 3u)) & 3u);
            audilune_hud_text("FREQ LYR", *(AL_FREQ_NAMES + audilune_freq_layer));
        } else if (row == 4u) {
            uint8_t ng = (uint8_t)((audilune_genre + (s > 0 ? 1u : 5u)) % 6u);
            audilune_genre = ng;
            if (song.sel < NTRK) audilune_trk_genre[song.sel] = ng;

            audilune_hud_text("GENRE", *(AUDILUNE_GENRE_NAMES + ng));
        } else if (row == 5u) {
            if (audilune_gen_scope == 4u) {
                ui.save_vault = (uint8_t)((ui.save_vault + (s > 0 ? 1u : 5u)) % 6u);
                audilune_hud_popup("DJ VAULT", (int32_t)ui.save_vault + 1, "");
            } else if (audilune_gen_scope == 2u) {
                uint32_t max_s = trk_len(t) > 64u ? 64u : trk_len(t);
                audilune_active_step = (uint8_t)((audilune_active_step + (s > 0 ? 1u : max_s - 1u)) % max_s);
                audilune_sel_step_mask[song.sel] ^= (1u << (audilune_active_step & 31u));
                audilune_hud_popup("STEP", (int32_t)audilune_active_step + 1, "");
            } else {
                uint32_t nt = (uint32_t)((song.sel + (s > 0 ? 1u : 5u)) % NTRK);
                track_select(nt);
                audilune_active_trk = (uint8_t)nt;
                audilune_hud_popup("TRACK", (int32_t)nt + 1, "");
            }
        }
    } else if (ui.scr == UI_SETUP) {
        uint32_t ti;
        if (row == 1u) {
            ui.setup_vault = (uint8_t)((ui.setup_vault + (s > 0 ? 1u : 6u)) % 7u);
            if (ui.setup_vault == 0u) {
                for (ti = 0; ti < NTRK; ti++) { steps_clear(&trk[ti]); trk[ti].seq_active = 0; }
                audilune_hud_text("VAULT", "BLANK");
            } else {
                project_load(ui.setup_vault - 1u);
                audilune_hud_popup("VAULT SLOT", (int32_t)ui.setup_vault, "");
            }
        } else if (row == 2u) {
            ui.session_mode = (uint8_t)((ui.session_mode + (s > 0 ? 1u : 5u)) % 6u);
            audilune_session_mode = ui.session_mode;
            audilune_hud_text("MODE", *(AL_SESS_NAMES + ui.session_mode));
        } else if (row == 3u) {
            al_gravity = (al_gravity == 0u) ? 50u : 0u;
            audilune_gravity_apply(al_gravity);
            audilune_hud_text("GRAVITY", al_gravity ? "UNQUANTIZED" : "QUANTIZED");
        } else if (row == 4u) {
            audilune_freq_layer = (uint8_t)((audilune_freq_layer + (s > 0 ? 1u : 3u)) & 3u);
            audilune_hud_text("FREQ LYR", *(AL_FREQ_NAMES + audilune_freq_layer));
        } else if (row == 5u) {
            al_helper_notes = (uint8_t)(!al_helper_notes);
            audilune_hud_text("HELPER", al_helper_notes ? "ON" : "OFF");
        } else if (row == 6u) {
            ui.auto_power_off = (uint8_t)((ui.auto_power_off + (s > 0 ? 1u : 6u)) % 7u);
            audilune_hud_text("AUTO OFF", *(AL_TIMER_NAMES + ui.auto_power_off));
        } else if (row == 7u) {
            ui.session_timer = (uint8_t)((ui.session_timer + (s > 0 ? 1u : 6u)) % 7u);
            audilune_hud_text("TIMER", *(AL_TIMER_NAMES + ui.session_timer));
        } else if (row == 8u) {
            song.g[G_BPM] = (int16_t)clamp(song.g[G_BPM] + s, GP[G_BPM].min, GP[G_BPM].max);
            audilune_hud_popup("TEMPO", song.g[G_BPM], "");
        } else if (row == 9u) {
            int32_t nr = ((*(trk + 0)).p[P_ROOT] + (s > 0 ? 1 : 11)) % 12;
            audilune_root_note = (uint8_t)nr;
            for (ti = 0; ti < NTRK; ti++) trk[ti].p[P_ROOT] = (int16_t)nr;
            audilune_hud_text("ROOT", *(AL_NOTE_NAMES + nr));
        } else if (row == 10u) {
            int32_t ns = ((*(trk + 0)).p[P_SCALE] + (s > 0 ? 1 : 21)) % 22;
            for (ti = 0; ti < NTRK; ti++) trk[ti].p[P_SCALE] = (int16_t)ns;
            audilune_hud_text("SCALE", *(AL_SCALE_NAMES + ns));
        } else if (row == 11u) {
            int32_t nl = clamp(((int32_t)(*(trk + 0)).p[P_SLEN] / 16 + (s > 0 ? 1 : -1)) * 16, 16, NSTEP);
            for (ti = 0; ti < NTRK; ti++) trk[ti].p[P_SLEN] = (int16_t)nl;
            audilune_hud_popup("LENGTH", nl, "");
        } else if (row == 12u) {
            song.g[G_SYNC] = (int16_t)((song.g[G_SYNC] + (s > 0 ? 1 : 2)) % 3);
            audilune_hud_text("SYNC", song.g[G_SYNC] == 1 ? "USB-C" : song.g[G_SYNC] == 2 ? "TRS MIDI" : "INTERNAL");
        }
    } else if (ui.scr == UI_SEQ) {
        if (k == 2u) {
            audilune_sel_vol(s);
        } else if (row < 6u) {
            int32_t nl = clamp(((int32_t)trk[row].p[P_SLEN] / 16 + (s > 0 ? 1 : -1)) * 16, 16, NSTEP);
            trk[row].p[P_SLEN] = (int16_t)nl;
            audilune_hud_popup("STEPS", nl, "");
        } else if (row == 6u) {
            ui.save_vault = (uint8_t)((ui.save_vault + (s > 0 ? 1u : 5u)) % 6u);
            audilune_hud_popup("SET VAULT", (int32_t)ui.save_vault + 1, "");
        }
    } else if (ui.scr == UI_CHOP_SRC) {
        if (k == 2u) audilune_sel_vol(s);
        else {
            ui.chop_src_type = (uint8_t)(row & 3u);
            ui.chop_src_idx = (uint8_t)((ui.chop_src_idx + (s > 0 ? 1u : 9u)) % 10u);
            audilune_hud_popup("SOURCE", (int32_t)ui.chop_src_idx + 1, "");
        }
    } else if (ui.scr == UI_CHOP_WAVE) {
        if (row == 0u && k == 3u) {
            static const char *const SRC_T[] = {"SONG", "TRACK", "SAMPLE", "VOICE"};
            ui.chop_src_type = (uint8_t)((ui.chop_src_type + (s > 0 ? 1u : 3u)) & 3u);
            audilune_hud_text("SRC TYPE", *(SRC_T + ui.chop_src_type));
        } else if (row == 1u && k == 3u) {
            uint32_t lim = ui.chop_src_type == 0u ? 7u : ui.chop_src_type == 1u ? 8u : ui.chop_src_type == 2u ? 6u : 10u;
            ui.chop_src_idx = (uint8_t)((ui.chop_src_idx + (s > 0 ? 1u : lim - 1u)) % lim);
            audilune_hud_popup("TARGET", (int32_t)ui.chop_src_idx + 1, "");
        } else if (row == 2u) {
            int32_t step = (k == 3u ? 4 : 1) * (s > 0 ? 1 : -1);
            if (ui.chop_trim == 2u) {
                al_chop_trim_e = (uint8_t)clamp((int32_t)al_chop_trim_e + step, (int32_t)al_chop_trim_s + 5, 100);
                audilune_hud_popup(k == 3u ? "TRIM ]" : "FINE ]", (int32_t)al_chop_trim_e, "%");
            } else {
                al_chop_trim_s = (uint8_t)clamp((int32_t)al_chop_trim_s + step, 0, (int32_t)al_chop_trim_e - 5);
                audilune_hud_popup(k == 3u ? "TRIM [" : "FINE [", (int32_t)al_chop_trim_s, "%");
            }
        } else if (row == 3u) {
            if (k == 3u) {
                ui.chop_mode = (uint8_t)((ui.chop_mode + (s > 0 ? 1u : 4u)) % 5u);
                audilune_hud_text("SLICE", *(AL_CHOP_MODES + ui.chop_mode));
            } else if (ui.chop_mode == 3u) {
                al_chop_sens = (uint8_t)clamp((int32_t)al_chop_sens + s * 4, 1, 100);
                audilune_hud_popup("SENS", (int32_t)al_chop_sens, "%");
            } else audilune_sel_vol(s);
        } else if (row == 4u && k == 3u) {
            al_chop_chrom = (uint8_t)(!al_chop_chrom);
            ui.chop_play_mode = al_chop_chrom;
            audilune_hud_text("PLAY MODE", al_chop_chrom ? "CHROMATIC" : "KIT");
        } else if (row >= 5u && (k == 3u || ui.chop_trim > 0u)) {
            int32_t step = (k == 3u ? 4 : 1) * (s > 0 ? 1 : -1);
            audilune_hud_popup(ui.chop_trim == 2u ? "FINE [" : ui.chop_trim == 3u ? "FINE ]" : k == 3u ? "NUDGE" : "FINE", step, "");
        } else if (k == 2u) audilune_sel_vol(s);
    }
    ui.force = 1;
}

UI_SMALL static __attribute__((noinline)) void row_kv(char *lbl, char *val, const char *l, const char *v)
{
    if (l) str_cpy(lbl, l, 21);
    if (v) str_cpy(val, v, 14);
}

UI_SMALL static void audilune_row_text(uint8_t scr, uint32_t row, char *lbl, char *val)
{
    const track_t *t = TSEL;
    *lbl = 0; *val = 0;
    if (scr == UI_FX) {
        if (row < 8u) {
            row_kv(lbl, val, *(AL_PAD_NAMES + row), ((al_pad_latch >> row) & 1u) ? "[ON]" : "PAD");
        } else {
            uint32_t fxr = row - 8u;
            row_kv(lbl, val, *(AL_FX_ROWS + fxr), 0);
            fmt_int(val, *(al_fx_val + fxr));
            str_cpy(val + str_len(val), "%", 4);
        }
    } else if (scr == UI_PUNCH) {
        row_kv(lbl, val, al_nth_str(AL_PUNCH_STR, row),
               (al_punch_active && al_punch_slot == (uint8_t)row) ? "[LIVE]" : *(AL_PUNCH_DIVS + (al_punch_div[row] % 6u)));
    } else if (scr == UI_LANDSCAPE) {
        row_kv(lbl, val, al_nth_str(AL_LAND_STR, row),
               ((al_land_on >> row) & 1u) ? al_land_sub_name(row, al_land_sub[row]) : "[MUTE]");
    } else if (scr == UI_ENV) {
        row_kv(lbl, val, *(AL_ENV_ROWS + row), 0);
        fmt_int(val, *(t->p + P_ATK + row) * 100 / 127);
        str_cpy(val + str_len(val), "%", 4);
    } else if (scr == UI_LFO) {
        static const uint32_t LFO_P[2 + 2] = {P_LWAVE, P_LRATE, P_LD_PIT, P_LD_FLT};
        row_kv(lbl, val, *(AL_LFO_ROWS + row), 0);
        fmt_int(val, *(t->p + *(LFO_P + row)));
    } else if (scr == UI_EDIT) {
        uint32_t has_sel = (*(ui.sel_step_mask + 0) | *(ui.sel_step_mask + 1) | *(ui.sel_step_mask + 2) | *(ui.sel_step_mask + 3)) != 0u;
        row_kv(lbl, val, *(AL_EDIT_ROWS + row), 0);
        if (row == 0u) row_kv(0, val, 0, ui.edit_redo_dir ? "REDO" : "UNDO");
        else if (row == 1u) row_kv(0, val, 0, has_sel ? "STEPS" : "TRK");
        else if (row == 2u) row_kv(0, val, 0, al_clip_ok ? "CLIP" : "READY");
        else if (row == 3u) row_kv(0, val, 0, al_clip_ok ? "READY" : "EMPTY");
        else if (row == 4u) { fmt_int(val, *(t->p + P_SLEN)); str_cpy(val + str_len(val), " ST", 4); }
        else if (row == 10u) { fmt_int(val, *(t->p + P_SGATE) * 100 / 127); str_cpy(val + str_len(val), "%", 4); }

        else if (row == 11u) { fmt_int(val, *(t->p + P_APROB) * 100 / 127); str_cpy(val + str_len(val), "%", 4); }
        else if (row >= 5u && row <= 13u) str_cpy(val, has_sel ? "STEPS" : "TRK", 14);
        else if (row == 14u) str_cpy(val, *(AL_NOTE_NAMES + (*(t->p + P_ROOT) % 12)), 14);
        else if (row == 15u) str_cpy(val, *(AL_SCALE_NAMES + (*(t->p + P_SCALE) % 22)), 14);
    } else if (scr == UI_GLO) {
        static const char *const AL_THM[] = {"TEAL", "ORANGE", "BURGUNDY", "PURPLE", "PINK", "BLUE", "MINT", "GREY"};
        str_cpy(lbl, *(AL_GLO_ROWS + row), 18);
        if (row == 0u) str_cpy(val, *(AL_THM + (audilune_theme_idx & 7u)), 14);
        else if (row == 1u) str_cpy(val, *(AL_FREQ_NAMES + (audilune_freq_layer & 3u)), 14);
        else if (row == 2u) str_cpy(val, al_helper_notes ? "ON" : "OFF", 14);
        else if (row == 5u) str_cpy(val, song.g[G_SYNC] == 1 ? "USB" : song.g[G_SYNC] == 2 ? "MIDI" : "INT", 14);
        else if (row == 7u) str_cpy(val, "OPEN", 14);
    } else if (scr == UI_SAVE) {
        str_cpy(lbl, "SLOT ", 18);
        fmt_int(lbl + 5, (int32_t)row + 1);
        str_cpy(val, ui.save_op ? "LOAD" : "SAVE", 14);
    } else if (scr == UI_ARP) {
        static const char *const AL_SCOPE_N[] = {"SONG", "TRACK", "STEP", "PART A->B", "SONG DJ"};
        if (row == 0u) {
            str_cpy(lbl, "AUTONOMOUS ENG", 18);
            str_cpy(val, audilune_infinite_on ? "ON" : "OFF", 14);
        } else if (row == 1u) {
            str_cpy(lbl, "SESSION MODE", 18);
            str_cpy(val, *(AL_SESS_NAMES + (ui.session_mode % 6u)), 14);
        } else if (row == 2u) {
            str_cpy(lbl, "MORPH SCOPE", 18);
            str_cpy(val, *(AL_SCOPE_N + (audilune_gen_scope % 5u)), 14);
        } else if (row == 3u) {
            str_cpy(lbl, "MORPH AMOUNT", 18);

            fmt_int(val, (int32_t)audilune_gen_morph);
            str_cpy(val + str_len(val), "%", 4);
        } else if (row == 4u) {
            str_cpy(lbl, "GENRE", 18);
            str_cpy(val, *(AUDILUNE_GENRE_NAMES + (audilune_genre % 6u)), 14);
        } else if (row == 6u) {
            str_cpy(lbl, "FREQ LAYER", 18);
            str_cpy(val, *(AL_FREQ_NAMES + (audilune_freq_layer & 3u)), 14);
        } else if (audilune_gen_scope == 4u) {
            str_cpy(lbl, "DJ VAULT", 18);
            str_cpy(val, "SLOT ", 14);
            fmt_int(val + 5, (int32_t)ui.save_vault + 1);
        } else if (audilune_gen_scope == 2u) {
            uint32_t st = audilune_active_step & 31u;
            str_cpy(lbl, "TARGET STEP", 18);
            str_cpy(val, ((audilune_sel_step_mask[song.sel] >> st) & 1u) ? "*" : "", 14);
            fmt_int(val + str_len(val), (int32_t)st + 1);
        } else {
            str_cpy(lbl, "TARGET TRK", 18);
            str_cpy(val, "TRK ", 14);
            fmt_int(val + 4, (int32_t)song.sel + 1);
        }
    } else if (scr == UI_ALGO) {
        if (row < 6u) {
            str_cpy(lbl, *(AUDILUNE_GENRE_NAMES + row), 18);
            str_cpy(val, (t->synth_mode && (ui.algo_genre[song.sel] % 6u) == row) ? "ACTIVE" : "GENRE", 14);
        } else {
            uint32_t ki = row - 6u;
            str_cpy(lbl, *(DRUM_KIT_NAMES + ki), 18);
            str_cpy(val, (!t->synth_mode && (uint32_t)drum_kit() == ki) ? "ACTIVE" : "KIT", 14);
        }
    } else if (scr == UI_SETUP) {
        str_cpy(lbl, *(AL_SETUP_ROWS + row), 21);
        if (row == 0u) { str_cpy(val, "SEL", 14); }
        else if (row == 1u) { if (ui.setup_vault == 0u) str_cpy(val, "BLANK", 14); else { str_cpy(val, "SLOT ", 14); fmt_int(val + 5, (int32_t)ui.setup_vault); } }
        else if (row == 2u) { str_cpy(val, *(AL_SESS_NAMES + (ui.session_mode % 6u)), 14); }
        else if (row == 3u) { str_cpy(val, al_gravity ? "UNQUANTIZED" : "QUANTIZED", 14); }
        else if (row == 4u) { str_cpy(val, *(AL_FREQ_NAMES + (audilune_freq_layer & 3u)), 14); }
        else if (row == 5u) { str_cpy(val, al_helper_notes ? "ON" : "OFF", 14); }
        else if (row == 6u) { str_cpy(val, *(AL_TIMER_NAMES + (ui.auto_power_off % 7u)), 14); }
        else if (row == 7u) { str_cpy(val, *(AL_TIMER_NAMES + (ui.session_timer % 7u)), 14); }
        else if (row == 8u) { fmt_int(val, song.g[G_BPM]); }
        else if (row == 9u) { str_cpy(val, *(AL_NOTE_NAMES + (*((*(trk + 0)).p + P_ROOT) % 12)), 14); }
        else if (row == 10u) { str_cpy(val, *(AL_SCALE_NAMES + (*((*(trk + 0)).p + P_SCALE) % 22)), 14); }
        else if (row == 11u) { fmt_int(val, *((*(trk + 0)).p + P_SLEN)); }
        else if (row == 12u) { str_cpy(val, song.g[G_SYNC] == 1 ? "USB-C" : song.g[G_SYNC] == 2 ? "TRS MIDI" : "INTERNAL", 14); }
    } else if (scr == UI_SEQ) {
        uint8_t grabbed = (ui.seq_grab && (uint32_t)ui.list_sel == row);
        if (row < 6u) {
            str_cpy(lbl, "TRACK ", 18);
            fmt_int(lbl + 6, (int32_t)row + 1);
            val[0] = (char)('A' + ((uint8_t)song.g[G_SLOT] % 6u));
            val[1] = ' ';
            fmt_int(val + 2, *(trk[row].p + P_SLEN));
            str_cpy(val + str_len(val), grabbed ? " [GRAB]" : " ST", 8);
        } else if (row == 6u) {
            str_cpy(lbl, "SET CHAIN", 18);
            str_cpy(val, "SLOT ", 14);
            fmt_int(val + 5, (int32_t)ui.save_vault + 1);
            if (grabbed) str_cpy(val + str_len(val), " [GRAB]", 8);
        } else {
            str_cpy(lbl, "CHOP SHOP ->", 18);
            str_cpy(val, "SEL", 14);
        }
    } else if (scr == UI_CHOP_SRC) {
        static const char *const SRC_L[] = {"CHOP SONG", "CHOP TRACK", "CHOP SAMPLE", "CHOP VOICE"};
        str_cpy(lbl, *(SRC_L + row), 18);
        fmt_int(val, (int32_t)ui.chop_src_idx + 1);
    } else if (scr == UI_CHOP_WAVE) {
        static const char *const SRC_T[] = {"SONG", "TRACK", "SAMPLE", "VOICE"};
        if (row == 0u) { str_cpy(lbl, "SOURCE TYPE", 18); str_cpy(val, *(SRC_T + (ui.chop_src_type & 3u)), 14); }
        else if (row == 1u) { str_cpy(lbl, "TARGET", 18); fmt_int(val, (int32_t)ui.chop_src_idx + 1); }
        else if (row == 2u) { str_cpy(lbl, "MASTER TRIM", 18); str_cpy(val, (uint32_t)ui.list_sel == 2u && ui.chop_trim == 1u ? "[START []" : (uint32_t)ui.list_sel == 2u && ui.chop_trim == 2u ? "[END ]]" : "GRAB", 14); }
        else if (row == 3u) { str_cpy(lbl, "SLICE MODE", 18); str_cpy(val, *(AL_CHOP_MODES + ui.chop_mode), 14); }
        else if (row == 4u) { str_cpy(lbl, "PLAY MODE", 18); str_cpy(val, al_chop_chrom ? "CHROMATIC" : "KIT", 14); }
        else {
            uint8_t act = ((uint32_t)ui.list_sel == row && ui.chop_trim > 0u);
            str_cpy(lbl, "CHOP ", 18);
            fmt_int(lbl + 5, (int32_t)row - 4);
            str_cpy(val, !act ? "GRAB" : ui.chop_trim == 1u ? "[WHOLE]" : ui.chop_trim == 2u ? "[START []" : "[END ]]", 14);
        }
    }

}

UI_SMALL static void audilune_chaos_apply(track_t *t)
{
    uint32_t p = al_pad_idx & 7u, x = al_pad_x[p], y = al_pad_y[p], z = al_pad_z[p];
    uint32_t rec_on = song.playing && ((song.rec >> song.sel) & 1u), step = t->seq_idx % trk_len(t);
    int32_t pid = -1;
    if (p == 0u) {
        pid = P_DLY; song.g[G_DTIME] = (int16_t)(1 + x / 8u);
        t->p[P_DLY] = (int16_t)clamp((int32_t)(y + z) * 140 / 254, 0, 140);
    } else if (p == 1u) {
        pid = P_REV; song.g[G_RSIZE] = (int16_t)(30 + x * 97u / 127u);
        t->p[P_REV] = (int16_t)clamp((int32_t)(y + z) * 140 / 254, 0, 140);
    } else if (p == 2u) {
        static const int16_t VC_FLT[] = {-32, 48, -48, 24}, VC_LFO[] = {16, 96, 64, 32};
        uint32_t ix = 127u - x, iy = 127u - y, w0 = ix * iy, w1 = x * iy, w2 = ix * y, w3 = x * y;
        uint32_t c0 = *(al_vec_corner + 0) & 3u, c1 = *(al_vec_corner + 1) & 3u, c2 = *(al_vec_corner + 2) & 3u, c3 = *(al_vec_corner + 3) & 3u;
        pid = P_TFLT;
        t->p[P_TFLT] = (int16_t)clamp((int32_t)((w0 * (uint32_t)(VC_FLT[c0] + 64) + w1 * (uint32_t)(VC_FLT[c1] + 64) + w2 * (uint32_t)(VC_FLT[c2] + 64) + w3 * (uint32_t)(VC_FLT[c3] + 64)) / 16129u) - 64 + ((int32_t)z - 64) / 2, -64, 63);
        t->p[P_LD_FLT] = (int16_t)((w0 * (uint32_t)VC_LFO[c0] + w1 * (uint32_t)VC_LFO[c1] + w2 * (uint32_t)VC_LFO[c2] + w3 * (uint32_t)VC_LFO[c3]) / 16129u);
    } else if (p == 3u) {
        pid = P_DIST; t->p[P_DIST] = (int16_t)(x * z / 127u); song.g[G_DUST] = (int16_t)(y * z / 127u);
    } else if (p == 4u) {
        pid = P_IMIX; t->p[P_ITYPE] = x < 43u ? IT_CHORUS : x < 86u ? IT_PHASER : IT_FLANGER;
        t->p[P_LRATE] = (int16_t)(4 + y * 120u / 127u); t->p[P_IMIX] = (int16_t)z;
    } else if (p == 5u) {
        song.g[G_TUNE] = (int16_t)(((int32_t)x - 64) * 24 / 64);
        t->p[P_LD_PIT] = (int16_t)(y * z / 127u); punch.req = y > 96u && z > 20u ? PX_STUT : -1;
    } else if (p == 6u) {
        punch.req = z < 16u ? -1 : y > 100u ? PX_FREEZE : y > 72u ? PX_REV : x < 43u ? PX_LOOP8 : x < 86u ? PX_LOOP16 : PX_LOOP32;
    } else if (p == 7u && z > 10u) {
        static uint8_t last_zone;
        uint8_t zone = (uint8_t)((x >> 4) + ((y >> 5) << 3));
        if (zone != last_zone) {
            int32_t deg = (int32_t)(x / 18u) * 2 + (int32_t)(y / 42u) * 12;
            uint8_t note = (uint8_t)clamp(48 + t->p[P_ROOT] + deg, 24, 108);
            last_zone = zone;
            pid = P_TFLT;
            t->p[P_TFLT] = (int16_t)clamp(((int32_t)x - 64), -64, 63);
            if (rec_on) { *(t->step[step].note + 0) = note; t->step[step].n = 1; t->step[step].vel = 100; t->seq_active = 1; }
        }
    }
    if (rec_on && pid >= 0) lock_set(t, step, (uint32_t)pid, t->p[pid]);
}

UI_SMALL static void audilune_page_draw(void)
{
    static uint32_t head, strip, footer;
    static const char *const TITLES[] = {"EFFECTS", "PUNCH-IN FX", "ENVELOPE", "LANDSCAPER", "LFO", "EDIT", "GLOBAL", "SAVE", "ARP", "SEQUENCE", "CHOP SRC", "CHOP SHOP", "ALGO / KITS", "STUDIO SETUP"};
    uint32_t rows = audilune_page_rows(ui.scr), sel = rows ? (uint32_t)ui.list_sel % rows : 0u;
    int32_t sect = song.playing && live_sec >= 0 ? live_sec : (int32_t)song.g[G_SLOT] - 1;
    uint32_t y0 = ui.scr == UI_CHOP_WAVE ? 110u : 56u, vis = ui.scr == UI_CHOP_WAVE ? 4u : 5u, top = sel >= vis ? sel - vis + 1u : 0u, i, j;
    audilune_in_arp_page = (uint8_t)(!ui.home && ui.scr == UI_ARP);
    audilune_active_trk = song.sel;
    te_header(ui.scr >= UI_FX && ui.scr <= UI_SETUP ? *(TITLES + (ui.scr - UI_FX)) : "AUDILUNE", C_WHITE, &head, sect);
    audilune_sel_strip(&strip);
    if (ui.scr == UI_FX && al_pad_list_mode) {
        uint32_t p = al_pad_idx & 7u, latched = (al_pad_latch >> p) & 1u;
        int32_t cx = 8 + (int32_t)al_pad_x[p] * 222 / 127, cy = 148 - (int32_t)al_pad_y[p] * 120 / 127;
        cv_begin(240, 156, C_BLACK);
        cv_text(6, 1, &FONT_S, *(AL_PAD_NAMES + p), HUD_ACCENT);

        cv_text(162, 1, &FONT_S, latched ? "[ON / LATCH]" : "[OFF]", latched ? C_WHITE : TE_G3);
        cv_rect(2, 20, 236, 134, HUD_ACCENT);
        cv_rect(3, 21, 234, 132, C_BLACK);
        for (i = 1u; i < 4u; i++) { cv_rect(2 + (int32_t)i * 59, 22, 1, 130, TE_G1); cv_rect(4, 20 + (int32_t)i * 33, 232, 1, TE_G1); }
        if (p == 2u) { cv_text(8, 24, &FONT_S, "A", TE_G3); cv_text(222, 24, &FONT_S, "B", TE_G3); cv_text(8, 136, &FONT_S, "C", TE_G3); cv_text(222, 136, &FONT_S, "D", TE_G3); }
        cv_rect(cx, 22, 1, 130, TE_G3);
        cv_rect(4, cy, 232, 1, TE_G3);
        cv_rect(cx - 4, cy - 4, 9, 9, C_WHITE);
        cv_rect(cx - 2, cy - 2, 5, 5, HUD_ACCENT);
        cv_blit(0, 56);
    } else if (ui.scr == UI_SEQ && al_seq_ovw) {
        track_t *ot = &trk[al_seq_ovw_trk < NTRK ? al_seq_ovw_trk : song.sel];
        uint32_t olen = trk_len(ot), opos = ot->seq_idx % olen;
        cv_begin(240, 156, C_BLACK);
        for (i = 0u; i < 8u; i++) {
            int32_t ry = 2 + (int32_t)i * 19;
            for (j = 0u; j < 16u; j++) {
                uint32_t st = i * 16u + j;
                int32_t sx = 4 + (int32_t)j * 14 + (j >> 2);
                uint16_t col = st >= olen ? C_BLACK : trk_step_on(ot, st) ? HUD_ACCENT : TE_G1;
                cv_rect(sx, ry, 12, 15, st == al_seq_ovw_step ? C_WHITE : col);
                if (song.playing && st == opos) cv_rect(sx, ry + 12, 12, 3, C_WHITE);
            }
        }
        cv_blit(0, 56);
    } else {
        if (ui.scr == UI_CHOP_WAVE) {
            cv_begin(240, 52, C_BLACK);
            cv_rect(2, 2, 236, 48, HUD_ACCENT);
            cv_rect(3, 3, 234, 46, C_BLACK);
            for (i = 0; i < 58u; i++) {
                int32_t x = 6 + (int32_t)i * 4, h = 6 + (int32_t)(((i * 19u + 7u) % 17u) * 2u);
                uint32_t pct = i * 100u / 58u, sl = i * 16u / 58u;
                uint16_t wc = (pct < al_chop_trim_s || pct > al_chop_trim_e) ? TE_G2 : sl == (ui.chop_sel & 15u) ? HUD_ACCENT : C_WHITE;
                cv_rect(x, 26 - h / 2, 2, h, wc);
                if ((i % 4u) == 0u) cv_rect(x, 4, 1, 44, TE_G2);
            }
            cv_blit(0, 56);
        }
        cv_begin(240, 212u - y0, C_BLACK);
        for (i = 0; i < vis && (top + i) < rows; i++) {
            uint32_t r = top + i;
            int32_t y = (int32_t)i * 22;
            char lbl[22], val[16];
            audilune_row_text(ui.scr, r, lbl, val);
            if (r == sel) cv_rect(2, y + 1, 236, 20, C_WHITE);
            cv_text(8, y + 3, &FONT_S, lbl, r == sel ? C_BLACK : C_WHITE);
            if (val[0]) cv_text(232 - text_w(&FONT_S, val), y + 3, &FONT_S, val, r == sel ? C_BLACK : HUD_ACCENT);
        }

        cv_blit(0, y0);
    }
    {
        static const char *const HUD_L[4] = {"BPM", "CLICK", "VAL", "OPT"};
        static const char *const HUD_V[4] = {"", "", "", ""};
        audilune_hud_bar(HUD_L, HUD_V, sect, &footer);
    }
}
