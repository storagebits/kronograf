/*
 * Stopwatch app. The model runs in the background (timestamps) and also drives the
 * aviator face's 30-minute counter.
 */
#include "nav.h"
#include "kit.h"
#include "model.h"
#include "theme.h"
#include "fonts/kg_fonts.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *s_main, *s_cents, *s_laps, *s_btn_go, *s_btn_aux;
static lv_timer_t *s_timer;

static void fmt_time(char *buf, size_t n, uint32_t ms)
{
    uint32_t s = ms / 1000;
    snprintf(buf, n, "%02lu:%02lu", (unsigned long)(s / 60 % 100), (unsigned long)(s % 60));
}

static void refresh(void)
{
    uint32_t ms = model_chrono_elapsed_ms();
    char buf[16];
    fmt_time(buf, sizeof buf, ms);
    lv_label_set_text(s_main, buf);
    lv_label_set_text_fmt(s_cents, ".%02lu", (unsigned long)(ms / 10 % 100));
    lv_obj_align_to(s_cents, s_main, LV_ALIGN_OUT_RIGHT_BOTTOM, 2, -8); /* follows the width of the minutes */

    bool running = model_chrono_running();
    kit_button_set_text(s_btn_go, running ? "Stop" : (ms ? "Resume" : "Start"));
    kit_button_set_text(s_btn_aux, running ? "Lap" : "Reset");
    if (!running && !ms) lv_obj_add_state(s_btn_aux, LV_STATE_DISABLED);
    else lv_obj_clear_state(s_btn_aux, LV_STATE_DISABLED);

    uint32_t laps[KG_MAX_LAPS];
    int n = model_chrono_laps(laps);
    char text[96] = "", line[32];
    for (int i = 0; i < n && i < 3; i++) {
        uint32_t split = laps[i] - (i + 1 < n ? laps[i + 1] : 0);
        fmt_time(buf, sizeof buf, split);
        snprintf(line, sizeof line, "%sLap %d   %s.%02lu", i ? "\n" : "", n - i, buf,
                 (unsigned long)(split / 10 % 100));
        strncat(text, line, sizeof text - strlen(text) - 1);
    }
    lv_label_set_text(s_laps, text);
}

static void tick(lv_timer_t *t)
{
    (void)t;
    refresh();
}

static void go_cb(lv_event_t *e)
{
    (void)e;
    model_chrono_start_stop();
    refresh();
}

static void aux_cb(lv_event_t *e)
{
    (void)e;
    if (model_chrono_running()) model_chrono_lap();
    else model_chrono_reset();
    refresh();
}

static lv_obj_t *create(void)
{
    lv_obj_t *scr = kit_screen();
    kit_title(scr, "STOPWATCH");

    s_main = kit_label(scr, &kg_font_digits_56, KG_C_TEXT, "00:00");
    lv_obj_align(s_main, LV_ALIGN_TOP_MID, -16, 22);
    s_cents = kit_label(scr, &kg_font_digits_30, KG_C_ACCENT, ".00");
    lv_obj_align_to(s_cents, s_main, LV_ALIGN_OUT_RIGHT_BOTTOM, 2, -8);

    s_laps = kit_label(scr, &kg_font_text_16, KG_C_TEXT_DIM, "");
    lv_obj_set_style_text_align(s_laps, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_laps, LV_ALIGN_TOP_MID, 0, 100);

    s_btn_aux = kit_button(scr, "Reset", false, 106, 44, aux_cb, NULL);
    lv_obj_align(s_btn_aux, LV_ALIGN_BOTTOM_LEFT, 10, -12);
    lv_obj_set_style_text_opa(s_btn_aux, LV_OPA_40, LV_STATE_DISABLED);
    s_btn_go = kit_button(scr, "Start", true, 106, 44, go_cb, NULL);
    lv_obj_align(s_btn_go, LV_ALIGN_BOTTOM_RIGHT, -10, -12);

    s_timer = lv_timer_create(tick, 40, NULL); /* 25 Hz for the hundredths, tiny area */
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

const kg_app_t kg_app_chrono = {
    .name = "chrono",
    .label = "Stopwatch",
    .icon = KG_ICON_CHRONO,
    .create = create,
    .on_show = on_show,
    .on_hide = on_hide,
};
