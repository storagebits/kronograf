/*
 * Settings app: brightness, screen timeout, default face, 24 h format, Wi-Fi (scan +
 * password entry on the touch keyboard), iPhone notifications (BLE), STM API key and
 * followed stops, about.
 */
#include "nav.h"
#include "faces.h"
#include "kit.h"
#include "theme.h"
#include "fonts/kg_fonts.h"
#include "../hal/hal.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *s_list, *s_bright, *s_fmt24, *s_wifi_value, *s_about, *s_bus_value, *s_key_value, *s_ble_sw, *s_ble_state;
static lv_obj_t *s_timeout_btn[4], *s_face_btn[3];
static const uint16_t TIMEOUTS[4] = {5, 10, 15, 30};

/* Wi-Fi screen */
static lv_obj_t *s_wifi_scr, *s_wifi_list, *s_wifi_state, *s_kb_panel, *s_ta, *s_kb_title;
static lv_timer_t *s_scan_timer;
static char s_chosen[33];

/* "STM stop" and "STM API key" screens */
static lv_obj_t *s_stop_scr, *s_stop_ta, *s_key_scr, *s_key_ta, *s_settings_scr;

/* ------------------------------------------------------------- components */

static lv_obj_t *section(const char *title)
{
    lv_obj_t *t = kit_label(s_list, &kg_font_stencil_11, KG_C_TEXT_DIM, title);
    lv_obj_set_style_text_letter_space(t, 2, 0);
    lv_obj_set_style_pad_top(t, 8, 0);
    return t;
}

static lv_obj_t *row(lv_coord_t h)
{
    lv_obj_t *r = lv_obj_create(s_list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, 220, h);
    lv_obj_set_style_bg_color(r, KG_C_SURFACE, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(r, 10, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    return r;
}

/* Row of small mutually exclusive buttons. */
static void choose(lv_obj_t **btns, int n, int sel)
{
    for (int i = 0; i < n; i++) {
        lv_obj_set_style_bg_color(btns[i], i == sel ? KG_C_ACCENT : KG_C_BTN, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(btns[i], 0), i == sel ? KG_C_BG : KG_C_TEXT, 0);
    }
}

/* --------------------------------------------------------------- settings */

static void refresh(void)
{
    lv_slider_set_value(s_bright, hal_brightness_get(), LV_ANIM_OFF);
    int t = 1;
    for (int i = 0; i < 4; i++)
        if (TIMEOUTS[i] == hal_screen_timeout_get()) t = i;
    choose(s_timeout_btn, 4, t);
    choose(s_face_btn, 3, ui_faces_current());
    if (ui_fmt24()) lv_obj_add_state(s_fmt24, LV_STATE_CHECKED);
    else lv_obj_clear_state(s_fmt24, LV_STATE_CHECKED);
    const char *ssid = hal_wifi_ssid();
    lv_label_set_text_fmt(s_wifi_value, "%s\n%s", ssid[0] ? ssid : "No network", hal_net_status());

    if (hal_ble_enabled()) lv_obj_add_state(s_ble_sw, LV_STATE_CHECKED);
    else lv_obj_clear_state(s_ble_sw, LV_STATE_CHECKED);
    static const char *const ST[] = {"Off", "Waiting for the iPhone", "Pairing…", "Connecting…", "iPhone connected"};
    lv_label_set_text(s_ble_state, ST[hal_ble_state()]);

    const char *stops = hal_bus_stops();
    lv_label_set_text_fmt(s_bus_value, "%s", stops[0] ? stops : "No stop selected");
    char hint[16];
    hal_bus_key_hint(hint, sizeof hint);
    if (hint[0]) lv_label_set_text_fmt(s_key_value, "API key %s", hint);
    else lv_label_set_text(s_key_value, "API key: none");

    const kg_inventory_t *inv = hal_inventory();
    lv_label_set_text_fmt(s_about, "Kronograf · T-Watch 2020 V%d\nFlash %lu MB · PSRAM %lu KB", inv->rev,
                          (unsigned long)inv->flash_mb, (unsigned long)inv->psram_kb);
}

static void bright_cb(lv_event_t *e)
{
    hal_brightness_set((uint8_t)lv_slider_get_value(lv_event_get_target(e)));
}

static void timeout_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    hal_screen_timeout_set(TIMEOUTS[i]);
    choose(s_timeout_btn, 4, i);
}

static void face_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    ui_faces_set_default(i);
    choose(s_face_btn, 3, i);
}

static void fmt24_cb(lv_event_t *e)
{
    ui_set_fmt24(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
    kit_tick();
}

static void ble_cb(lv_event_t *e)
{
    hal_ble_enable(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
    kit_tick();
    refresh();
}

static void forget_cb(lv_event_t *e)
{
    (void)e;
    hal_ble_forget();
    refresh();
}

/* ------------------------------------------------------------------ Wi-Fi */

static void kb_done_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        hal_wifi_set_credentials(s_chosen, lv_textarea_get_text(s_ta));
        lv_textarea_set_text(s_ta, ""); /* the password does not stay on screen */
        lv_obj_add_flag(s_kb_panel, LV_OBJ_FLAG_HIDDEN);
        kit_tick();
        ui_nav_back(); /* back to the launcher; the status shows in Settings */
    } else if (code == LV_EVENT_CANCEL) {
        lv_textarea_set_text(s_ta, "");
        lv_obj_add_flag(s_kb_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

static void network_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    snprintf(s_chosen, sizeof s_chosen, "%s", lv_label_get_text(lv_obj_get_child(btn, 0)));
    lv_label_set_text_fmt(s_kb_title, "Password · %s", s_chosen);
    lv_obj_clear_flag(s_kb_panel, LV_OBJ_FLAG_HIDDEN);
    kit_tick();
}

static void scan_poll(lv_timer_t *t)
{
    int n = hal_wifi_scan_count();
    if (n < 0) return;
    lv_timer_pause(t);
    lv_obj_clean(s_wifi_list);
    lv_label_set_text_fmt(s_wifi_state, n ? "%d networks" : "No network found", n);
    for (int i = 0; i < n && i < 12; i++) {
        char ssid[33];
        int rssi;
        if (!hal_wifi_scan_get(i, ssid, sizeof ssid, &rssi) || !ssid[0]) continue;
        lv_obj_t *b = kit_button(s_wifi_list, "", false, 220, 36, network_cb, NULL);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_t *l = lv_obj_get_child(b, 0);
        lv_label_set_text(l, ssid);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 12, 0);
        /* Signal strength: 1 to 4 bars, as discreet text. */
        int bars = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1;
        lv_obj_t *q = kit_label(b, &kg_font_stencil_11, KG_C_TEXT_DIM, bars >= 3 ? "STRONG" : bars == 2 ? "FAIR" : "WEAK");
        lv_obj_align(q, LV_ALIGN_RIGHT_MID, -10, 0);
    }
}

static void wifi_back_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_kb_panel, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *wifi_screen(void)
{
    lv_obj_t *scr = kit_screen();
    kit_title(scr, "WI-FI");
    s_wifi_state = kit_label(scr, &kg_font_text_16, KG_C_TEXT_DIM, "Scanning…");
    lv_obj_align(s_wifi_state, LV_ALIGN_TOP_MID, 0, 22);
    s_wifi_list = lv_obj_create(scr);
    lv_obj_remove_style_all(s_wifi_list);
    lv_obj_set_size(s_wifi_list, 230, 190);
    lv_obj_align(s_wifi_list, LV_ALIGN_TOP_MID, 0, 46);
    lv_obj_set_flex_flow(s_wifi_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_wifi_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_wifi_list, 6, 0);
    lv_obj_set_scroll_dir(s_wifi_list, LV_DIR_VER);

    /* Password entry: field + full-screen keyboard. */
    s_kb_panel = lv_obj_create(scr);
    lv_obj_remove_style_all(s_kb_panel);
    lv_obj_set_size(s_kb_panel, KG_W, KG_H);
    lv_obj_set_style_bg_color(s_kb_panel, KG_C_BG, 0);
    lv_obj_set_style_bg_opa(s_kb_panel, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_kb_panel, LV_OBJ_FLAG_HIDDEN);
    s_kb_title = kit_label(s_kb_panel, &kg_font_text_16, KG_C_TEXT_DIM, "");
    lv_obj_align(s_kb_title, LV_ALIGN_TOP_MID, 0, 4);
    s_ta = lv_textarea_create(s_kb_panel);
    lv_textarea_set_one_line(s_ta, true);
    lv_textarea_set_password_mode(s_ta, true);
    lv_textarea_set_password_show_time(s_ta, 800);
    lv_obj_set_size(s_ta, 228, 38);
    lv_obj_align(s_ta, LV_ALIGN_TOP_MID, 0, 24);
    lv_obj_set_style_text_font(s_ta, &kg_font_label_20, 0);
    lv_obj_set_style_bg_color(s_ta, KG_C_SURFACE, 0);
    lv_obj_set_style_text_color(s_ta, KG_C_TEXT, 0);
    lv_obj_set_style_border_color(s_ta, KG_C_ACCENT, 0);
    lv_obj_t *kb = lv_keyboard_create(s_kb_panel);
    lv_obj_set_size(kb, KG_W, 172);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, s_ta);
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_14, 0); /* has the keyboard symbols */
    lv_obj_set_style_bg_color(kb, KG_C_BG, 0);
    lv_obj_set_style_pad_all(kb, 2, 0);
    lv_obj_set_style_pad_gap(kb, 2, 0);
    lv_obj_set_style_bg_color(kb, KG_C_BTN, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, KG_C_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
    lv_obj_add_event_cb(kb, kb_done_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(kb, kb_done_cb, LV_EVENT_CANCEL, NULL);

    ui_nav_attach_back_gesture(scr);
    lv_obj_add_event_cb(scr, wifi_back_cb, LV_EVENT_SCREEN_UNLOADED, NULL);
    s_scan_timer = lv_timer_create(scan_poll, 300, NULL);
    lv_timer_pause(s_scan_timer);
    return scr;
}

static void wifi_open_cb(lv_event_t *e)
{
    (void)e;
    if (!s_wifi_scr) s_wifi_scr = wifi_screen();
    lv_obj_clean(s_wifi_list);
    lv_label_set_text(s_wifi_state, "Scanning for networks…");
    lv_obj_add_flag(s_kb_panel, LV_OBJ_FLAG_HIDDEN);
    hal_wifi_scan_start();
    lv_timer_resume(s_scan_timer);
    lv_scr_load(s_wifi_scr);
}

/* --------------------------------------------------------------- STM stop */

static void stop_kb_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_READY) {
        /* The numeric keypad has no comma: a dot separates several stops. */
        char buf[40];
        snprintf(buf, sizeof buf, "%s", lv_textarea_get_text(s_stop_ta));
        for (char *c = buf; *c; c++)
            if (*c == '.') *c = ',';
        hal_bus_set_stops(buf);
        kit_tick();
    }
    refresh();
    lv_scr_load(s_settings_scr); /* straight back to Settings */
}

static lv_obj_t *stop_screen(void)
{
    lv_obj_t *scr = kit_screen();
    kit_title(scr, "STM STOP");
    lv_obj_t *hint = kit_label(scr, &kg_font_text_16, KG_C_TEXT_DIM, "5-digit stop code;\nuse \".\" between several stops");
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 20);
    s_stop_ta = lv_textarea_create(scr);
    lv_textarea_set_one_line(s_stop_ta, true);
    lv_textarea_set_accepted_chars(s_stop_ta, "0123456789.");
    lv_textarea_set_max_length(s_stop_ta, 17);
    lv_obj_set_size(s_stop_ta, 200, 36);
    lv_obj_align(s_stop_ta, LV_ALIGN_TOP_MID, 0, 58);
    lv_obj_set_style_text_font(s_stop_ta, &kg_font_label_20, 0);
    lv_obj_set_style_bg_color(s_stop_ta, KG_C_SURFACE, 0);
    lv_obj_set_style_text_color(s_stop_ta, KG_C_TEXT, 0);
    lv_obj_set_style_border_color(s_stop_ta, KG_C_ACCENT, 0);
    lv_obj_t *kb = lv_keyboard_create(scr);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_set_size(kb, KG_W, 140);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, s_stop_ta);
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(kb, KG_C_BG, 0);
    lv_obj_set_style_pad_all(kb, 2, 0);
    lv_obj_set_style_pad_gap(kb, 2, 0);
    lv_obj_set_style_bg_color(kb, KG_C_BTN, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, KG_C_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
    lv_obj_add_event_cb(kb, stop_kb_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(kb, stop_kb_cb, LV_EVENT_CANCEL, NULL);
    ui_nav_attach_back_gesture(scr);
    return scr;
}

static void stop_open_cb(lv_event_t *e)
{
    (void)e;
    if (!s_stop_scr) s_stop_scr = stop_screen();
    char buf[40];
    snprintf(buf, sizeof buf, "%s", hal_bus_stops());
    for (char *c = buf; *c; c++)
        if (*c == ',') *c = '.';
    lv_textarea_set_text(s_stop_ta, buf);
    lv_scr_load(s_stop_scr);
}

/* ------------------------------------------------------------ STM API key */

static void key_kb_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_READY) {
        hal_bus_set_key(lv_textarea_get_text(s_key_ta));
        kit_tick();
    }
    lv_textarea_set_text(s_key_ta, ""); /* the key does not stay on screen */
    refresh();
    lv_scr_load(s_settings_scr);
}

static lv_obj_t *key_screen(void)
{
    lv_obj_t *scr = kit_screen();
    kit_title(scr, "STM API KEY");
    lv_obj_t *hint = kit_label(scr, &kg_font_text_16, KG_C_TEXT_DIM, "From the STM developer portal.\nLeave empty to remove it.");
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 20);
    s_key_ta = lv_textarea_create(scr);
    lv_textarea_set_one_line(s_key_ta, true);
    lv_textarea_set_max_length(s_key_ta, 95);
    lv_obj_set_size(s_key_ta, 228, 36);
    lv_obj_align(s_key_ta, LV_ALIGN_TOP_MID, 0, 58);
    lv_obj_set_style_text_font(s_key_ta, &kg_font_label_20, 0);
    lv_obj_set_style_bg_color(s_key_ta, KG_C_SURFACE, 0);
    lv_obj_set_style_text_color(s_key_ta, KG_C_TEXT, 0);
    lv_obj_set_style_border_color(s_key_ta, KG_C_ACCENT, 0);
    lv_obj_t *kb = lv_keyboard_create(scr);
    lv_obj_set_size(kb, KG_W, 140);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, s_key_ta);
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(kb, KG_C_BG, 0);
    lv_obj_set_style_pad_all(kb, 2, 0);
    lv_obj_set_style_pad_gap(kb, 2, 0);
    lv_obj_set_style_bg_color(kb, KG_C_BTN, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, KG_C_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
    lv_obj_add_event_cb(kb, key_kb_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(kb, key_kb_cb, LV_EVENT_CANCEL, NULL);
    ui_nav_attach_back_gesture(scr);
    return scr;
}

static void key_open_cb(lv_event_t *e)
{
    (void)e;
    if (!s_key_scr) s_key_scr = key_screen();
    lv_textarea_set_text(s_key_ta, "");
    lv_scr_load(s_key_scr);
}

/* ----------------------------------------------------------------- screen */

static lv_obj_t *small_button(lv_obj_t *parent, const char *text, lv_coord_t w, lv_event_cb_t cb, int i)
{
    lv_obj_t *b = kit_button(parent, text, false, w, 30, cb, (void *)(intptr_t)i);
    lv_obj_set_style_text_font(lv_obj_get_child(b, 0), &kg_font_text_16, 0);
    return b;
}

static lv_obj_t *create(void)
{
    lv_obj_t *scr = s_settings_scr = kit_screen();
    s_list = lv_obj_create(scr);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_size(s_list, KG_W, KG_H);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_list, 6, 0);
    lv_obj_set_style_pad_ver(s_list, 6, 0);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);

    kit_title(s_list, "SETTINGS");
    lv_obj_set_align(lv_obj_get_child(s_list, 0), LV_ALIGN_DEFAULT);

    section("BRIGHTNESS");
    lv_obj_t *r = row(40);
    s_bright = lv_slider_create(r);
    lv_obj_set_size(s_bright, 180, 8);
    lv_obj_center(s_bright);
    lv_slider_set_range(s_bright, 8, 255);
    lv_obj_set_style_bg_color(s_bright, KG_C_BTN, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bright, KG_C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_bright, KG_C_HAND, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_bright, 6, LV_PART_KNOB);
    lv_obj_add_event_cb(s_bright, bright_cb, LV_EVENT_VALUE_CHANGED, NULL);

    section("SCREEN TIMEOUT");
    r = row(42);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    static const char *const T[] = {"5 s", "10 s", "15 s", "30 s"};
    for (int i = 0; i < 4; i++) s_timeout_btn[i] = small_button(r, T[i], 48, timeout_cb, i);

    section("DEFAULT FACE");
    r = row(42);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    static const char *const F[] = {"Aviator", "Circuits", "Night"};
    for (int i = 0; i < 3; i++) s_face_btn[i] = small_button(r, F[i], 66, face_cb, i);

    section("DISPLAY");
    r = row(42);
    lv_obj_t *l = kit_label(r, &kg_font_label_20, KG_C_TEXT, "24-hour clock");
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 14, 0);
    s_fmt24 = lv_switch_create(r);
    lv_obj_set_size(s_fmt24, 50, 26);
    lv_obj_align(s_fmt24, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_set_style_bg_color(s_fmt24, KG_C_BTN, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_fmt24, KG_C_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(s_fmt24, KG_C_HAND, LV_PART_KNOB);
    lv_obj_add_event_cb(s_fmt24, fmt24_cb, LV_EVENT_VALUE_CHANGED, NULL);

    section("IPHONE NOTIFICATIONS");
    r = row(52);
    l = kit_label(r, &kg_font_label_20, KG_C_TEXT, "Bluetooth");
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 14, 5);
    s_ble_state = kit_label(r, &kg_font_text_16, KG_C_TEXT_DIM, "");
    lv_obj_align(s_ble_state, LV_ALIGN_BOTTOM_LEFT, 14, -4);
    s_ble_sw = lv_switch_create(r);
    lv_obj_set_size(s_ble_sw, 50, 26);
    lv_obj_align(s_ble_sw, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_set_style_bg_color(s_ble_sw, KG_C_BTN, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ble_sw, KG_C_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(s_ble_sw, KG_C_HAND, LV_PART_KNOB);
    lv_obj_add_event_cb(s_ble_sw, ble_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t *forget = kit_button(s_list, "Forget the iPhone", false, 220, 36, forget_cb, NULL);
    lv_obj_set_style_text_font(lv_obj_get_child(forget, 0), &kg_font_text_16, 0);

    section("WI-FI");
    r = row(52);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(r, KG_C_BTN_PRESS, LV_STATE_PRESSED);
    lv_obj_add_event_cb(r, wifi_open_cb, LV_EVENT_CLICKED, NULL);
    s_wifi_value = kit_label(r, &kg_font_text_16, KG_C_TEXT, "");
    lv_obj_align(s_wifi_value, LV_ALIGN_LEFT_MID, 14, 0);
    lv_obj_t *chev = kit_label(r, &kg_font_label_20, KG_C_ACCENT, ">");
    lv_obj_align(chev, LV_ALIGN_RIGHT_MID, -14, 0);

    section("STM BUSES");
    r = row(42);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(r, KG_C_BTN_PRESS, LV_STATE_PRESSED);
    lv_obj_add_event_cb(r, key_open_cb, LV_EVENT_CLICKED, NULL);
    s_key_value = kit_label(r, &kg_font_text_16, KG_C_TEXT, "");
    lv_obj_align(s_key_value, LV_ALIGN_LEFT_MID, 14, 0);
    chev = kit_label(r, &kg_font_label_20, KG_C_ACCENT, ">");
    lv_obj_align(chev, LV_ALIGN_RIGHT_MID, -14, 0);
    r = row(42);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(r, KG_C_BTN_PRESS, LV_STATE_PRESSED);
    lv_obj_add_event_cb(r, stop_open_cb, LV_EVENT_CLICKED, NULL);
    s_bus_value = kit_label(r, &kg_font_text_16, KG_C_TEXT, "");
    lv_obj_align(s_bus_value, LV_ALIGN_LEFT_MID, 14, 0);
    chev = kit_label(r, &kg_font_label_20, KG_C_ACCENT, ">");
    lv_obj_align(chev, LV_ALIGN_RIGHT_MID, -14, 0);

    section("ABOUT");
    r = row(46);
    s_about = kit_label(r, &kg_font_text_16, KG_C_TEXT_DIM, "");
    lv_obj_align(s_about, LV_ALIGN_LEFT_MID, 14, 0);

    refresh();
    return scr;
}

static void on_show(void)
{
    lv_obj_scroll_to_y(s_list, 0, LV_ANIM_OFF);
    refresh();
}

const kg_app_t kg_app_settings = {
    .name = "settings",
    .label = "Settings",
    .icon = KG_ICON_SETTINGS,
    .create = create,
    .on_show = on_show,
};
