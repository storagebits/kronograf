#include "nav.h"
#include "faces.h"
#include "kit.h"
#include "model.h"
#include "notif.h"
#include "theme.h"
#include "transition.h"
#include "fonts/kg_fonts.h"
#include "../hal/hal.h"
#include "kg_tick.h"
#include <math.h>
#include <string.h>

/* Launcher order: 3 × 3 pages, everyday apps first, tools on the second page. */
static const kg_app_t *const APPS[] = {
    &kg_app_chrono, &kg_app_timer, &kg_app_alarms,
    &kg_app_world, &kg_app_weather, &kg_app_sky,
    &kg_app_steps, &kg_app_level, &kg_app_torch,
    &kg_app_net, &kg_app_remote, &kg_app_system,
    &kg_app_settings,
};
#define N_APPS ((int)(sizeof APPS / sizeof APPS[0]))

static lv_obj_t *s_launcher, *s_pages, *s_dots[4];
static lv_obj_t *s_app_scr[N_APPS];
static int s_app = -1;              /* app on screen, -1 otherwise */
static bool s_in_launcher;
static lv_obj_t *s_return_to;       /* screen to return to after an alert */

/* ------------------------------------------------------------ pictograms */

static void line(lv_draw_ctx_t *ctx, const lv_draw_line_dsc_t *d, lv_coord_t x1, lv_coord_t y1, lv_coord_t x2,
                 lv_coord_t y2)
{
    lv_point_t a = {x1, y1}, b = {x2, y2};
    lv_draw_line(ctx, d, &a, &b);
}

static void arc(lv_draw_ctx_t *ctx, lv_coord_t w, lv_color_t c, lv_coord_t cx, lv_coord_t cy, uint16_t r,
                uint16_t a0, uint16_t a1)
{
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.color = c;
    d.width = w;
    d.rounded = 1;
    lv_point_t center = {cx, cy};
    lv_draw_arc(ctx, &d, &center, r, a0, a1);
}

/* Vector pictograms, centred on (cx, cy), about 22 px. */
void ui_nav_draw_icon(lv_draw_ctx_t *ctx, kg_icon_t icon, lv_coord_t cx, lv_coord_t cy)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = KG_C_HAND;
    d.width = 2;
    d.round_start = d.round_end = 1;
    lv_draw_line_dsc_t acc = d;
    acc.color = KG_C_ACCENT;

    switch (icon) {
    case KG_ICON_CHRONO: /* case, pusher, hand */
        arc(ctx, 2, KG_C_HAND, cx, cy + 2, 10, 0, 360);
        line(ctx, &d, cx, cy - 11, cx, cy - 13);
        line(ctx, &d, cx - 3, cy - 14, cx + 3, cy - 14);
        line(ctx, &d, cx + 8, cy - 7, cx + 10, cy - 9);
        line(ctx, &acc, cx, cy + 2, cx + 4, cy - 4);
        break;
    case KG_ICON_TIMER: /* hourglass */
        line(ctx, &d, cx - 8, cy - 11, cx + 8, cy - 11);
        line(ctx, &d, cx - 8, cy + 11, cx + 8, cy + 11);
        line(ctx, &d, cx - 6, cy - 11, cx + 6, cy + 11);
        line(ctx, &d, cx + 6, cy - 11, cx - 6, cy + 11);
        line(ctx, &acc, cx - 3, cy + 8, cx + 3, cy + 8);
        break;
    case KG_ICON_ALARM: /* bell */
        arc(ctx, 2, KG_C_HAND, cx, cy, 8, 180, 360);
        line(ctx, &d, cx - 8, cy, cx - 9, cy + 7);
        line(ctx, &d, cx + 8, cy, cx + 9, cy + 7);
        line(ctx, &d, cx - 11, cy + 8, cx + 11, cy + 8);
        line(ctx, &acc, cx - 2, cy + 12, cx + 2, cy + 12);
        line(ctx, &d, cx, cy - 8, cx, cy - 10);
        break;
    case KG_ICON_STEPS: { /* two offset footprints */
        lv_draw_rect_dsc_t r;
        lv_draw_rect_dsc_init(&r);
        r.bg_color = KG_C_HAND;
        r.radius = LV_RADIUS_CIRCLE;
        lv_area_t a1 = {cx - 9, cy - 4, cx - 3, cy + 8}, a2 = {cx + 3, cy - 11, cx + 9, cy + 1};
        lv_draw_rect(ctx, &r, &a1);
        lv_draw_rect(ctx, &r, &a2);
        r.bg_color = KG_C_ACCENT;
        lv_area_t h1 = {cx - 8, cy + 10, cx - 4, cy + 13}, h2 = {cx + 4, cy + 3, cx + 8, cy + 6};
        lv_draw_rect(ctx, &r, &h1);
        lv_draw_rect(ctx, &r, &h2);
        break;
    }
    case KG_ICON_SETTINGS: /* gear */
        arc(ctx, 2, KG_C_HAND, cx, cy, 7, 0, 360);
        for (int k = 0; k < 8; k++) {
            float a = k * (float)M_PI / 4;
            line(ctx, &d, cx + (lv_coord_t)(9 * cosf(a)), cy + (lv_coord_t)(9 * sinf(a)),
                 cx + (lv_coord_t)(12 * cosf(a)), cy + (lv_coord_t)(12 * sinf(a)));
        }
        arc(ctx, 2, KG_C_ACCENT, cx, cy, 2, 0, 360);
        break;
    case KG_ICON_WEATHER: /* sun behind a cloud */
        arc(ctx, 2, KG_C_ACCENT, cx - 3, cy - 3, 6, 0, 360);
        arc(ctx, 2, KG_C_HAND, cx + 2, cy + 4, 7, 180, 360);
        line(ctx, &d, cx - 9, cy + 8, cx + 11, cy + 8);
        break;
    case KG_ICON_NET: /* Wi-Fi waves */
        arc(ctx, 2, KG_C_HAND, cx, cy + 9, 17, 225, 315);
        arc(ctx, 2, KG_C_HAND, cx, cy + 9, 11, 225, 315);
        arc(ctx, 2, KG_C_HAND, cx, cy + 9, 5, 225, 315);
        arc(ctx, 2, KG_C_ACCENT, cx, cy + 9, 1, 0, 360);
        break;
    case KG_ICON_REMOTE: /* remote control and its beam */
        line(ctx, &d, cx - 5, cy - 3, cx - 5, cy + 12);
        line(ctx, &d, cx + 5, cy - 3, cx + 5, cy + 12);
        line(ctx, &d, cx - 5, cy + 12, cx + 5, cy + 12);
        line(ctx, &d, cx - 5, cy - 3, cx + 5, cy - 3);
        arc(ctx, 2, KG_C_ACCENT, cx, cy + 2, 1, 0, 360);
        arc(ctx, 2, KG_C_ACCENT, cx, cy - 3, 6, 240, 300);
        arc(ctx, 2, KG_C_ACCENT, cx, cy - 3, 11, 245, 295);
        break;
    case KG_ICON_SYSTEM: { /* chip */
        line(ctx, &d, cx - 7, cy - 7, cx + 7, cy - 7);
        line(ctx, &d, cx - 7, cy + 7, cx + 7, cy + 7);
        line(ctx, &d, cx - 7, cy - 7, cx - 7, cy + 7);
        line(ctx, &d, cx + 7, cy - 7, cx + 7, cy + 7);
        for (int k = -4; k <= 4; k += 4) {
            line(ctx, &d, cx + k, cy - 8, cx + k, cy - 12);
            line(ctx, &d, cx + k, cy + 8, cx + k, cy + 12);
            line(ctx, &d, cx - 8, cy + k, cx - 12, cy + k);
            line(ctx, &d, cx + 8, cy + k, cx + 12, cy + k);
        }
        lv_draw_rect_dsc_t r;
        lv_draw_rect_dsc_init(&r);
        r.bg_color = KG_C_ACCENT;
        lv_area_t c = {cx - 2, cy - 2, cx + 2, cy + 2};
        lv_draw_rect(ctx, &r, &c);
        break;
    }
    case KG_ICON_LEVEL: /* spirit level: tube and bubble */
        arc(ctx, 2, KG_C_HAND, cx, cy, 11, 0, 360);
        line(ctx, &d, cx - 11, cy, cx - 6, cy);
        line(ctx, &d, cx + 6, cy, cx + 11, cy);
        line(ctx, &d, cx, cy - 11, cx, cy - 6);
        line(ctx, &d, cx, cy + 6, cx, cy + 11);
        arc(ctx, 2, KG_C_ACCENT, cx + 2, cy - 2, 3, 0, 360);
        break;
    case KG_ICON_WORLD: /* globe */
        arc(ctx, 2, KG_C_HAND, cx, cy, 11, 0, 360);
        line(ctx, &d, cx - 11, cy, cx + 11, cy);
        line(ctx, &d, cx - 9, cy - 6, cx + 9, cy - 6);
        line(ctx, &d, cx - 9, cy + 6, cx + 9, cy + 6);
        arc(ctx, 2, KG_C_ACCENT, cx + 12, cy, 16, 137, 223);
        arc(ctx, 2, KG_C_ACCENT, cx - 12, cy, 16, 317, 43);
        break;
    case KG_ICON_SKY: /* sunrise */
        arc(ctx, 2, KG_C_ACCENT, cx, cy + 5, 6, 180, 360);
        line(ctx, &d, cx - 12, cy + 6, cx + 12, cy + 6);
        line(ctx, &acc, cx, cy - 4, cx, cy - 8);
        line(ctx, &acc, cx - 8, cy - 2, cx - 10, cy - 4);
        line(ctx, &acc, cx + 8, cy - 2, cx + 10, cy - 4);
        line(ctx, &d, cx - 7, cy + 11, cx + 7, cy + 11);
        break;
    case KG_ICON_TORCH: /* flashlight */
        line(ctx, &d, cx - 3, cy, cx - 3, cy + 12);
        line(ctx, &d, cx + 3, cy, cx + 3, cy + 12);
        line(ctx, &d, cx - 3, cy + 12, cx + 3, cy + 12);
        line(ctx, &d, cx - 3, cy, cx - 7, cy - 5);
        line(ctx, &d, cx + 3, cy, cx + 7, cy - 5);
        line(ctx, &d, cx - 7, cy - 5, cx + 7, cy - 5);
        line(ctx, &acc, cx, cy - 8, cx, cy - 12);
        line(ctx, &acc, cx - 6, cy - 8, cx - 9, cy - 11);
        line(ctx, &acc, cx + 6, cy - 8, cx + 9, cy - 11);
        break;
    }
}

static void icon_draw_cb(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target(e);
    const kg_app_t *app = lv_event_get_user_data(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    ui_nav_draw_icon(lv_event_get_draw_ctx(e), app->icon, (a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
}

/* --------------------------------------------------------------- launcher */

static void open_app_cb(lv_event_t *e)
{
    const kg_app_t *app = lv_event_get_user_data(e);
    ui_nav_open_app(app->name);
}

static void launcher_gesture(lv_event_t *e)
{
    (void)e;
    if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_BOTTOM) {
        lv_indev_wait_release(lv_indev_get_act());
        ui_nav_close_launcher();
    }
}

#define PER_PAGE 9
#define N_PAGES ((N_APPS + PER_PAGE - 1) / PER_PAGE)

static void pages_scrolled(lv_event_t *e)
{
    (void)e;
    int page = (lv_obj_get_scroll_x(s_pages) + KG_W / 2) / KG_W;
    for (int i = 0; i < N_PAGES; i++)
        lv_obj_set_style_bg_color(s_dots[i], i == page ? KG_C_ACCENT : KG_C_TRACK, 0);
}

static lv_obj_t *launcher_create(void)
{
    lv_obj_t *scr = kit_screen();
    /* Pages side by side in a horizontally scrolling strip that snaps one page at a time. A
     * vertical swipe does not scroll it, so it still reaches the screen (close). */
    s_pages = lv_obj_create(scr);
    lv_obj_remove_style_all(s_pages);
    lv_obj_set_size(s_pages, KG_W, KG_H);
    lv_obj_set_scroll_dir(s_pages, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(s_pages, LV_SCROLL_SNAP_START);
    lv_obj_add_flag(s_pages, LV_OBJ_FLAG_SCROLL_ONE);
    lv_obj_set_scrollbar_mode(s_pages, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_pages, pages_scrolled, LV_EVENT_SCROLL, NULL);
    lv_obj_t *page = NULL;
    for (int i = 0; i < N_APPS; i++) {
        if (i % PER_PAGE == 0) {
            page = lv_obj_create(s_pages);
            lv_obj_remove_style_all(page);
            lv_obj_set_size(page, KG_W, KG_H);
            lv_obj_set_pos(page, (i / PER_PAGE) * KG_W, 0);
            lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
        }
        int k = i % PER_PAGE;
        lv_coord_t cx = 42 + (k % 3) * 78, cy = 37 + (k / 3) * 76;
        /* Round sub-dial-style button: recessed background, rim, pictogram. */
        lv_obj_t *b = lv_btn_create(page);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, 48, 48);
        lv_obj_set_pos(b, cx - 24, cy - 24);
        lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(b, KG_C_SUBDIAL, 0);
        lv_obj_set_style_bg_grad_color(b, KG_C_GUILLOCHE, 0);
        lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(b, KG_C_TRACK, 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_bg_color(b, KG_C_BTN_PRESS, LV_STATE_PRESSED);
        lv_obj_set_style_transform_width(b, -3, LV_STATE_PRESSED);
        lv_obj_set_style_transform_height(b, -3, LV_STATE_PRESSED);
        lv_obj_add_event_cb(b, icon_draw_cb, LV_EVENT_DRAW_MAIN_END, (void *)APPS[i]);
        lv_obj_add_event_cb(b, open_app_cb, LV_EVENT_CLICKED, (void *)APPS[i]);
        lv_obj_add_event_cb(b, (lv_event_cb_t)kit_tick, LV_EVENT_PRESSED, NULL);

        lv_obj_t *l = kit_label(page, &kg_font_text_16, KG_C_TEXT, APPS[i]->label);
        lv_obj_set_width(l, 78);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(l, cx - 39, cy + 25);
    }
    /* Page dots, at the top edge. */
    for (int i = 0; i < N_PAGES; i++) {
        s_dots[i] = lv_obj_create(scr);
        lv_obj_remove_style_all(s_dots[i]);
        lv_obj_set_size(s_dots[i], 5, 5);
        lv_obj_set_style_radius(s_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(s_dots[i], LV_OPA_COVER, 0);
        lv_obj_set_pos(s_dots[i], KG_W / 2 - N_PAGES * 5 + i * 10, 3);
        lv_obj_clear_flag(s_dots[i], LV_OBJ_FLAG_CLICKABLE);
    }
    pages_scrolled(NULL);
    lv_obj_add_event_cb(scr, launcher_gesture, LV_EVENT_GESTURE, NULL);
    return scr;
}

void ui_nav_launcher_page(int page)
{
    if (!s_launcher) s_launcher = launcher_create();
    lv_obj_scroll_to_x(s_pages, page * KG_W, LV_ANIM_OFF);
    pages_scrolled(NULL);
}

void ui_nav_open_launcher(void)
{
    if (!s_launcher) s_launcher = launcher_create();
    ui_nav_launcher_page(0); /* from a face, always the first page */
    if (kg_transition(s_launcher, KG_SLIDE_UP)) {
        s_in_launcher = true;
        ui_faces_set_visible(false);
    }
}

void ui_nav_close_launcher(void)
{
    s_in_launcher = false;
    ui_faces_refresh_current(); /* also applies a default face changed in Settings */
    kg_transition(ui_faces_screen(ui_faces_current()), KG_SLIDE_DOWN);
}

/* -------------------------------------------------------------------- apps */

static void app_gesture(lv_event_t *e)
{
    (void)e;
    if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());
        ui_nav_back();
    }
}

void ui_nav_attach_back_gesture(lv_obj_t *scr) { lv_obj_add_event_cb(scr, app_gesture, LV_EVENT_GESTURE, NULL); }

bool ui_nav_open_app(const char *name)
{
    for (int i = 0; i < N_APPS; i++) {
        if (strcmp(APPS[i]->name, name)) continue;
        if (!s_app_scr[i]) {
            s_app_scr[i] = APPS[i]->create();
            ui_nav_attach_back_gesture(s_app_scr[i]);
        }
        if (APPS[i]->on_show) APPS[i]->on_show();
        if (!s_launcher) s_launcher = launcher_create();
        if (!kg_transition(s_app_scr[i], KG_SLIDE_LEFT)) return false;
        if (ui_nav_on_face()) ui_faces_set_visible(false);
        s_app = i;
        s_in_launcher = false;
        return true;
    }
    return false;
}

void ui_nav_back(void)
{
    if (s_app >= 0 && APPS[s_app]->on_hide) APPS[s_app]->on_hide();
    s_app = -1;
    if (!s_launcher) s_launcher = launcher_create();
    if (kg_transition(s_launcher, KG_SLIDE_RIGHT)) s_in_launcher = true;
}

bool ui_nav_on_face(void) { return s_app < 0 && !s_in_launcher; }

/* ----------------------------------------------------------------- alerts */

static lv_obj_t *s_alert, *s_alert_title, *s_alert_time, *s_alert_snooze;
static lv_timer_t *s_ring_timer;
static uint32_t s_ring_started;
static bool s_alert_is_alarm;

static void ring_once(void)
{
    static const uint16_t vib[] = {400, 200, 400, 600};
    hal_vibrate_pattern(vib, 4);
    if (s_alert_is_alarm) {
        /* Rising synthesised chime: a third, then a fifth. */
        static const kg_tone_t chime[] = {{880, 120}, {0, 40}, {1109, 120}, {0, 40}, {1319, 260}, {0, 300}};
        hal_tones(chime, 6);
    } else {
        static const kg_tone_t beeps[] = {{1760, 90}, {0, 90}, {1760, 90}, {0, 90}, {1760, 90}, {0, 400}};
        hal_tones(beeps, 6);
    }
}

static void stop_ringing(void)
{
    if (s_ring_timer) lv_timer_pause(s_ring_timer);
    lv_obj_t *back = s_return_to;
    s_return_to = NULL;
    if (back) kg_transition(back, KG_SLIDE_DOWN);
}

static void ring_tick(lv_timer_t *t)
{
    (void)t;
    lv_disp_trig_activity(NULL); /* the screen stays on while it rings */
    if (kg_tick_ms() - s_ring_started > 60000) { /* one minute without an answer: automatic snooze */
        if (s_alert_is_alarm) model_alarm_snooze();
        stop_ringing();
        return;
    }
    ring_once();
}

static void dismiss_cb(lv_event_t *e)
{
    (void)e;
    if (s_alert_is_alarm) model_alarm_dismiss();
    else model_timer_reset();
    stop_ringing();
}

static void snooze_cb(lv_event_t *e)
{
    (void)e;
    model_alarm_snooze();
    stop_ringing();
}

static void show_alert(bool alarm)
{
    if (!s_alert) {
        s_alert = kit_screen();
        lv_obj_set_style_bg_color(s_alert, KG_C_ALERT_BG, 0);
        s_alert_title = kit_label(s_alert, &kg_font_title_34, KG_C_ACCENT, "");
        lv_obj_align(s_alert_title, LV_ALIGN_TOP_MID, 0, 18);
        s_alert_time = kit_label(s_alert, &kg_font_digits_56, KG_C_TEXT, "");
        lv_obj_align(s_alert_time, LV_ALIGN_CENTER, 0, -8);
        lv_obj_t *stop = kit_button(s_alert, "Stop", true, 104, 44, dismiss_cb, NULL);
        lv_obj_align(stop, LV_ALIGN_BOTTOM_RIGHT, -10, -12);
        s_alert_snooze = kit_button(s_alert, "+5 min", false, 104, 44, snooze_cb, NULL);
        lv_obj_align(s_alert_snooze, LV_ALIGN_BOTTOM_LEFT, 10, -12);
        s_ring_timer = lv_timer_create(ring_tick, 2200, NULL);
    }
    s_alert_is_alarm = alarm;
    struct tm now;
    hal_now(&now, NULL);
    lv_label_set_text(s_alert_title, alarm ? "Alarm" : "Timer");
    if (alarm) lv_label_set_text_fmt(s_alert_time, "%02d:%02d", now.tm_hour, now.tm_min);
    else lv_label_set_text(s_alert_time, "00:00");
    if (alarm) lv_obj_clear_flag(s_alert_snooze, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_alert_snooze, LV_OBJ_FLAG_HIDDEN);

    if (lv_scr_act() != s_alert) {
        s_return_to = lv_scr_act();
        if (!kg_transition(s_alert, KG_SLIDE_UP)) lv_scr_load(s_alert);
    }
    s_ring_started = kg_tick_ms();
    lv_timer_resume(s_ring_timer);
    ring_once();
}

void ui_nav_test_alert(bool alarm) { show_alert(alarm); }

/* The shell: watches the model and schedules the next hardware wake-up. It only runs
 * with the screen on, which is when it refreshes the complication's weather if it is more
 * than 30 min old (never any Wi-Fi with the screen off). */
#define WX_COMPLICATION_MAX_AGE_S (30 * 60)

static void shell_tick(lv_timer_t *t)
{
    static uint32_t n;
    (void)t;
    if (n++ % 40 == 8) hal_weather_refresh(WX_COMPLICATION_MAX_AGE_S); /* every 10 s */
    kg_event_t ev = model_poll();
    if (ev == KG_EVT_ALARM) show_alert(true);
    else if (ev == KG_EVT_TIMER_DONE) show_alert(false);
    else if (!s_return_to) ui_notif_poll(); /* no card over a ringing alarm */
    hal_schedule_wakeup(model_next_event_time());
}

static void face_gesture(lv_event_t *e)
{
    (void)e;
    lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (d == LV_DIR_TOP) {
        lv_indev_wait_release(lv_indev_get_act());
        ui_nav_open_launcher();
    } else if (d == LV_DIR_BOTTOM) {
        lv_indev_wait_release(lv_indev_get_act());
        ui_notif_open_list();
    }
}

void ui_nav_init(void)
{
    model_alarms_load();
    ui_notif_init();
    for (int i = 0; i < ui_faces_count(); i++) lv_obj_add_event_cb(ui_faces_screen(i), face_gesture, LV_EVENT_GESTURE, NULL);
    lv_timer_create(shell_tick, 250, NULL);
}
