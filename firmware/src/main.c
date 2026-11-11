/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* AUDILUNE boot and main loop. Boot order: WDT first, boot-loop guard, fatal
 * vectors, guards; then LCD, 4000ms AUDILUNE splash, input (TIMER5 IRQ, 10 kHz), audio (ALNK0 IRQ). */
#ifndef UI_SMALL
#define UI_SMALL
#endif
extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _pool_start[], _pool_end[], _rt_start[], _rt_end[], _rt_load[];
extern uint32_t _c1_start[], _c1_end[], _c1_load[];
extern uint32_t _lr_start[], _lr_end[];

static uint8_t lowram_ok;
UI_SMALL static uint8_t lowram_test(void)
{
    uint32_t *d, bad = 0;
    for (d = _lr_start; d < _lr_end; d++)
        *d = ~(uint32_t)(uintptr_t)d;
    for (d = _lr_start; d < _lr_end; d++)
        bad |= *d != ~(uint32_t)(uintptr_t)d;
    for (d = _lr_start; d < _lr_end; d++)
        *d = 0;
    return bad ? 2u : 1u;
}
UI_SMALL static uint32_t lowram_state(void) { return lowram_ok; }

extern char _ustack_top[];
#define STACK_MARK 0x4B4D5453u
UI_SMALL static void stack_paint(char *lo, char *top)
{
    uint32_t a = (uint32_t)(uintptr_t)lo, lim = (uint32_t)(uintptr_t)top, p[3], i, *d;
    p[0] = fm1_sp();
    p[1] = fm1_ssp();
    p[2] = fm1_usp();
    for (i = 0; i < 3u; i++)
        if (p[i] >= a + 512u && p[i] < lim + 512u && p[i] - 512u < lim)
            lim = p[i] - 512u;
    for (d = (uint32_t *)lo; (uint32_t)(uintptr_t)d < lim; d++)
        *d = STACK_MARK;
}
UI_SMALL static uint32_t stack_free(const char *lo, const char *top)
{
    const uint32_t *d = (const uint32_t *)lo;
    while ((const char *)d < top && *d == STACK_MARK)
        d++;
    return (uint32_t)((const char *)d - lo);
}
UI_SMALL static uint32_t stack_free_main(void) { return stack_free(_ustack_lo, _ustack_top); }
UI_SMALL static uint32_t stack_free_sup(void) { return stack_free(_sstack_lo, _sstack_top); }
UI_SMALL static uint32_t stack_sp(void) { return fm1_sp(); }

void fm1_timer5_irq(void)
{
    static uint32_t sub, owed;
    uint32_t t0 = fm1_ticks(), usb_due = sub % 5u == 0u;
    fm1_timer5_ack();
    felucca_dbg.timer_irqs++;
    fm1_input_tick();
    {
        static uint32_t last, acc;
        acc += t0 - last;
        last = t0;
        while (acc >= 1000u * FM1_TICKS_PER_US) {
            acc -= 1000u * FM1_TICKS_PER_US;
            fm1_ms++;
        }
    }
    if (usb_due)
        owed |= 1u;
#if FELUCCA_UART
    if (sub % 5u == 2u)
        owed |= 2u;
#endif
    if (++sub == 10u)
        sub = 0;
    if (felucca_dbg.in_audio) {
        felucca_dbg.nested++;
#if FELUCCA_UAC
        if (usb_due)
            uac_service();
#endif
        t5_nested_ticks += fm1_ticks() - t0;
        return;
    }
    if (owed & 1u)
        usb_poll();
#if FELUCCA_UART
    if (owed & 2u)
        uart_midi_poll();
#endif
    owed = 0;
}
extern void isr_timer5(void);

UI_SMALL static void timer5_start(void)
{
    fm1_timer5_start(isr_timer5, 4);
}

UI_SMALL static void hexs(char *b, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        b[i] = "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u];
    b[8] = 0;
}

UI_SMALL static void fm1_fault(const fm1_crash_t *c)
{
    char b[12];
    uint32_t t0;
    if (fm1_c1_on)
        fm1_cpu1_bar();
    fm1_audio_stop();
    lcd_fill(0, 0, 240, 240, RGB(160, 0, 0));
    draw_text_box(0, 8, 240, &FONT_S, "AUDILUNE FAULT", C_WHITE, 1);
    hexs(b, c->vec);
    draw_text_box(10, 40, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->pc);
    draw_text_box(10, 60, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->emu);
    draw_text_box(10, 84, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->dbg);
    draw_text_box(10, 102, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->rets);
    draw_text_box(10, 120, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->sp);
    draw_text_box(10, 144, 220, &FONT_S, b, C_WHITE, 0);
    {
        char t[28];
        str_cpy(t, "C1 ", sizeof t);
        fmt_int(t + str_len(t), (int32_t)cpu1_state);
        str_cpy(t + str_len(t), " LATE ", 7);
        fmt_int(t + str_len(t), (int32_t)(cpu1_timeouts > 99u ? 99u : cpu1_timeouts));
        str_cpy(t + str_len(t), " BAD ", 6);
        fmt_int(t + str_len(t), (int32_t)(fm1_c1_mb.bad > 99u ? 99u : fm1_c1_mb.bad));
        draw_text_box(10, 162, 220, &FONT_S, t, C_WHITE, 0);
    }
    t0 = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t0) < 10000u * 1000u * FM1_TICKS_PER_US)
        ;
    fm1_reboot();
}

UI_SMALL static void felucca_init(void)
{
    uint32_t i;
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    fm6_init();
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults(t);
        if (i < NPART) {
            set_engine_of(t, TRK_DEF[i][0]);
            apply_preset_to(t, TRK_DEF[i][1]);
            t->engine = t->eng_req;
        }
        track_defaults_steps(t);
    }
    TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;
    song.sel = 0;
    song.master_q12 = 2048;
    autosave_resume();
    song.g[G_SYNC] = (int16_t)lights_sync;
    song.g[G_MIDI] = (int16_t)lights_mout;
    song.g[G_ROUTE] = (int16_t)lights_min;
    song.g[G_CH1] = midi_set[0], song.g[G_CH2] = midi_set[1], song.g[G_CH3] = midi_set[2];
    song.g[G_DRCH] = midi_set[3], song.g[G_CHOTH] = midi_set[4], song.g[G_BEND] = midi_set[5];
    song.g[G_FXCH] = midi_set[6];
    song.g[G_DRMAP] = midi_set[7];
    song.g[G_CLKOUT] = midi_set[9];
    song.g[G_RMCH] = midi_set[8];
    song.g[G_FXFADE] = fxfade;
    layers_init();
    go_home();
    ui.force = 1;
}

UI_SMALL static void fm1_main(void)
{
    int32_t knob = 512 * 16;
    uint32_t healthy_since;
    persist_boot();
#if FELUCCA_OTA
    if (flash_ok)
        ota_boot_cleanup();
#endif
    settings_init();
    lcd_init();
    sloop_splash();
    if (felucca_dbg.magic != DBG_MAGIC) {
        memset(&felucca_dbg, 0, sizeof felucca_dbg);
        felucca_dbg.magic = DBG_MAGIC;
    }
    felucca_dbg.boots++;
    felucca_dbg.max_us = 0;
    felucca_dbg.in_audio = 0;
    felucca_dbg.prev_stage = felucca_dbg.stage;
    felucca_dbg.prev_page = felucca_dbg.page;
    felucca_dbg.prev_home = felucca_dbg.home;
    felucca_dbg.prev_frames = felucca_dbg.ui_frames;
    felucca_dbg.prev_rst = fm1_boot.p3_rst;
    fm1_input_init();
    fm1_adc_init();
    panel_init();
    felucca_init();
    cpu1_boot();
    audio_init();
#if FELUCCA_CDC
    usb_cdc_on = usb_serial;
#endif
#if FELUCCA_UAC && FELUCCA_UAC_48K
    usb_rate_mode = usb_rate;
#endif
    usb_start();
#if FELUCCA_UART
    uart_midi_init();
#endif
    timer5_start();
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    fm1_delay_ms(30);
    if ((fm1_in.buttons & 3u) == 3u) {
        panel_setup();
        settings_save();
    }
    lcd_fill(0, 0, 240, 240, C_BLACK);

    healthy_since = fm1_ms;
    for (;;) {
        uint32_t m = fm1_ms;
        fm1_wdt_feed();
        usb_retry(fm1_ms);
        if ((uint32_t)(m - healthy_since) >= 30000u && bootguard.pending) {
            bootguard_clear(&bootguard);
        }
        {
            int32_t b = fm1_adc_read(FM1_ADC_BATT);
            if (b > 0)
                song.batt_raw = song.batt_raw ? song.batt_raw + (b - song.batt_raw) / 32 : b;
        }
        {
            int32_t a = fm1_adc_read(FM1_ADC_MASTER);
            if (a >= 0) {
                uint32_t k10;
                knob += (a * 16 - knob) / 8;
                k10 = (uint32_t)(knob / 16);
                if (ed_master) {
                    if (ed_master_k == 0xFFFFu)
                        ed_master_k = (uint16_t)k10;
                    if (k10 + 48u < ed_master_k || k10 > ed_master_k + 48u)
                        ed_master = 0;
                    else
                        k10 = (ed_master - 1u) * 1023u / 126u;
                }
                song.master_q12 = (k10 * k10) >> 8;
            }
        }
        {
            static uint32_t t0, shown;
            uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
            if ((fm1_in.buttons & both) != both || song.playing) {
                if (shown)
                    ui_say("UPDATE MODE ", "CANCELLED");
                shown = 0;
                t0 = fm1_ms;
            } else if (fm1_ms - t0 > 2000u && fm1_ms - t0 <= 5000u) {
                uint32_t left = (5000u - (fm1_ms - t0) + 999u) / 1000u;
                if (left != shown) {
                    char d[4] = {(char)('0' + left), '.', '.', 0};
                    ui_say("UPDATE MODE IN ", d);
                    shown = left;
                }
            } else if (fm1_ms - t0 > 5000u) {
                fm1_audio_stop();
#if FELUCCA_ARRANGER
                persist_flush_now();
#endif
                lcd_fill(0, 0, 240, 240, C_BLACK);
                draw_text_box(0, 110, 240, &FONT_S, "UBOOT", RGB(80, 120, 255), 1);
                usb_detach();
                fm1_delay_ms(30);
                bootguard.pending = 0;
                fm1_enter_uboot();
            }
        }
#if FELUCCA_OTA
        ed_service();
        ota_service();
        if (usb.ota_req) {
            usb.ota_req = 0;
            panic_req = (1u << NTRK) - 1u;
#if FELUCCA_ARRANGER
            persist_flush_now();
#endif
            if (flash_ok)
                ota_session();
            lcd_fill(0, 0, 240, 240, C_BLACK);
            ui.force = 1;
        }
#endif
        if (usb.uboot_req) {
            fm1_audio_stop();
#if FELUCCA_ARRANGER
            persist_flush_now();
#endif
            lcd_fill(0, 0, 240, 240, C_BLACK);
            draw_text_box(0, 110, 240, &FONT_S, "UBOOT (USB)", C_WHITE, 1);
            fm1_delay_ms(20);
            usb_detach();
            fm1_delay_ms(30);
            bootguard.pending = 0;
            fm1_enter_uboot();
        }
#if FELUCCA_CDC
        cdc_task();
#endif
        felucca_dbg.ui_frames++;
        felucca_dbg.page = ui.page;
        felucca_dbg.home = ui.home;
        felucca_dbg.stage = 1;
