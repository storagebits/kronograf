/*
 * Torch: the whole screen lit at full backlight. Tap to switch between white and red
 * (keeps night vision). The screen stays on for up to 5 minutes, then the usual timeout
 * applies again.
 */
#include "nav.h"
#include "kit.h"
#include "theme.h"
#include "fonts/kg_fonts.h"
#include "kg_tick.h"
#include "../hal/hal.h"

#define TORCH_MAX_MS (5 * 60 * 1000)

static lv_obj_t *s_scr, *s_hint;
static lv_timer_t *s_timer;
static bool s_red;
static uint32_t s_opened;

static void apply(void)
{
    lv_obj_set_style_bg_color(s_scr, s_red ? lv_color_hex(0xFF1800) : lv_color_white(), 0);
    lv_obj_set_style_text_color(s_hint, s_red ? lv_color_hex(0x5A0800) : lv_color_hex(0xB8B8B8), 0);
}

static void tap_cb(lv_event_t *e)
{
    (void)e;
    s_red = !s_red;
    kit_tick();
    apply();
}

static void tick(lv_timer_t *t)
{
    (void)t;
    if (kg_tick_ms() - s_opened < TORCH_MAX_MS) lv_disp_trig_activity(NULL);
}

static lv_obj_t *create(void)
{
    s_scr = kit_screen();
    s_hint = kit_label(s_scr, &kg_font_text_16, KG_C_TEXT_DIM, "Tap: white / red");
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_add_flag(s_scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_scr, tap_cb, LV_EVENT_CLICKED, NULL);
    s_timer = lv_timer_create(tick, 1000, NULL);
    lv_timer_pause(s_timer);
    apply();
    return s_scr;
}

static void on_show(void)
{
    s_opened = kg_tick_ms();
    hal_brightness_boost(true);
    lv_timer_resume(s_timer);
}

static void on_hide(void)
{
    hal_brightness_boost(false);
    lv_timer_pause(s_timer);
}

const kg_app_t kg_app_torch = {
    .name = "torch",
    .label = "Torch",
    .icon = KG_ICON_TORCH,
    .create = create,
    .on_show = on_show,
    .on_hide = on_hide,
};
