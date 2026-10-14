/*
 * Steps app: today's count in a goal ring (10,000), and a 7-day bar chart
 * (6 days of NVS history + today in orange).
 */
#include "nav.h"
#include "kit.h"
#include "theme.h"
#include "fonts/kg_fonts.h"
#include "../hal/hal.h"
#include <stdio.h>

#define BAR_W 20
#define BAR_GAP 9
#define BAR_MAX_H 62
#define BARS_Y 214 /* bar baseline */

static lv_obj_t *s_arc, *s_count, *s_bars[7], *s_days[7], *s_goal;
static lv_timer_t *s_timer;

static const char *const INITIALS[] = {"S", "M", "T", "W", "T", "F", "S"};

static void refresh(void)
{
    uint32_t today = hal_steps_today(), hist[7], vals[7], top = KG_STEPS_GOAL;
    hal_steps_history(hist);
    for (int i = 0; i < 6; i++) vals[i] = hist[5 - i]; /* 6 days ago … yesterday */
    vals[6] = today;
    for (int i = 0; i < 7; i++)
        if (vals[i] > top) top = vals[i];

    char buf[16];
    if (today >= 1000) snprintf(buf, sizeof buf, "%lu,%03lu", (unsigned long)(today / 1000), (unsigned long)(today % 1000));
    else snprintf(buf, sizeof buf, "%lu", (unsigned long)today);
    lv_label_set_text(s_count, buf);
    lv_arc_set_value(s_arc, (int16_t)(today > KG_STEPS_GOAL ? 1000 : today * 1000 / KG_STEPS_GOAL));
    lv_label_set_text(s_goal, today >= KG_STEPS_GOAL ? "GOAL REACHED" : "GOAL 10,000");
    lv_obj_set_style_text_color(s_goal, today >= KG_STEPS_GOAL ? KG_C_OK : KG_C_TEXT_DIM, 0);

    struct tm now;
    hal_now(&now, NULL);
    for (int i = 0; i < 7; i++) {
        lv_coord_t h = (lv_coord_t)((uint64_t)vals[i] * BAR_MAX_H / top);
        if (h < 2) h = 2;
        lv_obj_set_height(s_bars[i], h);
        lv_obj_set_y(s_bars[i], BARS_Y - h);
        lv_label_set_text(s_days[i], INITIALS[(now.tm_wday + i + 1) % 7]);
    }
}

static void tick(lv_timer_t *t)
{
    (void)t;
    refresh();
}

static lv_obj_t *create(void)
{
    lv_obj_t *scr = kit_screen();
    kit_title(scr, "STEPS");

    s_arc = lv_arc_create(scr);
    lv_obj_set_size(s_arc, 116, 116);
    lv_obj_align(s_arc, LV_ALIGN_TOP_MID, 0, 20);
    lv_arc_set_rotation(s_arc, 135);
    lv_arc_set_bg_angles(s_arc, 0, 270);
    lv_arc_set_range(s_arc, 0, 1000);
    lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(s_arc, KG_C_BTN, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, 7, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, KG_C_OK, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_arc, 7, LV_PART_INDICATOR);

    s_count = kit_label(scr, &kg_font_digits_30, KG_C_TEXT, "0");
    lv_obj_set_width(s_count, 110);
    lv_obj_set_style_text_align(s_count, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(s_count, s_arc, LV_ALIGN_CENTER, 0, -4);
    s_goal = kit_label(scr, &kg_font_stencil_11, KG_C_TEXT_DIM, "");
    lv_obj_set_width(s_goal, 140); /* fixed width: the text changes, the centring holds */
    lv_obj_set_style_text_align(s_goal, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(s_goal, s_arc, LV_ALIGN_BOTTOM_MID, 0, 2);

    lv_coord_t x0 = (KG_W - (7 * BAR_W + 6 * BAR_GAP)) / 2;
    for (int i = 0; i < 7; i++) {
        s_bars[i] = lv_obj_create(scr);
        lv_obj_remove_style_all(s_bars[i]);
        lv_obj_set_width(s_bars[i], BAR_W);
        lv_obj_set_x(s_bars[i], x0 + i * (BAR_W + BAR_GAP));
        lv_obj_set_style_radius(s_bars[i], 3, 0);
        lv_obj_set_style_bg_opa(s_bars[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(s_bars[i], i == 6 ? KG_C_ACCENT : KG_C_TRACK, 0);
        s_days[i] = kit_label(scr, &kg_font_stencil_11, i == 6 ? KG_C_ACCENT : KG_C_TEXT_DIM, "");
        lv_obj_set_width(s_days[i], BAR_W);
        lv_obj_set_style_text_align(s_days[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(s_days[i], x0 + i * (BAR_W + BAR_GAP), BARS_Y + 4);
    }

    s_timer = lv_timer_create(tick, 2000, NULL);
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

const kg_app_t kg_app_steps = {
    .name = "steps",
    .label = "Steps",
    .icon = KG_ICON_STEPS,
    .create = create,
    .on_show = on_show,
    .on_hide = on_hide,
};
