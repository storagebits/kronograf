/*
 * Radio: international web radios (MP3 over HTTP) on the speaker, plus one custom URL.
 * Previous / next change station (and switch while playing); the song title comes from
 * the stream's ICY metadata. Playback continues with the screen off; long-press the station
 * name to type a custom URL.
 */
#include "nav.h"
#include "kit.h"
#include "theme.h"
#include "fonts/kg_fonts.h"
#include "../hal/hal.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *s_scr, *s_name, *s_genre, *s_title, *s_info, *s_buf, *s_play, *s_vol, *s_kb_panel, *s_ta;
static lv_timer_t *s_timer;
static int s_station;
static bool s_drawn_playing;

static int n_stations(void) { return KG_N_STATIONS + (hal_radio_custom_url()[0] ? 1 : 0); }

static void station(int i, const char **name, const char **genre, const char **url)
{
    if (i < KG_N_STATIONS) {
        *name = KG_STATIONS[i].name, *genre = KG_STATIONS[i].genre, *url = KG_STATIONS[i].url;
    } else {
        *name = "Custom", *genre = hal_radio_custom_url(), *url = hal_radio_custom_url();
    }
}

static bool playing(const kg_radio_status_t *st) { return st->state != KG_RADIO_OFF && st->state != KG_RADIO_ERROR; }

static void play_current(void)
{
    const char *name, *genre, *url;
    station(s_station, &name, &genre, &url);
    hal_radio_play(name, url);
}

static void refresh(void)
{
    kg_radio_status_t st;
    hal_radio_status(&st);
    const char *name, *genre, *url;
    station(s_station, &name, &genre, &url);
    lv_label_set_text(s_name, name);
    lv_label_set_text(s_genre, genre);
    bool on = playing(&st) && !strcmp(st.station, name);
    char b[64];
    switch (on ? st.state : st.state == KG_RADIO_ERROR ? KG_RADIO_ERROR : KG_RADIO_OFF) {
    case KG_RADIO_CONNECTING: snprintf(b, sizeof b, "%s", st.error[0] ? st.error : "Connecting…"); break;
    case KG_RADIO_BUFFERING: snprintf(b, sizeof b, "Buffering… %u %%", st.buffer_pct); break;
    case KG_RADIO_PLAYING:
        snprintf(b, sizeof b, "%u kbps · %.1f kHz · buffer %u %%", st.kbps, st.rate / 1000.0, st.buffer_pct);
        break;
    case KG_RADIO_ERROR: snprintf(b, sizeof b, "%s", st.error); break;
    default: snprintf(b, sizeof b, hal_sound_available() ? "Tap play" : "No speaker on this watch"); break;
    }
    lv_label_set_text(s_info, b);
    lv_obj_set_style_text_color(s_info, st.state == KG_RADIO_ERROR && !on ? KG_C_ALERT
                                        : on && st.state != KG_RADIO_PLAYING ? KG_C_WARN : KG_C_TEXT_DIM, 0);
    const char *title = on && st.title[0] ? st.title : on ? "Live" : "";
    if (strcmp(lv_label_get_text(s_title), title)) lv_label_set_text(s_title, title); /* keeps the scroll going */
    lv_bar_set_value(s_buf, on ? st.buffer_pct : 0, LV_ANIM_OFF);
    lv_bar_set_value(s_vol, hal_radio_volume(), LV_ANIM_OFF);
    if (on != s_drawn_playing) {
        s_drawn_playing = on;
        lv_obj_invalidate(s_play);
    }
}

static void tick(lv_timer_t *t)
{
    (void)t;
    refresh();
}

static void play_cb(lv_event_t *e)
{
    (void)e;
    kg_radio_status_t st;
    hal_radio_status(&st);
    if (s_drawn_playing) hal_radio_stop();
    else play_current();
    refresh();
}

static void step_cb(lv_event_t *e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    int n = n_stations();
    s_station = (s_station + d + n) % n;
    hal_setting_set("radio_st", s_station);
    if (s_drawn_playing) play_current(); /* switch stations while playing */
    lv_label_set_text(s_title, "");
    refresh();
}

static void vol_cb(lv_event_t *e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    int v = hal_radio_volume() + d;
    if (v >= 0 && v <= KG_RADIO_VOL_MAX) hal_radio_set_volume((uint8_t)v);
    refresh();
}

/* Play ▶ or stop ■ drawn on the main button. */
static void play_draw(lv_event_t *e)
{
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target(e), &a);
    lv_coord_t cx = (a.x1 + a.x2) / 2, cy = (a.y1 + a.y2) / 2;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = KG_C_BG;
    if (s_drawn_playing) {
        d.radius = 3;
        lv_area_t sq = {cx - 11, cy - 11, cx + 11, cy + 11};
        lv_draw_rect(ctx, &d, &sq);
    } else {
        lv_point_t tri[3] = {{cx - 9, cy - 14}, {cx - 9, cy + 14}, {cx + 15, cy}};
        lv_draw_polygon(ctx, &d, tri, 3);
    }
}

static void kb_cb(lv_event_t *e)
{
    lv_obj_add_flag(s_kb_panel, LV_OBJ_FLAG_HIDDEN);
    if (lv_event_get_code(e) != LV_EVENT_READY) return;
    const char *url = lv_textarea_get_text(s_ta);
    hal_radio_set_custom_url(!strcmp(url, "http://") ? "" : url);
    s_station = hal_radio_custom_url()[0] ? KG_N_STATIONS : 0;
    hal_setting_set("radio_st", s_station);
    refresh();
}

static void custom_cb(lv_event_t *e)
{
    (void)e;
    lv_textarea_set_text(s_ta, hal_radio_custom_url()[0] ? hal_radio_custom_url() : "http://");
    lv_obj_clear_flag(s_kb_panel, LV_OBJ_FLAG_HIDDEN);
    kit_tick();
}

static lv_obj_t *round_btn(lv_obj_t *scr, const char *sym, lv_coord_t cx, lv_coord_t cy, lv_coord_t d, lv_event_cb_t cb, int arg)
{
    lv_obj_t *b = kit_button(scr, sym, false, d, d, cb, (void *)(intptr_t)arg);
    lv_obj_set_style_text_font(lv_obj_get_child(b, 0), &lv_font_montserrat_14, 0); /* has the symbols */
    lv_obj_set_pos(b, cx - d / 2, cy - d / 2);
    return b;
}

static lv_obj_t *centered(lv_obj_t *scr, const lv_font_t *f, lv_color_t c, lv_coord_t y)
{
    lv_obj_t *l = kit_label(scr, f, c, "");
    lv_obj_set_width(l, 220);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(l, 10, y);
    return l;
}

static lv_obj_t *create(void)
{
    lv_obj_t *scr = s_scr = kit_screen();
    kit_title(scr, "RADIO");
    s_name = centered(scr, &kg_font_label_20, KG_C_TEXT, 24);
    lv_obj_add_flag(s_name, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_name, custom_cb, LV_EVENT_LONG_PRESSED, NULL);
    s_genre = centered(scr, &kg_font_text_16, KG_C_TEXT_DIM, 47);
    s_title = centered(scr, &kg_font_text_16, KG_C_ACCENT, 74);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    s_info = centered(scr, &kg_font_text_16, KG_C_TEXT_DIM, 96);

    s_buf = lv_bar_create(scr);
    lv_obj_set_size(s_buf, 140, 3);
    lv_obj_set_pos(s_buf, 50, 120);
    lv_bar_set_range(s_buf, 0, 100);
    lv_obj_set_style_bg_color(s_buf, KG_C_BTN, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_buf, KG_C_TRACK, LV_PART_INDICATOR);

    round_btn(scr, LV_SYMBOL_PREV, 46, 160, 46, step_cb, -1);
    round_btn(scr, LV_SYMBOL_NEXT, 194, 160, 46, step_cb, 1);
    s_play = kit_button(scr, "", true, 64, 64, play_cb, NULL);
    lv_obj_set_pos(s_play, 120 - 32, 160 - 32);
    lv_obj_add_event_cb(s_play, play_draw, LV_EVENT_DRAW_MAIN_END, NULL);

    round_btn(scr, LV_SYMBOL_VOLUME_MID, 30, 214, 34, vol_cb, -1);
    round_btn(scr, LV_SYMBOL_VOLUME_MAX, 210, 214, 34, vol_cb, 1);
    s_vol = lv_bar_create(scr);
    lv_obj_set_size(s_vol, 120, 6);
    lv_obj_set_pos(s_vol, 60, 211);
    lv_bar_set_range(s_vol, 0, KG_RADIO_VOL_MAX);
    lv_obj_set_style_bg_color(s_vol, KG_C_BTN, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_vol, KG_C_ACCENT, LV_PART_INDICATOR);

    /* Custom URL: field + keyboard over the whole screen. */
    s_kb_panel = lv_obj_create(scr);
    lv_obj_remove_style_all(s_kb_panel);
    lv_obj_set_size(s_kb_panel, KG_W, KG_H);
    lv_obj_set_style_bg_color(s_kb_panel, KG_C_BG, 0);
    lv_obj_set_style_bg_opa(s_kb_panel, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_kb_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *hint = kit_label(s_kb_panel, &kg_font_text_16, KG_C_TEXT_DIM, "MP3 stream, http:// only");
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 2);
    s_ta = lv_textarea_create(s_kb_panel);
    lv_textarea_set_one_line(s_ta, true);
    lv_textarea_set_max_length(s_ta, 255);
    lv_obj_set_size(s_ta, 228, 38);
    lv_obj_align(s_ta, LV_ALIGN_TOP_MID, 0, 22);
    lv_obj_set_style_text_font(s_ta, &kg_font_text_16, 0);
    lv_obj_set_style_bg_color(s_ta, KG_C_SURFACE, 0);
    lv_obj_set_style_text_color(s_ta, KG_C_TEXT, 0);
    lv_obj_set_style_border_color(s_ta, KG_C_ACCENT, 0);
    lv_obj_t *kb = lv_keyboard_create(s_kb_panel);
    lv_obj_set_size(kb, KG_W, 172);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, s_ta);
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(kb, KG_C_BG, 0);
    lv_obj_set_style_pad_all(kb, 2, 0);
    lv_obj_set_style_pad_gap(kb, 2, 0);
    lv_obj_set_style_bg_color(kb, KG_C_BTN, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, KG_C_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
    lv_obj_add_event_cb(kb, kb_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(kb, kb_cb, LV_EVENT_CANCEL, NULL);

    s_timer = lv_timer_create(tick, 500, NULL);
    lv_timer_pause(s_timer);
    return scr;
}

static void on_show(void)
{
    s_station = (int)hal_setting_get("radio_st", 0);
    if (s_station < 0 || s_station >= n_stations()) s_station = 0;
    lv_obj_add_flag(s_kb_panel, LV_OBJ_FLAG_HIDDEN);
    s_drawn_playing = false;
    refresh();
    lv_timer_resume(s_timer);
}

static void on_hide(void) { lv_timer_pause(s_timer); } /* the radio keeps playing */

const kg_app_t kg_app_radio = {
    .name = "radio",
    .label = "Radio",
    .icon = KG_ICON_RADIO,
    .create = create,
    .on_show = on_show,
    .on_hide = on_hide,
};
