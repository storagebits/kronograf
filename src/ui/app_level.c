/*
 * Spirit level from the BMA423: lay the watch on a surface, the bubble rises towards the
 * high side. Rings every 2°, green when level within 0.5°. Tap to zero it on the current
 * surface (stored), long press to clear the calibration.
 */
#include "nav.h"
#include "kit.h"
#include "theme.h"
#include "fonts/kg_fonts.h"
#include "kg_tick.h"
#include "../hal/hal.h"
#include <math.h>
#include <stdio.h>

#define CX 120
#define CY 112
#define R_OUT 92
#define PX_PER_DEG 9.0f  /* bubble travel: 10° reaches the outer ring */
#define BUBBLE 26
#define AWAKE_MAX_MS (5 * 60 * 1000) /* hands-off reading, but not forever */

static lv_obj_t *s_bubble, *s_x, *s_y, *s_total, *s_hint;
static lv_timer_t *s_timer;
static float s_fx, s_fy, s_fz = 1, s_off_x, s_off_y;
static bool s_level, s_have;
static uint32_t s_opened;

static lv_obj_t *ring(lv_obj_t *scr, lv_coord_t r, lv_color_t c)
{
    lv_obj_t *o = lv_obj_create(scr);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, 2 * r, 2 * r);
    lv_obj_set_pos(o, CX - r, CY - r);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(o, c, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static void bar(lv_obj_t *scr, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *o = lv_obj_create(scr);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, KG_C_HAIRLINE, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
}

static void angles(float *ax, float *ay)
{
    /* Tilt around each axis, in degrees, relative to the calibrated surface. */
    *ax = atan2f(s_fx, s_fz) * 57.29578f - s_off_x;
    *ay = atan2f(s_fy, s_fz) * 57.29578f - s_off_y;
}

static void tick(lv_timer_t *t)
{
    (void)t;
    if (kg_tick_ms() - s_opened < AWAKE_MAX_MS) lv_disp_trig_activity(NULL); /* a level is read hands-off */
    float x, y, z;
    if (!hal_accel(&x, &y, &z)) {
        lv_label_set_text(s_hint, "NO ACCELEROMETER");
        return;
    }
    const float k = s_have ? 0.25f : 1.0f; /* low-pass: steady bubble */
    s_fx += k * (x - s_fx), s_fy += k * (y - s_fy), s_fz += k * (z - s_fz);
    s_have = true;

    float ax, ay;
    angles(&ax, &ay);
    float total = sqrtf(ax * ax + ay * ay);
    float px = ax * PX_PER_DEG, py = -ay * PX_PER_DEG, d = sqrtf(px * px + py * py), lim = R_OUT - BUBBLE / 2 - 2;
    if (d > lim) px *= lim / d, py *= lim / d;
    lv_obj_set_pos(s_bubble, CX - BUBBLE / 2 + (lv_coord_t)lroundf(px), CY - BUBBLE / 2 + (lv_coord_t)lroundf(py));

    bool level = total < 0.5f;
    if (level && !s_level) hal_vibrate(30);
    s_level = level;
    lv_obj_set_style_bg_color(s_bubble, level ? KG_C_OK : KG_C_ACCENT, 0);
    char b[24];
    snprintf(b, sizeof b, "X %+.1f°", (double)ax);
    lv_label_set_text(s_x, b);
    snprintf(b, sizeof b, "Y %+.1f°", (double)ay);
    lv_label_set_text(s_y, b);
    snprintf(b, sizeof b, "%.1f°", (double)total);
    lv_label_set_text(s_total, b);
    lv_obj_set_style_text_color(s_total, level ? KG_C_OK : KG_C_TEXT, 0);
}

static void zero_cb(lv_event_t *e)
{
    bool reset = lv_event_get_code(e) == LV_EVENT_LONG_PRESSED;
    s_off_x = s_off_y = 0;
    if (!reset) {
        float ax, ay;
        angles(&ax, &ay);
        s_off_x = ax, s_off_y = ay;
    }
    hal_setting_set("lvl_x", (int32_t)lroundf(s_off_x * 100));
    hal_setting_set("lvl_y", (int32_t)lroundf(s_off_y * 100));
    lv_label_set_text(s_hint, reset ? "CALIBRATION CLEARED" : "ZEROED");
    hal_vibrate(reset ? 60 : 20);
}

static lv_obj_t *create(void)
{
    lv_obj_t *scr = kit_screen();
    kit_title(scr, "LEVEL");
    lv_obj_t *dial = ring(scr, R_OUT, KG_C_TRACK);
    lv_obj_set_style_bg_color(dial, KG_C_DIAL, 0);
    lv_obj_set_style_bg_opa(dial, LV_OPA_COVER, 0);
    for (int deg = 2; deg <= 8; deg += 2) ring(scr, (lv_coord_t)(deg * PX_PER_DEG + BUBBLE / 2), KG_C_HAIRLINE);
    bar(scr, CX - R_OUT + 4, CY, 2 * R_OUT - 8, 1);
    bar(scr, CX, CY - R_OUT + 4, 1, 2 * R_OUT - 8);
    lv_obj_t *target = ring(scr, BUBBLE / 2 + 3, KG_C_HAND);
    lv_obj_set_style_border_width(target, 2, 0);

    s_bubble = lv_obj_create(scr);
    lv_obj_remove_style_all(s_bubble);
    lv_obj_set_size(s_bubble, BUBBLE, BUBBLE);
    lv_obj_set_style_radius(s_bubble, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_bubble, LV_OPA_80, 0);
    lv_obj_set_style_bg_color(s_bubble, KG_C_ACCENT, 0);
    lv_obj_set_style_shadow_color(s_bubble, KG_C_ACCENT, 0);
    lv_obj_set_style_shadow_width(s_bubble, 8, 0);
    lv_obj_set_style_shadow_opa(s_bubble, LV_OPA_40, 0);
    lv_obj_clear_flag(s_bubble, LV_OBJ_FLAG_CLICKABLE);

    s_x = kit_label(scr, &kg_font_text_16, KG_C_TEXT_DIM, "");
    lv_obj_set_pos(s_x, 8, 206);
    s_y = kit_label(scr, &kg_font_text_16, KG_C_TEXT_DIM, "");
    lv_obj_set_width(s_y, 70);
    lv_obj_set_style_text_align(s_y, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(s_y, KG_W - 78, 206);
    s_total = kit_label(scr, &kg_font_title_34, KG_C_TEXT, "");
    lv_obj_set_width(s_total, 100);
    lv_obj_set_style_text_align(s_total, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_total, CX - 50, 200);
    s_hint = kit_label(scr, &kg_font_stencil_11, KG_C_TEXT_DIM, "TAP TO ZERO");
    lv_obj_set_style_text_letter_space(s_hint, 1, 0);
    lv_obj_set_width(s_hint, 140);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_hint, CX - 70, CY + R_OUT - 24);

    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, zero_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(scr, zero_cb, LV_EVENT_LONG_PRESSED, NULL);
    s_timer = lv_timer_create(tick, 50, NULL);
    lv_timer_pause(s_timer);
    return scr;
}

static void on_show(void)
{
    s_off_x = hal_setting_get("lvl_x", 0) / 100.0f;
    s_off_y = hal_setting_get("lvl_y", 0) / 100.0f;
    s_have = s_level = false;
    s_opened = kg_tick_ms();
    lv_label_set_text(s_hint, "TAP TO ZERO");
    tick(NULL);
    lv_timer_resume(s_timer);
}

static void on_hide(void) { lv_timer_pause(s_timer); }

const kg_app_t kg_app_level = {
    .name = "level",
    .label = "Level",
    .icon = KG_ICON_LEVEL,
    .create = create,
    .on_show = on_show,
    .on_hide = on_hide,
};
