/*
 * App Minuteur. L'échéance est en heure murale : le minuteur continue écran éteint, et
 * la gestion d'énergie réveille la montre à l'échéance (hal_schedule_wakeup).
 */
#include "nav.h"
#include "kit.h"
#include "model.h"
#include "theme.h"
#include "fonts/kg_fonts.h"

static lv_obj_t *s_arc, *s_time, *s_left, *s_main, *s_right;
static lv_timer_t *s_timer;

static void refresh(void)
{
    kg_timer_state_t st = model_timer_state();
    uint32_t left = model_timer_remaining_ms(), dur = model_timer_duration();
    uint32_t s = (left + 999) / 1000; /* arrondi supérieur : jamais « 00:00 » avant la fin */
    lv_label_set_text_fmt(s_time, "%02lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
    lv_arc_set_value(s_arc, dur ? (int16_t)(1000 - (uint64_t)left * 1000 / dur) : 0);

    bool idle = st == KG_TIMER_IDLE || st == KG_TIMER_DONE;
    kit_button_set_text(s_main, st == KG_TIMER_RUNNING ? "Pause" : st == KG_TIMER_PAUSED ? "Reprise" : "Départ");
    kit_button_set_text(s_left, idle ? "-1" : "RàZ");
    if (idle) lv_obj_clear_state(s_right, LV_STATE_DISABLED);
    else lv_obj_add_state(s_right, LV_STATE_DISABLED);
}

static void tick(lv_timer_t *t)
{
    (void)t;
    refresh();
}

static void adjust(int delta_min)
{
    int32_t m = (int32_t)(model_timer_duration() / 60000) + delta_min;
    if (m < 1) m = 1;
    if (m > 99) m = 99;
    model_timer_set((uint32_t)m * 60000);
}

static void left_cb(lv_event_t *e)
{
    (void)e;
    kg_timer_state_t st = model_timer_state();
    if (st == KG_TIMER_IDLE || st == KG_TIMER_DONE) adjust(-1);
    else model_timer_reset();
    refresh();
}

static void right_cb(lv_event_t *e)
{
    (void)e;
    adjust(+1);
    refresh();
}

static void main_cb(lv_event_t *e)
{
    (void)e;
    if (model_timer_state() == KG_TIMER_DONE) model_timer_reset();
    model_timer_start_pause();
    refresh();
}

static lv_obj_t *create(void)
{
    lv_obj_t *scr = kit_screen();
    kit_title(scr, "MINUTEUR");

    s_arc = lv_arc_create(scr);
    lv_obj_set_size(s_arc, 150, 150);
    lv_obj_align(s_arc, LV_ALIGN_TOP_MID, 0, 22);
    lv_arc_set_rotation(s_arc, 270);
    lv_arc_set_bg_angles(s_arc, 0, 360);
    lv_arc_set_range(s_arc, 0, 1000);
    lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(s_arc, KG_C_BTN, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, KG_C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_arc, 6, LV_PART_INDICATOR);

    s_time = kit_label(scr, &kg_font_digits_56, KG_C_TEXT, "05:00");
    lv_obj_set_width(s_time, 130); /* chiffres proportionnels : largeur fixe, texte centré */
    lv_obj_set_style_text_align(s_time, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(s_time, s_arc, LV_ALIGN_CENTER, 0, 0);

    s_left = kit_button(scr, "-1", false, 58, 44, left_cb, NULL);
    lv_obj_align(s_left, LV_ALIGN_BOTTOM_LEFT, 10, -12);
    s_main = kit_button(scr, "Départ", true, 92, 44, main_cb, NULL);
    lv_obj_align(s_main, LV_ALIGN_BOTTOM_MID, 0, -12);
    s_right = kit_button(scr, "+1", false, 58, 44, right_cb, NULL);
    lv_obj_align(s_right, LV_ALIGN_BOTTOM_RIGHT, -10, -12);
    lv_obj_set_style_text_opa(s_right, LV_OPA_30, LV_STATE_DISABLED);

    s_timer = lv_timer_create(tick, 200, NULL);
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

const kg_app_t kg_app_timer = {
    .name = "timer",
    .label = "Minuteur",
    .icon = KG_ICON_TIMER,
    .create = create,
    .on_show = on_show,
    .on_hide = on_hide,
};
