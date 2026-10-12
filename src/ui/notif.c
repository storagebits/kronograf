/*
 * Notifications iPhone : carte plein écran à l'arrivée (vibration), liste des dernières
 * notifications (glissé vers le bas depuis un cadran), écran de code d'appairage.
 * Tout est lu dans la HAL, sondée par le shell (nav.c) — aucun rappel depuis la pile BLE.
 */
#include "notif.h"
#include "faces.h"
#include "kit.h"
#include "nav.h"
#include "theme.h"
#include "transition.h"
#include "fonts/kg_fonts.h"
#include "../hal/hal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static lv_obj_t *s_card, *s_card_icon, *s_card_app, *s_card_title, *s_card_msg, *s_card_clear, *s_card_ok;
static lv_obj_t *s_list_scr, *s_list, *s_list_state;
static lv_obj_t *s_pair_scr, *s_pair_code;
static lv_obj_t *s_return_to;        /* écran à retrouver après la carte ou l'appairage */
static uint32_t s_card_uid, s_seen_seq;
static bool s_card_from_list;

/* ------------------------------------------------------------ pictogrammes */

static void line(lv_draw_ctx_t *ctx, lv_color_t c, lv_coord_t w, lv_coord_t x1, lv_coord_t y1, lv_coord_t x2,
                 lv_coord_t y2)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = c;
    d.width = w;
    d.round_start = d.round_end = 1;
    lv_point_t a = {x1, y1}, b = {x2, y2};
    lv_draw_line(ctx, &d, &a, &b);
}

static void frame(lv_draw_ctx_t *ctx, lv_color_t c, lv_coord_t x1, lv_coord_t y1, lv_coord_t x2, lv_coord_t y2,
                  lv_coord_t r)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_opa = LV_OPA_TRANSP;
    d.border_color = c;
    d.border_width = 2;
    d.radius = r;
    lv_area_t a = {x1, y1, x2, y2};
    lv_draw_rect(ctx, &d, &a);
}

/* Pictogramme de catégorie (≈ 20 px) centré en (cx, cy). */
static void draw_category(lv_draw_ctx_t *ctx, uint8_t cat, lv_coord_t cx, lv_coord_t cy)
{
    lv_color_t c = KG_C_ACCENT;
    switch (cat) {
    case KG_CAT_SOCIAL: /* bulle de dialogue */
        frame(ctx, c, cx - 9, cy - 7, cx + 9, cy + 5, 5);
        line(ctx, c, 2, cx - 4, cy + 5, cx - 6, cy + 9);
        break;
    case KG_CAT_EMAIL: /* enveloppe */
        frame(ctx, c, cx - 9, cy - 6, cx + 9, cy + 6, 2);
        line(ctx, c, 2, cx - 8, cy - 5, cx, cy + 1);
        line(ctx, c, 2, cx + 8, cy - 5, cx, cy + 1);
        break;
    case KG_CAT_CALL:
    case KG_CAT_MISSED_CALL:
    case KG_CAT_VOICEMAIL: { /* combiné : arc épais aux extrémités renflées */
        lv_draw_arc_dsc_t a;
        lv_draw_arc_dsc_init(&a);
        a.color = cat == KG_CAT_MISSED_CALL ? KG_C_ALERT : c;
        a.width = 4;
        a.rounded = 1;
        lv_point_t center = {cx + 3, cy - 3};
        lv_draw_arc(ctx, &a, &center, 10, 100, 170);
        break;
    }
    case KG_CAT_SCHEDULE: /* calendrier */
        frame(ctx, c, cx - 8, cy - 6, cx + 8, cy + 8, 2);
        line(ctx, c, 2, cx - 7, cy - 2, cx + 7, cy - 2);
        line(ctx, c, 2, cx - 4, cy - 9, cx - 4, cy - 5);
        line(ctx, c, 2, cx + 4, cy - 9, cx + 4, cy - 5);
        break;
    case KG_CAT_NEWS: /* page et lignes */
        frame(ctx, c, cx - 8, cy - 8, cx + 8, cy + 8, 2);
        for (int i = 0; i < 3; i++) line(ctx, c, 2, cx - 4, cy - 3 + i * 4, cx + 4, cy - 3 + i * 4);
        break;
    default: { /* cloche simplifiée : point plein */
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.bg_color = c;
        d.radius = LV_RADIUS_CIRCLE;
        lv_area_t a = {cx - 5, cy - 5, cx + 5, cy + 5};
        lv_draw_rect(ctx, &d, &a);
        break;
    }
    }
}

static void icon_draw_cb(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    draw_category(lv_event_get_draw_ctx(e), (uint8_t)(uintptr_t)lv_obj_get_user_data(o), (a.x1 + a.x2) / 2,
                  (a.y1 + a.y2) / 2);
}

static lv_obj_t *category_icon(lv_obj_t *parent, lv_coord_t size)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, size, size);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, KG_C_SUBDIAL, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(o, KG_C_HAIRLINE, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(o, icon_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
    return o;
}

/* « 20261012T171205 » → « 17:12 » (ou date courte si ce n'est pas aujourd'hui). */
static void fmt_when(char *buf, size_t n, const char *date)
{
    int y, mo, d, h, mi;
    if (sscanf(date, "%4d%2d%2dT%2d%2d", &y, &mo, &d, &h, &mi) != 5) {
        buf[0] = 0;
        return;
    }
    struct tm now;
    hal_now(&now, NULL);
    if (now.tm_mday == d && now.tm_mon + 1 == mo) snprintf(buf, n, "%02d:%02d", ui_display_hour(h), mi);
    else snprintf(buf, n, "%d/%02d %02d:%02d", d, mo, ui_display_hour(h), mi);
}

/* En-tête « MESSAGES · 17:12 » au pochoir : capitales ASCII et accents disponibles. */
static void fmt_header(char *buf, size_t n, const kg_notif_t *nt)
{
    char app[40], when[16];
    const char *name = kg_ancs_app_name(nt->app, app, sizeof app);
    fmt_when(when, sizeof when, nt->date);
    snprintf(buf, n, "%s   %s", name, when);
    for (char *c = buf; *c; c++) {
        if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);
        else if ((unsigned char)c[0] == 0xC3 && c[1]) { /* é è à ç ê î ô → capitales latin-1 */
            unsigned char lo = (unsigned char)c[1];
            if (lo >= 0xA0 && lo <= 0xBF) c[1] = (char)(lo - 0x20);
            c++;
        }
    }
}

/* ------------------------------------------------------------------ carte */

static void leave_to(lv_obj_t *back, kg_slide_t dir)
{
    if (!back) back = ui_faces_screen(ui_faces_current());
    if (back == ui_faces_screen(ui_faces_current())) ui_faces_refresh_current();
    if (!kg_transition(back, dir)) lv_scr_load(back);
}

static void card_close(void)
{
    lv_obj_t *back = s_card_from_list ? s_list_scr : s_return_to;
    s_return_to = NULL;
    if (s_card_from_list) ui_notif_open_list();
    else leave_to(back, KG_SLIDE_DOWN);
}

static void card_ok_cb(lv_event_t *e)
{
    (void)e;
    card_close();
}

static void card_clear_cb(lv_event_t *e)
{
    (void)e;
    hal_notif_dismiss(s_card_uid); /* efface sur l'iPhone ; refuse un appel entrant */
    card_close();
}

static void card_gesture(lv_event_t *e)
{
    (void)e;
    lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (d == LV_DIR_BOTTOM || d == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());
        card_close();
    }
}

static void card_create(void)
{
    s_card = kit_screen();
    s_card_icon = category_icon(s_card, 34);
    lv_obj_set_pos(s_card_icon, 10, 8);
    s_card_app = kit_label(s_card, &kg_font_stencil_11, KG_C_TEXT_DIM, "");
    lv_obj_set_style_text_letter_space(s_card_app, 1, 0);
    lv_obj_set_pos(s_card_app, 52, 19);
    s_card_title = kit_label(s_card, &kg_font_label_20, KG_C_TEXT, "");
    lv_obj_set_width(s_card_title, 220);
    lv_label_set_long_mode(s_card_title, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(s_card_title, 10, 48);

    /* Message : zone défilante si le texte est long. */
    lv_obj_t *box = lv_obj_create(s_card);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, 224, 116);
    lv_obj_set_pos(box, 10, 74);
    lv_obj_set_scroll_dir(box, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_AUTO);
    s_card_msg = kit_label(box, &kg_font_text_16, KG_C_TEXT, "");
    lv_obj_set_width(s_card_msg, 216);
    lv_label_set_long_mode(s_card_msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(s_card_msg, 2, 0);

    s_card_clear = kit_button(s_card, "Effacer", false, 104, 40, card_clear_cb, NULL);
    lv_obj_align(s_card_clear, LV_ALIGN_BOTTOM_LEFT, 10, -6);
    s_card_ok = kit_button(s_card, "OK", true, 104, 40, card_ok_cb, NULL);
    lv_obj_align(s_card_ok, LV_ALIGN_BOTTOM_RIGHT, -10, -6);
    lv_obj_add_event_cb(s_card, card_gesture, LV_EVENT_GESTURE, NULL);
}

static void card_show(const kg_notif_t *n, bool from_list)
{
    if (!s_card) card_create();
    char buf[96];
    s_card_uid = n->uid;
    s_card_from_list = from_list;
    lv_obj_set_user_data(s_card_icon, (void *)(uintptr_t)n->category);
    lv_obj_invalidate(s_card_icon);
    fmt_header(buf, sizeof buf, n);
    lv_label_set_text(s_card_app, buf);
    lv_label_set_text(s_card_title, n->title[0] ? n->title : "Notification");
    lv_label_set_text(s_card_msg, n->message);
    lv_obj_scroll_to_y(lv_obj_get_parent(s_card_msg), 0, LV_ANIM_OFF);
    bool call = n->category == KG_CAT_CALL;
    kit_button_set_text(s_card_clear, call ? "Refuser" : "Effacer");
    lv_obj_set_style_bg_color(s_card_clear, call ? KG_C_ALERT : KG_C_BTN, 0);

    if (lv_scr_act() != s_card) {
        if (!from_list) s_return_to = lv_scr_act();
        if (ui_nav_on_face()) ui_faces_set_visible(false);
        if (!kg_transition(s_card, from_list ? KG_SLIDE_LEFT : KG_SLIDE_UP)) lv_scr_load(s_card);
    }
}

/* ------------------------------------------------------------------ liste */

static void item_cb(lv_event_t *e)
{
    kg_notif_t n;
    if (hal_notif_get((int)(intptr_t)lv_event_get_user_data(e), &n)) card_show(&n, true);
}

static void list_gesture(lv_event_t *e)
{
    (void)e;
    lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (d == LV_DIR_RIGHT || d == LV_DIR_TOP) {
        lv_indev_wait_release(lv_indev_get_act());
        leave_to(NULL, KG_SLIDE_UP);
    }
}

static const char *ble_hint(void)
{
    switch (hal_ble_state()) {
    case KG_BLE_OFF: return "Notifications iPhone désactivées\n(Réglages de la montre)";
    case KG_BLE_WAITING: return "iPhone non connecté.\nPour appairer, sur l'iPhone :\nRéglages, Bluetooth, « Kronograf »";
    case KG_BLE_PAIRING: return "Appairage en cours…";
    case KG_BLE_CONNECTED: return "Connexion à l'iPhone…";
    default: return "Aucune notification";
    }
}

static void list_fill(void)
{
    lv_obj_clean(s_list);
    int n = hal_notif_count();
    kg_ble_state_t st = hal_ble_state();
    lv_label_set_text(s_list_state, st == KG_BLE_READY ? "IPHONE CONNECTÉ" : st == KG_BLE_OFF ? "BLUETOOTH COUPÉ" : "IPHONE HORS LIGNE");
    lv_obj_set_style_text_color(s_list_state, st == KG_BLE_READY ? KG_C_OK : KG_C_TEXT_DIM, 0);
    if (!n) {
        lv_obj_t *l = kit_label(s_list, &kg_font_text_16, KG_C_TEXT_DIM, ble_hint());
        lv_obj_set_width(l, 220);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(l, 50, 0);
        return;
    }
    for (int i = 0; i < n; i++) {
        kg_notif_t nt;
        if (!hal_notif_get(i, &nt)) break;
        lv_obj_t *b = lv_obj_create(s_list);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, 224, 62);
        lv_obj_set_style_bg_color(b, KG_C_SURFACE, 0);
        lv_obj_set_style_bg_color(b, KG_C_BTN_PRESS, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(b, 10, 0);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(b, item_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(b, (lv_event_cb_t)kit_tick, LV_EVENT_PRESSED, NULL);
        lv_obj_t *ic = category_icon(b, 26);
        lv_obj_set_user_data(ic, (void *)(uintptr_t)nt.category);
        lv_obj_set_pos(ic, 6, 6);
        char hdr[96];
        fmt_header(hdr, sizeof hdr, &nt);
        lv_obj_t *h = kit_label(b, &kg_font_stencil_11, KG_C_TEXT_DIM, hdr);
        lv_obj_set_pos(h, 40, 6);
        lv_obj_t *t = kit_label(b, &kg_font_text_16, KG_C_TEXT, nt.title[0] ? nt.title : "Notification");
        lv_obj_set_width(t, 176);
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(t, 40, 20);
        lv_obj_t *m = kit_label(b, &kg_font_text_16, KG_C_TEXT_DIM, nt.message);
        lv_obj_set_width(m, 176);
        lv_label_set_long_mode(m, LV_LABEL_LONG_DOT);
        lv_obj_set_height(m, 19);
        lv_obj_set_pos(m, 40, 39);
    }
}

void ui_notif_open_list(void)
{
    if (!s_list_scr) {
        s_list_scr = kit_screen();
        kit_title(s_list_scr, "NOTIFICATIONS");
        s_list_state = kit_label(s_list_scr, &kg_font_stencil_11, KG_C_TEXT_DIM, "");
        lv_obj_align(s_list_state, LV_ALIGN_TOP_MID, 0, 22);
        s_list = lv_obj_create(s_list_scr);
        lv_obj_remove_style_all(s_list);
        lv_obj_set_size(s_list, KG_W, KG_H - 40);
        lv_obj_set_pos(s_list, 0, 40);
        lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(s_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(s_list, 6, 0);
        lv_obj_set_style_pad_bottom(s_list, 8, 0);
        lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);
        lv_obj_add_event_cb(s_list_scr, list_gesture, LV_EVENT_GESTURE, NULL);
    }
    list_fill();
    lv_obj_scroll_to_y(s_list, 0, LV_ANIM_OFF);
    if (lv_scr_act() == s_list_scr) return;
    bool from_card = lv_scr_act() == s_card;
    if (ui_nav_on_face()) ui_faces_set_visible(false);
    if (!kg_transition(s_list_scr, from_card ? KG_SLIDE_RIGHT : KG_SLIDE_DOWN)) lv_scr_load(s_list_scr);
}

/* -------------------------------------------------------------- appairage */

static void pair_show(void)
{
    if (!s_pair_scr) {
        s_pair_scr = kit_screen();
        kit_title(s_pair_scr, "APPAIRAGE IPHONE");
        lv_obj_t *l = kit_label(s_pair_scr, &kg_font_text_16, KG_C_TEXT_DIM, "Saisissez ce code\nsur l'iPhone");
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 40);
        s_pair_code = kit_label(s_pair_scr, &kg_font_digits_56, KG_C_ACCENT, "");
        lv_obj_align(s_pair_code, LV_ALIGN_CENTER, 0, 18);
    }
    uint32_t k = hal_ble_passkey();
    lv_label_set_text_fmt(s_pair_code, "%03lu %03lu", (unsigned long)(k / 1000), (unsigned long)(k % 1000));
    if (lv_scr_act() != s_pair_scr) {
        s_return_to = lv_scr_act();
        if (ui_nav_on_face()) ui_faces_set_visible(false);
        if (!kg_transition(s_pair_scr, KG_SLIDE_UP)) lv_scr_load(s_pair_scr);
    }
}

/* ------------------------------------------------------------------ sonde */

void ui_notif_poll(void)
{
    if (kg_transition_running()) return;
    lv_obj_t *act = lv_scr_act();

    /* Appairage : le code reste affiché tant que l'iPhone le demande. */
    bool pairing = hal_ble_state() == KG_BLE_PAIRING;
    if (pairing && act != s_pair_scr) {
        pair_show();
        hal_vibrate(60);
        return;
    }
    if (!pairing && s_pair_scr && act == s_pair_scr) {
        lv_obj_t *back = s_return_to;
        s_return_to = NULL;
        leave_to(back, KG_SLIDE_DOWN);
        return;
    }

    uint32_t seq = hal_notif_seq();
    if (seq != s_seen_seq) {
        s_seen_seq = seq;
        kg_notif_t n;
        if (hal_notif_get(0, &n)) {
            static const uint16_t vib[] = {80, 120, 80};
            hal_vibrate_pattern(vib, 3);
            card_show(&n, false);
            lv_disp_trig_activity(NULL);
        }
        return;
    }
    if (act == s_list_scr) { /* liste ouverte : suit les ajouts et retraits */
        static int last_n = -1;
        static kg_ble_state_t last_st;
        int n = hal_notif_count();
        if (n != last_n || hal_ble_state() != last_st) {
            last_n = n;
            last_st = hal_ble_state();
            list_fill();
        }
    }
}

void ui_notif_init(void) { s_seen_seq = hal_notif_seq(); }
