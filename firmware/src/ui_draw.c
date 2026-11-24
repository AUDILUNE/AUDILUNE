/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* AUDILUNE Block 2: Display Engine — top status bar, columns, step graphs, and
 * persistent 1px-bordered Bottom Info Bar (HUD Footer) with 1.5s popups. */
#define B4 4
#define B8 8
#define B12 12
#define B16 16
#define B24 24
#define B32 32
#define B48 48
#define I0 0
#define I1 1
#define I2 2

static void draw_menu(void);
static uint32_t str_hash(uint32_t h, const char *s);

UI_SMALL static int32_t batt_level(void)
{
    return song.batt_raw >= 591 ? 3 : song.batt_raw >= 561 ? 2 : song.batt_raw >= 531 ? 1 : 0;
}

UI_SMALL static int32_t batt_shown(void)
{
    if (usb.config && !usb.suspended)
        return 1 + (int32_t)((fm1_ms / 600u) % 3u);
    return batt_level();
}

UI_SMALL static void draw_head(void)
{
    (void)0;
}

UI_SMALL static void draw_frame(void)
{
    (void)0;
}

UI_SMALL static void al_unused_frame_tail(void)
{
    if (0) {
        (void)0;
    }
}

UI_SMALL static void draw_column(uint32_t c, const char *label, const char *val, const char *unit, uint16_t vc,
                        int32_t ratio, uint32_t icon)
{
    (void)c; (void)label; (void)val; (void)unit; (void)vc; (void)ratio; (void)icon;
    if (0) {

        (void)0;
    }
}

UI_SMALL static void graph_steps(const track_t *t, uint16_t c)
{
    uint32_t i, len = (uint32_t)t->p[ P_SLEN ];
    if (0) {
        for (i = 0; i < NSTEP; i++) {
            int32_t x = 6 + (int32_t)(i % 16u) * 14 + (int32_t)(i % 16u) / 4 * 4, y = 6 + (int32_t)(i / 16u) * 24;
            const step_t *st = &t->step[ i ];
            (void)st; (void)x; (void)y; (void)len; (void)c;
        }
    }
}

UI_SMALL static void graph_roll(const track_t *t, uint16_t c)

{
    uint32_t i;
    (void)t; (void)c;
    if (0) {
        for (i = 0; i < 1u; i++) {
            (void)0;
        }
    }
}

UI_SMALL static void al_unused_roll_tail(void)
{
    if (0) {
         char nu[ B8 ];
        (void)nu;
    }
}

UI_SMALL static void graph_slicer(const track_t *t, uint16_t c)
{
    uint32_t i;
    (void)t; (void)c;
    if (0) {
        for (i = 0; i < 16u; i++) {
            int32_t x = 4 + (int32_t)i * 14, open = 0;
            (void)x; (void)open;
        }
    }
}

UI_SMALL static uint32_t str_hash(uint32_t h, const char *s)
{
    while (*s)
        h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

UI_SMALL static uint32_t graph_signature(void)
{
    const page_t *pg = cur_page();
    const track_t *t = TSEL;
    (void)pg; (void)t;
    return 0u;
}

UI_SMALL static uint32_t trk_level(uint32_t c)
{
    return (uint32_t)(c == TRK_DRUM ? song.g[ G_DRLVL ] : trk[ c ].p[ P_LEVEL ]) & 127u;
}

UI_SMALL static void trk_short_name(uint32_t c, char *b)
{
    const track_t *t = &trk[ c ];
    const engine_t *e = ENGINES[ t->eng_req % NENGINES ];
    if (c == TRK_DRUM)
        str_cpy(b, "DRUM", 13);
    else if (user_of(t) < UP_SLOTS)
        up_name(user_of(t), b);
    else if (e->npresets)
        str_cpy(b, e->presets[ t->preset % e->npresets ].name, 13);
    else
        str_cpy(b, e->name, 13);
}
UI_SMALL static void draw_tracks(void)
{
    uint32_t c;
    if (0) {
        for (c = 0; c < NTRK; c++) {
            uint32_t x0 = c * 60u + 4u;
            (void)x0;

              (void)0;
        }
    }
}

UI_SMALL static void graph_big(void)
{
    (void)0;
}

UI_SMALL static void draw_graph(void)
{
    uint32_t sig = graph_signature();
    if (0) {
        if (sig) {

            (void)0;
        }
    }
}

/* AUDILUNE Block 2: Persistent Bottom Info Bar (HUD Footer) */
static char hud_popup[ B32 ];
static uint32_t hud_popup_until;

UI_SMALL static void hud_notify(const char *label, const char *val)
{
    uint32_t n;
    str_cpy(hud_popup, label, sizeof hud_popup);
    n = str_len(hud_popup);
    if (val && val[ I0 ] && n + 2u < sizeof hud_popup) {
        str_cpy(hud_popup + n, ": ", sizeof hud_popup - n);
        n = str_len(hud_popup);

        str_cpy(hud_popup + n, val, sizeof hud_popup - n);
    }
    hud_popup_until = fm1_ms + 1500u;
    ui.force = 1;
}

UI_SMALL static void draw_foot(void)
{
    char line[ B48 ], b[ B16 ];
    const track_t *t = TSEL;
    const engine_t *e = ENGINES[ t->eng_req % NENGINES ];
    const char *ename = is_drum(t) ? "DRUMS" : e->name;
    int32_t sect = song.playing && live_sec >= 0 ? live_sec : (int32_t)song.g[ G_SLOT ] - 1;
    uint32_t popup_on = hud_popup[ I0 ] && (int32_t)(hud_popup_until - fm1_ms) > 0;
    uint32_t sig;

    if (popup_on) {
        str_cpy(line, hud_popup, sizeof line);
    } else if (ui.hot_t && ui.focus_l[ I0 ]) {
        str_cpy(line, ui.focus_l, sizeof line);
        str_cpy(line + str_len(line), ": ", sizeof line - str_len(line));
        str_cpy(line + str_len(line), ui.focus_v, sizeof line - str_len(line));
        if (ui.focus_u[ I0 ]) {
            str_cpy(line + str_len(line), " ", sizeof line - str_len(line));
            str_cpy(line + str_len(line), ui.focus_u, sizeof line - str_len(line));
        }
    } else {
        str_cpy(line, "TRK ", sizeof line);
        fmt_int(b, (int32_t)song.sel + 1);
        str_cpy(line + str_len(line), b, sizeof line - str_len(line));
        str_cpy(line + str_len(line), " | PAT ", sizeof line - str_len(line));
        b[ I0 ] = (char)('A' + ((uint32_t)(sect < 0 ? 0 : sect) % 6u));
        b[ I1 ] = 0;
        str_cpy(line + str_len(line), b, sizeof line - str_len(line));
        str_cpy(line + str_len(line), " | ", sizeof line - str_len(line));
        fmt_int(b, song.g[ G_BPM ]);
        str_cpy(line + str_len(line), b, sizeof line - str_len(line));
        str_cpy(line + str_len(line), " BPM | ", sizeof line - str_len(line));
        str_cpy(line + str_len(line), ename, sizeof line - str_len(line));
    }


    sig = str_hash(0x9E3779B9u, line) + (uint32_t)C_ACCENT * 31u + (popup_on ? 7919u : 0u);
    if (!ui.force && sig == ui.foot_sig)
        return;
    ui.foot_sig = sig;

    cv_begin(240, H_FOOT, C_BLACK);
    cv_rect(0, 12, 240, 1, C_ACCENT);
    cv_rect(0, H_FOOT - 1, 240, 1, C_ACCENT);
    cv_rect(0, 12, 1, H_FOOT - 12, C_ACCENT);
    cv_rect(239, 12, 1, H_FOOT - 12, C_ACCENT);

    while (line[ I0 ] && text_w(&FONT_S, line) > 230)
        line[ str_len(line) - 1u ] = 0;
    cv_text((240 - text_w(&FONT_S, line)) / 2, 18, &FONT_S, line, popup_on || ui.hot_t ? C_WHITE : C_HI);
    cv_blit(0, Y_FOOT);
}
UI_SMALL static void draw_columns(void)
{
    uint32_t c;
    char val[ B12 ];
    const char *unit;
    if (ui.home) {
        for (c = 0; c < 4u; c++) {
            int16_t *vp;
            const param_desc_t *d = home_param(c, &vp);
            param_format(d, *vp, val, &unit);
            draw_column(c, d->label, val, unit, VAL(c), RATIO(d, *vp), param_icon(d, *vp));
        }
        return;
    }
    if (is_drum(TSEL) && !page_for_drum(cur_page())) {
        for (c = 0; c < 4u; c++)
            draw_column(c, "", "", "", C_HI, -1, ICON_AUTO);
        return;
    }
    if (cur_page()->scope == SC_TRK) {
        const track_t *t = TSEL;
        uint32_t lvl = trk_level(song.sel);
        fmt_int(val, (int32_t)song.sel + 1);
        draw_column(0, "TRACK", val, "/6", VAL(0u), (int32_t)song.sel * 1000 / (NTRK - 1), ICON_AUTO);
        if (!lvl || t->p[ P_MUTE ]) {
            str_cpy(val, "MUTE", 12);
            unit = "";
        } else if (is_drum(t)) {
            fmt_int(val, (int32_t)lvl);
            unit = "";
        } else {
            param_format(&TP[ P_LEVEL ], (int32_t)lvl, val, &unit);
        }
        draw_column(1, "LEVEL", val, unit, lvl && !t->p[ P_MUTE ] ? VAL(1u) : C_DIM, (int32_t)lvl * 1000 / 127, ICON_AUTO);
        param_format(&TP[ P_SLEN ], t->p[ P_SLEN ], val, &unit);
        draw_column(2, "LEN", val, unit, VAL(2u), RATIO(&TP[ P_SLEN ], t->p[ P_SLEN ]), ICON_AUTO);
        param_format(&TP[ P_PAN ], t->p[ P_PAN ], val, &unit);
        draw_column(3, "PAN", val, unit, VAL(3u), RATIO(&TP[ P_PAN ], t->p[ P_PAN ]), param_icon(&TP[ P_PAN ], t->p[ P_PAN ]));
        return;
    }
    if (cur_page()->graph == GR_BROWSE) {
        uint32_t total, cur = preset_pos(&total);
        char u[ B8 ];
        fmt_int(val, (int32_t)cur + 1);
        str_cpy(u, "/", 8);
        fmt_int(u + 1, (int32_t)total);
        draw_column(0, "No.", val, u, VAL(0u), -1, ICON_NONE);
        draw_column(1, "ENG", ENGINES[ TSEL->eng_req ]->name, "", VAL(1u), -1, engine_icon(ENGINES[ TSEL->eng_req ]->name));
        draw_column(2, "", "", "", C_HI, -1, ICON_AUTO);
        draw_column(3, "FAV", fav_has(fav_key()) ? "ON" : "--", "", VAL(3u), -1, ICON_NONE);
        return;
    }
    if (cur_page()->graph == GR_USER) {
        int used = up_used(ui.uslot);
        up_slot_label(val, ui.uslot);
        draw_column(0, "SLOT", val, "", VAL(0u), (int32_t)ui.uslot * 1000 / (int32_t)(UP_SLOTS - 1u), ICON_AUTO);
        draw_column(1, "LOAD", "--", "", used ? C_HI : C_DIM, -1, ICON_AUTO);
        draw_column(2, "ERASE", "--", "", used ? C_HI : C_DIM, -1, ICON_AUTO);
        draw_column(3, "SAVE", "--", "", C_HI, -1, ICON_AUTO);
        return;
    }
    if (cur_page()->scope == SC_STEP) {
        const step_t *st = &TSEL->step[ ui.cursor ];
        char sn[ B8 ], sl[ B8 ], u[ B8 ], ln[ B8 ];
        int on = step_on(st);
        uint32_t k = on ? ui.note_k % st->n : 0u;
        fmt_int(sn, (int32_t)ui.cursor + 1);
        str_cpy(sl, "/", 8);
        fmt_int(sl + 1, TSEL->p[ P_SLEN ]);
        draw_column(0, "NOTE", sn, sl, VAL(0u), -1, ICON_AUTO);
        u[ I0 ] = 0;
        if (on) {
            note_name(val, st->note[ k ]);
            if (st->n > 1u) {
                fmt_int(u, (int32_t)k + 1);
                str_cpy(u + str_len(u), "/", 2);
                fmt_int(u + str_len(u), st->n);
            }
        } else {
            str_cpy(val, "--", 12);
        }
        draw_column(1, "PITCH", val, u, on ? VAL(1u) : C_DIM, -1, ICON_AUTO);
        if (on)
            fmt_int(ln, (int32_t)note_span(TSEL, ui.cursor));
        else
            str_cpy(ln, "--", 8);
        draw_column(2, "LEN", ln, on ? "STEP" : "", on ? VAL(2u) : C_DIM, -1, ICON_AUTO);
        draw_column(3, "FORCE", on ? FORCE_N[ note_force(st, k) ] : "--", "", on ? VAL(3u) : C_DIM, -1, ICON_AUTO);
        return;
    }
    for (c = 0; c < 4u; c++) {
        int16_t *vp;
        const param_desc_t *d = page_desc(cur_page(), c, &vp);
        if (cur_page()->graph == GR_SLOTS && c == 1u) {
            draw_column(1, "ERASE", "--", "", project_used((uint32_t)song.g[ G_SLOT ] - 1u) ? C_HI : C_DIM, -1, ICON_AUTO);
            continue;
        }
        if (!d || !d->label[ I0 ] || d->label[ I0 ] == '-') {
            draw_column(c, "", "", "", C_HI, -1, ICON_AUTO);
            continue;
        }
        if (cur_page()->id[ c ] == G_INFO && cur_page()->scope == SC_GLOBAL && !usb.config) {
            str_cpy(val, !usb.up ? "OFF" : usb.config ? "MIDI" : usb.setups ? "ENUM" : usb.sof_seen ? "BUS" : "WAIT", 12);
            unit = "USB";
            draw_column(c, "USB", val, unit, C_HI, -1, ICON_AUTO);
            continue;
        }
        if (cur_page()->id[ c ] == G_INFO && cur_page()->scope == SC_GLOBAL) {
            fmt_int(val, (int32_t)(song.cpu_q8 * 100u / 256u));
            unit = "%";
        } else {
            param_format(d, *vp, val, &unit);
        }
        draw_column(c, d->label, val, unit, VAL(c), d->fmt == F_ENUM && d->max < 2 ? -1 : RATIO(d, *vp),
                    param_icon(d, *vp));
    }
}

UI_SMALL static void ui_timers(void)
{
    if (ui.msg_t)
        ui.msg_t--;
    if (ui.bpm_t)
        ui.bpm_t--;
    if (ui.arm_t && !--ui.arm_t)
        ui.arm = 0;
    if (ui.hot_t)
        ui.hot_t--;
}

static void vis_draw(void);
static int vis_shown(void);
static void audilune_page_draw(void);
static uint8_t vis_on, vis_shown_last, vis_name_t;
UI_SMALL static void ui_draw(void)
{
    ui.frame++;
    pads_tick();
    if (rec_go) {
        rec_go = 0;

        ui_message("RECORDING");
    }
    if (ft_bars) {
        char m[ B24 ];
        uint32_t n = ft_bars;
        ft_bars = 0;
        if (n == 0xFFu) {
            ui_message("TAKE DROPPED");
        } else {
            str_cpy(m, n == 1u ? "LOOP 1 BAR " : n == 2u ? "LOOP 2 BARS " : "LOOP 4 BARS ", sizeof m);
            fmt_int(m + str_len(m), song.g[ G_BPM ]);
            str_cpy(m + str_len(m), " BPM", sizeof m - str_len(m));
            ui_message(m);
        }
    }
    if (er_flash) {
        er_flash = 0;
        if (!ui.msg_t)
            ui_message("ERASED");
    }
    if (!ui.menu && (ui.layer != LY_PLAY || ui.hold_kind)) {
        if (ui.hold_kind)
            hold_screen_draw();
        else
            layer_screen_draw();
        ui_timers();
        ui.force = 0;
        return;
    }
    if (layer_shown) {
        layer_shown = 0;
        lcd_fill(0, 0, 240, 240, C_BLACK);
        ui.force = 1;
    }
    if ((rec_wait || ft_on) && !ui.menu && !on_song_page()) {
        rec_screen_draw();
        ui_timers();
        ui.force = 0;
        return;
    }
    if (rec_shown) {
        rec_shown = 0;
        lcd_fill(0, 0, 240, 240, C_BLACK);
        ui.force = 1;
    }
#if FELUCCA_ARRANGER
    if (!ui.menu && on_song_page()) {
        song_screen_draw();
        ui_timers();
        ui.force = 0;
        return;
    }
#endif
    if (ui.menu) {
        draw_menu();
        ui_timers();
        ui.force = 0;
        return;
    }
    if (ui.scr == UI_VIS || vis_shown()) {
        vis_draw();
        ui_timers();
        ui.force = 0;
        return;
    }
    if (vis_shown_last) {
        vis_shown_last = 0;
        vis_on = 0;
        lcd_fill(0, 0, 240, 240, C_BLACK);
        ui.force = 1;
    }
    if (ui.scr >= UI_FX) {
        audilune_page_draw();
        ui_timers();
        ui.force = 0;
        return;
    }
    studio_tracks_draw();
    draw_foot();

    ui_timers();
    felucca_dbg.stage = 6;
    draw_foot();
    ui.force = 0;
}
