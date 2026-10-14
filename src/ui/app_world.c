/*
 * World clock: twelve cities from Honolulu to Auckland, summer time included (POSIX rules,
 * kg_tz). Each row: code, city, local time, UTC offset and day shift; a small sun or moon
 * tells day from night. The home city is in orange.
 */
#include "nav.h"
#include "faces.h"
#include "kit.h"
#include "theme.h"
#include "fonts/kg_fonts.h"
#include "../data/kg_tz.h"
#include "../hal/hal.h"
#include <stdio.h>

#define HOME 2 /* Montréal in KG_ZONES */
#define ROW_H 44

static lv_obj_t *s_list, *s_time[16], *s_info[16], *s_dot[16];
static lv_timer_t *s_timer;

static void refresh(void)
{
    struct tm now;
    hal_now(&now, NULL);
    time_t t = mktime(&now); /* the watch's own local time → UTC instant */
    struct tm home;
    kg_tz_local(KG_ZONES[HOME].tz, t, &home, NULL);
    for (int i = 0; i < KG_N_ZONES && i < 16; i++) {
        struct tm lt;
        bool dst;
        kg_tz_local(KG_ZONES[i].tz, t, &lt, &dst);
        char b[32];
        bool h24 = ui_fmt24();
        snprintf(b, sizeof b, "%02d:%02d", h24 ? lt.tm_hour : (lt.tm_hour + 11) % 12 + 1, lt.tm_min);
        lv_label_set_text(s_time[i], b);

        int32_t off = kg_tz_offset(KG_ZONES[i].tz, t, NULL);
        int days = (int)((lt.tm_year * 400 + lt.tm_yday) - (home.tm_year * 400 + home.tm_yday));
        if (days > 1) days = -1; /* across New Year's Eve */
        char o[12] = "UTC";
        if (off) snprintf(o, sizeof o, "UTC%s%d%s", off < 0 ? "−" : "+", (int)(off < 0 ? -off : off) / 3600,
                          off % 3600 ? ":30" : "");
        snprintf(b, sizeof b, "%s%s%s%s", h24 ? "" : lt.tm_hour < 12 ? "AM · " : "PM · ", o, dst ? " · summer" : "",
                 days > 0 ? " · tomorrow" : days < 0 ? " · yesterday" : "");
        lv_label_set_text(s_info[i], b);
        bool day = lt.tm_hour >= 7 && lt.tm_hour < 19;
        lv_obj_set_style_bg_color(s_dot[i], day ? KG_C_WARN : KG_C_SURFACE, 0);
        lv_obj_set_style_border_color(s_dot[i], day ? KG_C_WARN : KG_C_TEXT_DIM, 0);
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
    s_list = lv_obj_create(scr);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_size(s_list, KG_W, KG_H);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_list, 4, 0);
    lv_obj_set_style_pad_ver(s_list, 6, 0);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);
    kit_title(s_list, "WORLD CLOCK");
    lv_obj_set_align(lv_obj_get_child(s_list, 0), LV_ALIGN_DEFAULT);

    for (int i = 0; i < KG_N_ZONES && i < 16; i++) {
        lv_obj_t *r = lv_obj_create(s_list);
        lv_obj_remove_style_all(r);
        lv_obj_set_size(r, 224, ROW_H);
        lv_obj_set_style_bg_color(r, KG_C_SURFACE, 0);
        lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(r, 10, 0);
        lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
        if (i == HOME) {
            lv_obj_set_style_border_color(r, KG_C_ACCENT, 0);
            lv_obj_set_style_border_width(r, 1, 0);
        }
        s_dot[i] = lv_obj_create(r);
        lv_obj_remove_style_all(s_dot[i]);
        lv_obj_set_size(s_dot[i], 8, 8);
        lv_obj_set_style_radius(s_dot[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(s_dot[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_dot[i], 1, 0);
        lv_obj_align(s_dot[i], LV_ALIGN_TOP_LEFT, 10, 9);
        lv_obj_t *code = kit_label(r, &kg_font_stencil_11, i == HOME ? KG_C_ACCENT : KG_C_TEXT_DIM, KG_ZONES[i].code);
        lv_obj_align(code, LV_ALIGN_TOP_LEFT, 24, 7);
        lv_obj_t *city = kit_label(r, &kg_font_label_20, KG_C_TEXT, KG_ZONES[i].city);
        lv_obj_align(city, LV_ALIGN_TOP_LEFT, 54, 1);
        s_info[i] = kit_label(r, &kg_font_text_16, KG_C_TEXT_DIM, "");
        lv_obj_align(s_info[i], LV_ALIGN_BOTTOM_LEFT, 10, -2);
        s_time[i] = kit_label(r, &kg_font_digits_30, i == HOME ? KG_C_ACCENT : KG_C_TEXT, "");
        lv_obj_align(s_time[i], LV_ALIGN_RIGHT_MID, -10, 0);
    }
    s_timer = lv_timer_create(tick, 1000, NULL);
    lv_timer_pause(s_timer);
    refresh();
    return scr;
}

static void on_show(void)
{
    refresh();
    lv_obj_scroll_to_y(s_list, 0, LV_ANIM_OFF);
    lv_timer_resume(s_timer);
}

static void on_hide(void) { lv_timer_pause(s_timer); }

const kg_app_t kg_app_world = {
    .name = "world",
    .label = "World",
    .icon = KG_ICON_WORLD,
    .create = create,
    .on_show = on_show,
    .on_hide = on_hide,
};
