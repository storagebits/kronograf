/*
 * Sun and Moon at home: the Sun on its day arc between sunrise and sunset, civil twilight,
 * day length and its change since yesterday; the Moon's phase drawn to scale, its
 * illumination and the next full moon. Computed offline (kg_sky).
 */
#include "nav.h"
#include "faces.h"
#include "kit.h"
#include "theme.h"
#include "fonts/kg_fonts.h"
#include "../data/kg_sky.h"
#include "../data/kg_tz.h"
#include "../data/kg_weather.h"
#include "../hal/hal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define ARC_CX 120
#define ARC_CY 104
#define ARC_R 84
#define MOON_X 46
#define MOON_Y 196
#define MOON_R 22

static lv_obj_t *s_arc, *s_sun, *s_rise, *s_set, *s_dawn, *s_dusk, *s_len, *s_delta, *s_moon, *s_mname, *s_millum, *s_mnext;
static lv_timer_t *s_timer;
static kg_moon_t s_m;

static void fmt_time(char *b, size_t n, time_t t)
{
    struct tm lt;
    localtime_r(&t, &lt);
    if (ui_fmt24()) snprintf(b, n, "%02d:%02d", lt.tm_hour, lt.tm_min);
    else snprintf(b, n, "%d:%02d %s", (lt.tm_hour + 11) % 12 + 1, lt.tm_min, lt.tm_hour < 12 ? "AM" : "PM");
}

/* Moon disk: dark disk, lit part bounded by the terminator, row by row (northern view). */
static void moon_draw(lv_event_t *e)
{
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target(e), &a);
    lv_coord_t cx = (a.x1 + a.x2) / 2, cy = (a.y1 + a.y2) / 2;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(0x1E2126);
    d.radius = LV_RADIUS_CIRCLE;
    lv_area_t disk = {cx - MOON_R, cy - MOON_R, cx + MOON_R, cy + MOON_R};
    lv_draw_rect(ctx, &d, &disk);
    d.radius = 0;
    d.bg_color = lv_color_hex(0xE9E3CF);
    float c = cosf(2 * (float)M_PI * (float)s_m.phase);
    for (int y = -MOON_R; y <= MOON_R; y++) {
        float w = sqrtf((float)(MOON_R * MOON_R - y * y));
        float x0 = s_m.phase < 0.5 ? w * c : -w, x1 = s_m.phase < 0.5 ? w : -w * c;
        if (x1 - x0 < 0.5f) continue;
        lv_area_t r = {cx + (lv_coord_t)lroundf(x0), cy + y, cx + (lv_coord_t)lroundf(x1), cy + y};
        lv_draw_rect(ctx, &d, &r);
    }
}

static void refresh(void)
{
    struct tm now;
    hal_now(&now, NULL);
    time_t t = mktime(&now);
    kg_sun_t sun, prev;
    kg_sun_day(now.tm_year + 1900, now.tm_mon + 1, now.tm_mday, KG_HOME_LAT, KG_HOME_LON, &sun);
    int64_t yd = kg_days_from_civil(now.tm_year + 1900, now.tm_mon + 1, now.tm_mday) - 1;
    int py, pm, pd;
    kg_civil_from_days(yd, &py, &pm, &pd);
    kg_sun_day(py, pm, pd, KG_HOME_LAT, KG_HOME_LON, &prev);

    char b[40];
    if (sun.rise) {
        fmt_time(b, sizeof b, sun.rise);
        lv_label_set_text(s_rise, b);
        fmt_time(b, sizeof b, sun.set);
        lv_label_set_text(s_set, b);
        long len = (long)(sun.set - sun.rise), dl = len - (long)(prev.set - prev.rise);
        lv_label_set_text_fmt(s_len, "%ld h %02ld", len / 3600, len / 60 % 60);
        snprintf(b, sizeof b, "%s%ld min %02ld s", dl < 0 ? "−" : "+", labs(dl) / 60, labs(dl) % 60);
        lv_label_set_text(s_delta, b);
    } else {
        lv_label_set_text(s_rise, "—");
        lv_label_set_text(s_set, "—");
        lv_label_set_text(s_len, sun.polar_day ? "24 h" : "0 h");
        lv_label_set_text(s_delta, sun.polar_day ? "Midnight sun" : "Polar night");
    }
    if (sun.dawn) {
        fmt_time(b, sizeof b, sun.dawn);
        lv_label_set_text_fmt(s_dawn, "Dawn %s", b);
        fmt_time(b, sizeof b, sun.dusk);
        lv_label_set_text_fmt(s_dusk, "Dusk %s", b);
    }

    /* Sun on the arc: 180° at sunrise, 360° at sunset; below the horizon, hidden. */
    bool up = sun.rise && t >= sun.rise && t <= sun.set;
    int pm1000 = up ? (int)((t - sun.rise) * 1000 / (sun.set - sun.rise)) : (sun.rise && t > sun.set ? 1000 : 0);
    lv_arc_set_value(s_arc, (int16_t)pm1000);
    float ang = (float)M_PI * (1 + pm1000 / 1000.0f);
    lv_obj_set_pos(s_sun, ARC_CX + (lv_coord_t)lroundf(ARC_R * cosf(ang)) - 9, ARC_CY + (lv_coord_t)lroundf(ARC_R * sinf(ang)) - 9);
    if (up) lv_obj_clear_flag(s_sun, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_sun, LV_OBJ_FLAG_HIDDEN);

    kg_moon(t, &s_m);
    lv_label_set_text(s_mname, s_m.name);
    snprintf(b, sizeof b, "%.0f %% lit · day %.0f of 29", s_m.illum * 100, floor(s_m.age) + 1);
    lv_label_set_text(s_millum, b);
    static const char *const MON[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    struct tm f;
    time_t next = s_m.phase > 0.47 && s_m.phase < 0.53 ? s_m.next_new : s_m.next_full;
    localtime_r(&next, &f);
    lv_label_set_text_fmt(s_mnext, "%s %s %d", next == s_m.next_new ? "New moon" : "Full moon", MON[f.tm_mon], f.tm_mday);
    lv_obj_invalidate(s_moon);
}

static void tick(lv_timer_t *t)
{
    (void)t;
    refresh();
}

static lv_obj_t *small(lv_obj_t *scr, const char *text, lv_coord_t x, lv_coord_t y, bool right)
{
    lv_obj_t *l = kit_label(scr, &kg_font_stencil_11, KG_C_TEXT_DIM, text);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    if (right) {
        lv_obj_set_width(l, 100);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
    }
    lv_obj_set_pos(l, x, y);
    return l;
}

static lv_obj_t *value(lv_obj_t *scr, const lv_font_t *font, lv_color_t c, lv_coord_t x, lv_coord_t y, lv_coord_t w,
                       lv_text_align_t align)
{
    lv_obj_t *l = kit_label(scr, font, c, "");
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_align(l, align, 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

static lv_obj_t *create(void)
{
    lv_obj_t *scr = kit_screen();
    kit_title(scr, "SUN & MOON");

    s_arc = lv_arc_create(scr);
    lv_obj_set_size(s_arc, 2 * ARC_R, 2 * ARC_R);
    lv_obj_set_pos(s_arc, ARC_CX - ARC_R, ARC_CY - ARC_R);
    lv_arc_set_bg_angles(s_arc, 180, 360);
    lv_arc_set_rotation(s_arc, 0);
    lv_arc_set_angles(s_arc, 180, 180);
    lv_arc_set_mode(s_arc, LV_ARC_MODE_NORMAL);
    lv_arc_set_range(s_arc, 0, 1000);
    lv_arc_set_bg_angles(s_arc, 180, 360);
    lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(s_arc, KG_C_HAIRLINE, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, 2, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(0x8A5A1E), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_arc, 2, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_arc, false, LV_PART_INDICATOR);

    lv_obj_t *h = lv_obj_create(scr);
    lv_obj_remove_style_all(h);
    lv_obj_set_pos(h, 14, ARC_CY);
    lv_obj_set_size(h, KG_W - 28, 1);
    lv_obj_set_style_bg_color(h, KG_C_TRACK, 0);
    lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0);

    s_sun = lv_obj_create(scr);
    lv_obj_remove_style_all(s_sun);
    lv_obj_set_size(s_sun, 18, 18);
    lv_obj_set_style_radius(s_sun, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_sun, KG_C_WARN, 0);
    lv_obj_set_style_bg_opa(s_sun, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_color(s_sun, KG_C_WARN, 0);
    lv_obj_set_style_shadow_width(s_sun, 14, 0);
    lv_obj_set_style_shadow_opa(s_sun, LV_OPA_50, 0);

    s_len = value(scr, &kg_font_label_20, KG_C_TEXT, 60, 62, 120, LV_TEXT_ALIGN_CENTER);
    s_delta = value(scr, &kg_font_text_16, KG_C_TEXT_DIM, 50, 82, 140, LV_TEXT_ALIGN_CENTER);

    small(scr, "SUNRISE", 12, ARC_CY + 5, false);
    small(scr, "SUNSET", KG_W - 112, ARC_CY + 5, true);
    s_rise = value(scr, &kg_font_label_20, KG_C_TEXT, 12, ARC_CY + 17, 100, LV_TEXT_ALIGN_LEFT);
    s_set = value(scr, &kg_font_label_20, KG_C_TEXT, KG_W - 112, ARC_CY + 17, 100, LV_TEXT_ALIGN_RIGHT);
    s_dawn = value(scr, &kg_font_text_16, KG_C_TEXT_DIM, 12, ARC_CY + 38, 100, LV_TEXT_ALIGN_LEFT);
    s_dusk = value(scr, &kg_font_text_16, KG_C_TEXT_DIM, KG_W - 112, ARC_CY + 38, 100, LV_TEXT_ALIGN_RIGHT);

    lv_obj_t *sep = lv_obj_create(scr);
    lv_obj_remove_style_all(sep);
    lv_obj_set_pos(sep, 14, 166);
    lv_obj_set_size(sep, KG_W - 28, 1);
    lv_obj_set_style_bg_color(sep, KG_C_HAIRLINE, 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);

    s_moon = lv_obj_create(scr);
    lv_obj_remove_style_all(s_moon);
    lv_obj_set_size(s_moon, 2 * MOON_R + 2, 2 * MOON_R + 2);
    lv_obj_set_pos(s_moon, MOON_X - MOON_R - 1, MOON_Y - MOON_R - 1);
    lv_obj_add_event_cb(s_moon, moon_draw, LV_EVENT_DRAW_MAIN, NULL);
    s_mname = value(scr, &kg_font_label_20, KG_C_TEXT, 80, 172, 150, LV_TEXT_ALIGN_LEFT);
    s_millum = value(scr, &kg_font_text_16, KG_C_TEXT_DIM, 80, 194, 150, LV_TEXT_ALIGN_LEFT);
    s_mnext = value(scr, &kg_font_text_16, KG_C_TEXT_DIM, 80, 212, 150, LV_TEXT_ALIGN_LEFT);

    s_timer = lv_timer_create(tick, 30000, NULL);
    lv_timer_pause(s_timer);
    refresh();
    return scr;
}

static void on_show(void)
{
    refresh();
    lv_timer_resume(s_timer);
}

static void on_hide(void) { lv_timer_pause(s_timer); }

const kg_app_t kg_app_sky = {
    .name = "sky",
    .label = "Sky",
    .icon = KG_ICON_SKY,
    .create = create,
    .on_show = on_show,
    .on_hide = on_hide,
};
