/*
 * Face 3 — "Night": the time alone, in large dark-red digits, dimmed backlight.
 * One update per minute: the most power-frugal face. Rationale in DECISIONS.md (D19).
 * The battery level sits under the time, dark red too (orange below 20 %).
 */
#include "faces.h"
#include "theme.h"
#include "fonts/kg_fonts.h"
#include "../hal/hal.h"

static lv_obj_t *s_time, *s_batt;
static lv_timer_t *s_timer;
static int s_last_min = -1;

static void update(lv_timer_t *t)
{
    (void)t;
    struct tm now;
    hal_now(&now, NULL);
    if (now.tm_min == s_last_min) return;
    s_last_min = now.tm_min;
    if (ui_fmt24()) lv_label_set_text_fmt(s_time, "%02d:%02d", now.tm_hour, now.tm_min);
    else lv_label_set_text_fmt(s_time, "%d:%02d", ui_display_hour(now.tm_hour), now.tm_min);
    kg_battery_t b = hal_battery();
    if (b.percent >= 0) {
        lv_label_set_text_fmt(s_batt, "%s%d %%", b.charging ? "+ " : "", b.percent);
        lv_obj_set_style_text_color(s_batt, b.percent < 20 && !b.charging ? KG_C_WARN : KG_C_NIGHT, 0);
    } else {
        lv_label_set_text(s_batt, "");
    }
}

static void set_active(bool on)
{
    if (on) {
        s_last_min = -1;
        lv_timer_resume(s_timer);
        update(NULL);
    } else {
        lv_timer_pause(s_timer);
    }
}

static lv_obj_t *create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, KG_C_BG, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    s_time = lv_label_create(scr);
    lv_obj_set_style_text_font(s_time, &kg_font_night_104, 0);
    lv_obj_set_style_text_color(s_time, KG_C_NIGHT, 0);
    lv_obj_set_style_text_letter_space(s_time, -2, 0);
    lv_label_set_text(s_time, "00:00");
    lv_obj_center(s_time);
    s_batt = lv_label_create(scr);
    lv_obj_set_style_text_font(s_batt, &kg_font_label_20, 0);
    lv_obj_set_style_text_color(s_batt, KG_C_NIGHT, 0);
    lv_label_set_text(s_batt, "");
    lv_obj_align(s_batt, LV_ALIGN_CENTER, 0, 72);

    s_timer = lv_timer_create(update, 1000, NULL);
    update(NULL);
    return scr;
}

const kg_face_t kg_face_night = {
    .name = "night",
    .create = create,
    .set_active = set_active,
    .brightness_pct = KG_NIGHT_BRIGHTNESS_PCT,
};
