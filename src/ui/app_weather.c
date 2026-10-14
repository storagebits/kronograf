/*
 * Weather app (Montréal): current conditions, the next hours, and the next STM buses at
 * the followed stops when an API key is provided. Everything comes from the HAL cache;
 * opening the app requests an update when the reading is old (Wi-Fi on for the request).
 */
#include "nav.h"
#include "kit.h"
#include "theme.h"
#include "wx_icon.h"
#include "fonts/kg_fonts.h"
#include "../hal/hal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define N_COLS 6        /* upcoming hours shown, one every 2 h */
#define WX_MAX_AGE_S (10 * 60)
#define BUS_MAX_AGE_S 60

static lv_obj_t *s_icon, *s_temp, *s_desc, *s_range, *s_age, *s_empty, *s_band;
static lv_obj_t *s_col_h[N_COLS], *s_col_icon[N_COLS], *s_col_t[N_COLS];
static lv_obj_t *s_bus_title, *s_bus_line[2], *s_bus_route[2], *s_bus_times[2], *s_bus_hint;
static lv_timer_t *s_timer;
static uint32_t s_ticks;

/* "−3°" with a real minus sign. */
static void fmt_temp(char *buf, size_t n, float t)
{
    int v = (int)lroundf(t);
    if (v < 0) snprintf(buf, n, "\xE2\x88\x92%d°", -v);
    else snprintf(buf, n, "%d°", v);
}

/* Age of the reading, stencilled on two lines next to the temperature. */
static void fmt_age(char *buf, size_t n, const char *first, time_t fetched)
{
    long s = (long)(time(NULL) - fetched);
    if (s < 90) snprintf(buf, n, "%s\nJUST NOW", first);
    else if (s < 3600) snprintf(buf, n, "%s\n%ld MIN AGO", first, s / 60);
    else if (s < 86400) snprintf(buf, n, "%s\n%ld H AGO", first, s / 3600);
    else snprintf(buf, n, "%s\n%ld D AGO", first, s / 86400);
}

static void refresh_weather(void)
{
    kg_weather_t wx;
    bool ok = hal_weather(&wx);
    const char *status = hal_weather_status();
    char buf[64], t1[16], t2[16];

    lv_obj_t *const parts[] = {s_icon, s_temp, s_desc, s_range, s_band, s_age};
    for (unsigned i = 0; i < sizeof parts / sizeof parts[0]; i++) {
        if (ok) lv_obj_clear_flag(parts[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(parts[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (!ok) {
        lv_obj_clear_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_empty, hal_weather_busy() ? "Updating the weather…" : status ? status : "No reading yet");
        for (int i = 0; i < N_COLS; i++) {
            lv_obj_add_flag(s_col_h[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_col_icon[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_col_t[i], LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    lv_obj_add_flag(s_empty, LV_OBJ_FLAG_HIDDEN);

    kg_wx_icon_set(s_icon, kg_weather_icon(wx.code, wx.day));
    fmt_temp(buf, sizeof buf, wx.temp);
    lv_label_set_text(s_temp, buf);
    fmt_temp(t1, sizeof t1, wx.feels);
    snprintf(buf, sizeof buf, "%s, feels like %s", kg_weather_label(wx.code), t1);
    lv_label_set_text(s_desc, buf);
    fmt_temp(t1, sizeof t1, wx.tmin);
    fmt_temp(t2, sizeof t2, wx.tmax);
    snprintf(buf, sizeof buf, "low %s   high %s   rain %u %%", t1, t2, wx.pop_max);
    lv_label_set_text(s_range, buf);

    /* Columns: skip hours already past. */
    time_t now = time(NULL);
    int first = 0;
    while (first < wx.n_hours - 1 && wx.hours[first + 1].t <= now) first++;
    for (int i = 0; i < N_COLS; i++) {
        int k = first + 1 + i * 2;
        bool has = k < wx.n_hours;
        lv_obj_t *col[] = {s_col_h[i], s_col_icon[i], s_col_t[i]};
        for (int j = 0; j < 3; j++) {
            if (has) lv_obj_clear_flag(col[j], LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(col[j], LV_OBJ_FLAG_HIDDEN);
        }
        if (!has) continue;
        struct tm tm;
        localtime_r(&wx.hours[k].t, &tm);
        lv_label_set_text_fmt(s_col_h[i], "%dH", tm.tm_hour);
        kg_wx_icon_set(s_col_icon[i], kg_weather_icon(wx.hours[k].code, tm.tm_hour >= 7 && tm.tm_hour < 19));
        fmt_temp(buf, sizeof buf, wx.hours[k].temp);
        lv_label_set_text(s_col_t[i], buf);
    }

    /* Data present: a failed update erases nothing, it is flagged here. */
    bool busy = hal_weather_busy();
    fmt_age(buf, sizeof buf, busy ? "UPDATING" : status ? "OFFLINE" : "UPDATED", wx.fetched);
    lv_label_set_text(s_age, buf);
    lv_obj_set_style_text_color(s_age, status && !busy ? KG_C_WARN : KG_C_TEXT_DIM, 0);
}

static void bus_hint(const char *text)
{
    lv_label_set_text(s_bus_hint, text);
    lv_obj_clear_flag(s_bus_hint, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 2; i++) lv_obj_add_flag(s_bus_line[i], LV_OBJ_FLAG_HIDDEN);
}

static void refresh_bus(void)
{
    if (!hal_bus_available()) {
        lv_obj_add_flag(s_bus_title, LV_OBJ_FLAG_HIDDEN);
        bus_hint("STM buses: add an API key in Settings");
        return;
    }
    lv_obj_clear_flag(s_bus_title, LV_OBJ_FLAG_HIDDEN);
    const char *stops = hal_bus_stops();
    lv_label_set_text_fmt(s_bus_title, "STM BUS  %s", stops[0] ? stops : "");
    kg_bus_t bus;
    bool ok = hal_bus(&bus);
    const char *status = hal_bus_status();
    if (!stops[0]) {
        bus_hint("Pick a stop in Settings");
        return;
    }
    if (!ok) {
        bus_hint(status ? status : "Fetching departures…");
        return;
    }

    /* At most two routes, each with its next two departures. */
    time_t now = time(NULL);
    char routes[2][8] = {"", ""};
    char times[2][32] = {"", ""};
    int count[2] = {0, 0}, n_routes = 0;
    for (int i = 0; i < bus.n; i++) {
        long min = (long)(bus.deps[i].t - now + 30) / 60;
        if (min < 0) continue;
        int r = 0;
        while (r < n_routes && strcmp(routes[r], bus.deps[i].route)) r++;
        if (r == n_routes) {
            if (n_routes == 2) continue;
            snprintf(routes[n_routes++], sizeof routes[0], "%s", bus.deps[i].route);
        }
        if (count[r] == 2) continue;
        size_t len = strlen(times[r]);
        if (count[r]) len += (size_t)snprintf(times[r] + len, sizeof times[r] - len, "   then   ");
        if (min == 0) snprintf(times[r] + len, sizeof times[r] - len, "due");
        else snprintf(times[r] + len, sizeof times[r] - len, "%ld min", min);
        count[r]++;
    }
    if (!n_routes) {
        bus_hint("No departures scheduled");
        return;
    }
    lv_obj_add_flag(s_bus_hint, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 2; i++) {
        if (i >= n_routes) {
            lv_obj_add_flag(s_bus_line[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(s_bus_line[i], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_bus_route[i], routes[i]);
        lv_label_set_text(s_bus_times[i], times[i]);
    }
}

static void tick(lv_timer_t *t)
{
    (void)t;
    if (++s_ticks % 30 == 0) hal_bus_refresh(BUS_MAX_AGE_S); /* every 30 s, if stale */
    refresh_weather();
    refresh_bus();
}

static lv_obj_t *create(void)
{
    lv_obj_t *scr = kit_screen();
    kit_title(scr, "WEATHER  MONTREAL");

    s_icon = kg_wx_icon_create(scr, 52);
    lv_obj_set_pos(s_icon, 22, 26);
    s_temp = kit_label(scr, &kg_font_digits_56, KG_C_TEXT, "");
    lv_obj_set_pos(s_temp, 84, 18);
    s_age = kit_label(scr, &kg_font_stencil_11, KG_C_TEXT_DIM, "");
    lv_obj_set_style_text_line_space(s_age, 3, 0);
    lv_obj_set_pos(s_age, 160, 42);
    s_desc = kit_label(scr, &kg_font_text_16, KG_C_TEXT, "");
    lv_obj_set_width(s_desc, 220);
    lv_label_set_long_mode(s_desc, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_desc, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_desc, 10, 82);
    s_range = kit_label(scr, &kg_font_text_16, KG_C_TEXT_DIM, "");
    lv_obj_set_width(s_range, 220);
    lv_obj_set_style_text_align(s_range, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_range, 10, 100);
    s_empty = kit_label(scr, &kg_font_text_16, KG_C_TEXT_DIM, "");
    lv_obj_set_width(s_empty, 200);
    lv_obj_set_style_text_align(s_empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_empty, LV_ALIGN_TOP_MID, 0, 60);

    /* Hourly strip on a recessed background. */
    lv_obj_t *band = s_band = lv_obj_create(scr);
    lv_obj_remove_style_all(band);
    lv_obj_set_size(band, 228, 58);
    lv_obj_set_pos(band, 6, 122);
    lv_obj_set_style_bg_color(band, KG_C_SURFACE, 0);
    lv_obj_set_style_bg_opa(band, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(band, 10, 0);
    lv_obj_clear_flag(band, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    for (int i = 0; i < N_COLS; i++) {
        lv_coord_t cx = 6 + 19 + i * 38;
        s_col_h[i] = kit_label(scr, &kg_font_stencil_11, KG_C_TEXT_DIM, "");
        lv_obj_set_width(s_col_h[i], 38);
        lv_obj_set_style_text_align(s_col_h[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(s_col_h[i], cx - 19, 126);
        s_col_icon[i] = kg_wx_icon_create(scr, 20);
        lv_obj_set_pos(s_col_icon[i], cx - 10, 139);
        lv_obj_set_style_bg_color(s_col_icon[i], KG_C_SURFACE, 0); /* background behind the crescent moon */
        s_col_t[i] = kit_label(scr, &kg_font_text_16, KG_C_TEXT, "");
        lv_obj_set_width(s_col_t[i], 38);
        lv_obj_set_style_text_align(s_col_t[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(s_col_t[i], cx - 19, 159);
    }

    /* STM buses */
    s_bus_title = kit_label(scr, &kg_font_stencil_11, KG_C_TEXT_DIM, "");
    lv_obj_set_style_text_letter_space(s_bus_title, 1, 0);
    lv_obj_set_pos(s_bus_title, 12, 186);
    for (int i = 0; i < 2; i++) {
        s_bus_line[i] = lv_obj_create(scr);
        lv_obj_remove_style_all(s_bus_line[i]);
        lv_obj_set_size(s_bus_line[i], 220, 20);
        lv_obj_set_pos(s_bus_line[i], 12, 199 + i * 20);
        lv_obj_clear_flag(s_bus_line[i], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        s_bus_route[i] = kit_label(s_bus_line[i], &kg_font_text_16, KG_C_BG, "");
        lv_obj_set_width(s_bus_route[i], 34);
        lv_obj_set_style_text_align(s_bus_route[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_bg_color(s_bus_route[i], KG_C_RAIN, 0); /* route badge */
        lv_obj_set_style_bg_opa(s_bus_route[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(s_bus_route[i], 4, 0);
        lv_obj_align(s_bus_route[i], LV_ALIGN_LEFT_MID, 0, 0);
        s_bus_times[i] = kit_label(s_bus_line[i], &kg_font_text_16, KG_C_TEXT, "");
        lv_obj_align(s_bus_times[i], LV_ALIGN_LEFT_MID, 44, 0);
    }
    s_bus_hint = kit_label(scr, &kg_font_text_16, KG_C_TEXT_DIM, "");
    lv_obj_set_width(s_bus_hint, 220);
    lv_obj_set_style_text_align(s_bus_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_bus_hint, 10, 204);


    s_timer = lv_timer_create(tick, 1000, NULL);
    lv_timer_pause(s_timer);
    refresh_weather();
    refresh_bus();
    return scr;
}

static void on_show(void)
{
    hal_weather_refresh(WX_MAX_AGE_S);
    hal_bus_refresh(BUS_MAX_AGE_S);
    s_ticks = 0;
    refresh_weather();
    refresh_bus();
    lv_timer_resume(s_timer);
}

static void on_hide(void) { lv_timer_pause(s_timer); }

const kg_app_t kg_app_weather = {
    .name = "weather",
    .label = "Weather",
    .icon = KG_ICON_WEATHER,
    .create = create,
    .on_show = on_show,
    .on_hide = on_hide,
};
