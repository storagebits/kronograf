/*
 * Face 2 — "Time circuits".
 *
 * Three rows of 7-segment displays, each with stencilled labels:
 *   red    DESTINATION TIME    next alarm (dashes when there is none)
 *   green  PRESENT TIME        current date and time, ':' beating the seconds
 *   amber  LAST TIME DEPARTED  last NTP synchronisation
 *
 * Easter egg: long press on the green row, or 10,000 steps reached → electrical
 * discharge (animated lightning).
 */
#include "ui.h"
#include "faces.h"
#include "model.h"
#include "seg7.h"
#include "theme.h"
#include "fonts/kg_fonts.h"
#include "../hal/hal.h"
#include <stdio.h>
#include <stdlib.h>

#define ROW_H 79
#define DIGIT_W 19
#define DIGIT_H 34
#define STEPS_EGG 10000

typedef struct {
    lv_obj_t *day, *month, *time;
} row_t;

static lv_obj_t *s_scr, *s_bolts;
static row_t s_dest, s_now, s_last;
static lv_obj_t *s_dest_title;
static lv_timer_t *s_timer;
static int s_last_min = -1, s_last_sec = -1;
static bool s_egg_done_today;

/* ----------------------------------------------------------------- layout */

static lv_obj_t *stencil(lv_obj_t *parent, const char *txt, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, &kg_font_stencil_11, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    lv_label_set_text(l, txt);
    return l;
}

/* Small label plate above a display group. */
static void plate(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, const char *txt)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_remove_style_all(p);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_size(p, w, 13);
    lv_obj_set_style_bg_color(p, KG_C_BAND, 0);
    lv_obj_set_style_bg_grad_color(p, lv_color_darken(KG_C_BAND, 40), 0);
    lv_obj_set_style_bg_grad_dir(p, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(p, 2, 0);
    lv_obj_t *l = stencil(p, txt, KG_C_BAND_TEXT);
    lv_obj_center(l);
}

static lv_obj_t *window(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_remove_style_all(p);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_size(p, w, DIGIT_H + 8);
    lv_obj_set_style_bg_color(p, KG_C_PANEL, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(p, KG_C_PANEL_EDGE, 0);
    lv_obj_set_style_border_width(p, 1, 0);
    lv_obj_set_style_radius(p, 3, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return p;
}

static lv_obj_t *group(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, int ndigits, int colon_after,
                       lv_color_t color)
{
    lv_obj_t *win = window(parent, x, y, w);
    lv_obj_t *seg = seg7_create(win, ndigits, DIGIT_W, DIGIT_H, colon_after, color);
    lv_obj_center(seg);
    return seg;
}

static lv_obj_t *row(lv_obj_t *parent, lv_coord_t y, lv_color_t color, const char *title, row_t *r)
{
    const lv_coord_t x_month = 8, x_day = 62, x_time = 116, w2 = 50, w4 = 116;
    plate(parent, x_month, y, w2, "MONTH");
    plate(parent, x_day, y, w2, "DAY");
    plate(parent, x_time, y, w4, "HOUR   MIN");
    r->day = group(parent, x_day, y + 15, w2, 2, -1, color);
    r->month = group(parent, x_month, y + 15, w2, 2, -1, color);
    r->time = group(parent, x_time, y + 15, w4, 4, 1, color);

    /* Title band below the displays, in the row's colour. */
    lv_obj_t *band = lv_obj_create(parent);
    lv_obj_remove_style_all(band);
    lv_obj_set_pos(band, 8, y + 15 + DIGIT_H + 10);
    lv_obj_set_size(band, 224, 12);
    lv_obj_set_style_bg_color(band, color, 0);
    lv_obj_set_style_bg_opa(band, LV_OPA_20, 0);
    lv_obj_set_style_radius(band, 2, 0);
    lv_obj_clear_flag(band, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *t = stencil(band, title, color);
    lv_obj_center(t);
    return t;
}

static void show(row_t *r, const struct tm *t)
{
    char buf[24];
    if (!t) {
        seg7_set_text(r->day, "--");
        seg7_set_text(r->month, "--");
        seg7_set_text(r->time, "----");
        seg7_set_colon(r->time, false);
        return;
    }
    snprintf(buf, sizeof buf, "%02d", t->tm_mday);
    seg7_set_text(r->day, buf);
    snprintf(buf, sizeof buf, "%02d", t->tm_mon + 1);
    seg7_set_text(r->month, buf);
    snprintf(buf, sizeof buf, "%02d%02d", ui_display_hour(t->tm_hour), t->tm_min);
    seg7_set_text(r->time, buf);
}

/* ------------------------------------------------------------ easter egg */

#define BOLTS 6      /* 3 main bolts + 1 branch each */
#define BOLT_PTS 15
static lv_point_t s_bolt[BOLTS][BOLT_PTS];
static int s_bolt_len[BOLTS];
static int32_t s_flash; /* 0..1000 */

static void bolts_draw(lv_event_t *e)
{
    if (s_flash <= 0 || s_flash >= 1000) return;
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    /* Three successive flashes (even phases), black in between (odd phases). */
    int phase = s_flash * 6 / 1000;
    if (phase % 2) return;
    int decay = 255 - (s_flash % 167) * 255 / 167; /* each flash decays */

    /* Brief bluish veil over the whole screen. */
    lv_draw_rect_dsc_t veil;
    lv_draw_rect_dsc_init(&veil);
    veil.bg_color = lv_color_hex(0x9CC4FF);
    veil.bg_opa = (lv_opa_t)(decay * 45 / 255);
    lv_area_t all = {0, 0, KG_W - 1, KG_H - 1};
    lv_draw_rect(ctx, &veil, &all);

    /* Opaque tinted glow (no alpha build-up at the joints), then a white core. */
    lv_draw_line_dsc_t glow, core;
    lv_draw_line_dsc_init(&glow);
    lv_draw_line_dsc_init(&core);
    glow.color = lv_color_mix(lv_color_hex(0x5B8CFF), KG_C_BG, (uint8_t)(90 + decay * 120 / 255));
    glow.width = 6;
    glow.round_start = glow.round_end = 1;
    core.color = lv_color_mix(lv_color_white(), lv_color_hex(0x5B8CFF), (uint8_t)decay);
    core.width = 2;
    core.round_start = core.round_end = 1;
    for (int pass = 0; pass < 2; pass++)
        for (int b = 0; b < BOLTS; b++)
            for (int i = 0; i + 1 < s_bolt_len[b]; i++)
                lv_draw_line(ctx, pass ? &core : &glow, &s_bolt[b][i], &s_bolt[b][i + 1]);
}

static void flash_cb(void *var, int32_t v)
{
    (void)var;
    s_flash = v;
    lv_obj_invalidate(s_bolts);
}

static lv_coord_t clamp_x(lv_coord_t x) { return x < 4 ? 4 : x > KG_W - 4 ? KG_W - 4 : x; }

static void discharge(void)
{
    for (int b = 0; b < BOLTS / 2; b++) {
        /* Main bolt, top to bottom, jagged path. */
        lv_coord_t x = 30 + rand() % 180;
        s_bolt_len[b] = BOLT_PTS;
        for (int i = 0; i < BOLT_PTS; i++) {
            s_bolt[b][i] = (lv_point_t){x, i * KG_H / (BOLT_PTS - 1)};
            x = clamp_x(x + (rand() % 29) - 14);
        }
        /* Branch: starts in the middle third and runs off diagonally. */
        int r = BOLTS / 2 + b, from = 4 + rand() % 5, dir = rand() % 2 ? 1 : -1;
        lv_point_t p = s_bolt[b][from];
        s_bolt_len[r] = 6;
        for (int i = 0; i < 6; i++) {
            s_bolt[r][i] = p;
            p.x = clamp_x(p.x + dir * (6 + rand() % 10));
            p.y += 8 + rand() % 8;
        }
    }
    hal_vibrate(40);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_bolts);
    lv_anim_set_values(&a, 1, 999);
    lv_anim_set_time(&a, 700);
    lv_anim_set_exec_cb(&a, flash_cb);
    lv_anim_start(&a);
}

static void long_press_cb(lv_event_t *e)
{
    (void)e;
    discharge();
}

/* ------------------------------------------------------------------ clock */

static void update(lv_timer_t *t)
{
    (void)t;
    struct tm now;
    hal_now(&now, NULL);

    if (now.tm_sec != s_last_sec) {
        s_last_sec = now.tm_sec;
        seg7_set_colon(s_now.time, now.tm_sec % 2 == 0);
    }
    if (now.tm_min == s_last_min) return;
    s_last_min = now.tm_min;

    show(&s_now, &now);

    struct tm alarm;
    if (model_next_alarm(&alarm)) {
        show(&s_dest, &alarm);
        seg7_set_colon(s_dest.time, true);
        lv_label_set_text(s_dest_title, "DESTINATION TIME");
    } else {
        show(&s_dest, NULL);
        lv_label_set_text(s_dest_title, "DESTINATION TIME  -  NO ALARM");
    }

    time_t sync = hal_last_sync();
    if (sync) {
        struct tm st;
        localtime_r(&sync, &st);
        show(&s_last, &st);
        seg7_set_colon(s_last.time, true);
    } else {
        show(&s_last, NULL);
    }

    /* Step goal reached: one discharge per day. */
    if (now.tm_hour == 0 && now.tm_min == 0) s_egg_done_today = false;
    if (!s_egg_done_today && hal_steps_today() >= STEPS_EGG) {
        s_egg_done_today = true;
        discharge();
    }
}

static void set_active(bool on)
{
    if (on) {
        s_last_min = s_last_sec = -1; /* redraw everything on return */
        lv_timer_resume(s_timer);
        update(NULL);
    } else {
        lv_timer_pause(s_timer);
    }
}

static lv_obj_t *create(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, KG_C_BG, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    s_dest_title = row(s_scr, 3, KG_C_SEG_RED, "DESTINATION TIME", &s_dest);
    row(s_scr, 3 + ROW_H, KG_C_SEG_GREEN, "PRESENT TIME", &s_now);
    row(s_scr, 3 + 2 * ROW_H, KG_C_SEG_AMBER, "LAST TIME DEPARTED", &s_last);

    /* Hidden long-press area: the green row. */
    lv_obj_t *hot = lv_obj_create(s_scr);
    lv_obj_remove_style_all(hot);
    lv_obj_set_pos(hot, 0, 3 + ROW_H);
    lv_obj_set_size(hot, KG_W, ROW_H);
    lv_obj_add_event_cb(hot, long_press_cb, LV_EVENT_LONG_PRESSED, NULL);

    s_bolts = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_bolts);
    lv_obj_set_size(s_bolts, KG_W, KG_H);
    lv_obj_clear_flag(s_bolts, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_bolts, bolts_draw, LV_EVENT_DRAW_POST, NULL);

    s_timer = lv_timer_create(update, 100, NULL);
    update(NULL);
    return s_scr;
}

const kg_face_t kg_face_circuits = {
    .name = "circuits",
    .create = create,
    .set_active = set_active,
    .brightness_pct = 100,
};

/* Console / test hook: trigger the discharge. */
void face_circuits_discharge(void) { discharge(); }
