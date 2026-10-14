/*
 * System monitor: battery (voltage, charge / discharge current from the AXP202, die
 * temperature), memory (internal RAM and PSRAM with their low-water mark), CPU and frame
 * rate, uptime, sleep counters, last reset cause and firmware. Refreshed every second.
 */
#include "nav.h"
#include "kit.h"
#include "theme.h"
#include "fonts/kg_fonts.h"
#include "../hal/hal.h"
#include <stdio.h>

static lv_obj_t *s_list, *s_batt_big, *s_batt_cur, *s_batt_info, *s_ram_bar, *s_ram_txt, *s_ps_bar, *s_ps_txt, *s_cpu,
    *s_up, *s_fw;
static lv_timer_t *s_timer;

static lv_obj_t *section(const char *title)
{
    lv_obj_t *t = kit_label(s_list, &kg_font_stencil_11, KG_C_TEXT_DIM, title);
    lv_obj_set_style_text_letter_space(t, 2, 0);
    lv_obj_set_style_pad_top(t, 6, 0);
    return t;
}

static lv_obj_t *row(lv_coord_t h)
{
    lv_obj_t *r = lv_obj_create(s_list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, 224, h);
    lv_obj_set_style_bg_color(r, KG_C_SURFACE, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(r, 10, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    return r;
}

static lv_obj_t *meter(lv_obj_t *parent, lv_coord_t y)
{
    lv_obj_t *b = lv_bar_create(parent);
    lv_obj_set_size(b, 200, 6);
    lv_obj_set_pos(b, 12, y);
    lv_bar_set_range(b, 0, 1000);
    lv_obj_set_style_bg_color(b, KG_C_BTN, LV_PART_MAIN);
    lv_obj_set_style_bg_color(b, KG_C_OK, LV_PART_INDICATOR);
    return b;
}

static void fmt_kb(char *b, size_t n, uint32_t bytes)
{
    if (bytes >= 1024 * 1024) snprintf(b, n, "%.2f MB", bytes / 1048576.0);
    else snprintf(b, n, "%lu KB", (unsigned long)(bytes / 1024));
}

static void refresh(void)
{
    kg_sysinfo_t s;
    hal_sysinfo(&s);
    char b[96], f1[16], f2[16];

    snprintf(b, sizeof b, "%.2f V  %d %%", s.batt_mv / 1000.0, s.batt_pct);
    lv_label_set_text(s_batt_big, b);
    snprintf(b, sizeof b, "%s%.1f mA", s.batt_ma > 0 ? "+" : s.batt_ma < 0 ? "−" : "", s.batt_ma < 0 ? -s.batt_ma : s.batt_ma);
    lv_label_set_text(s_batt_cur, b);
    lv_obj_set_style_text_color(s_batt_cur, s.batt_ma > 0 ? KG_C_OK : KG_C_WARN, 0);
    if (s.usb) snprintf(b, sizeof b, "USB %.2f V · %s · AXP %.0f °C", s.vbus_mv / 1000.0, s.charging ? "charging" : "full", (double)s.pmu_temp);
    else snprintf(b, sizeof b, "On battery · AXP %.0f °C", (double)s.pmu_temp);
    lv_label_set_text(s_batt_info, b);

    uint32_t used = s.heap_total - s.heap_free;
    lv_bar_set_value(s_ram_bar, s.heap_total ? (int32_t)((uint64_t)used * 1000 / s.heap_total) : 0, LV_ANIM_OFF);
    bool low = s.heap_total && s.heap_free * 100 / s.heap_total < 30;
    lv_obj_set_style_bg_color(s_ram_bar, low ? KG_C_WARN : KG_C_OK, LV_PART_INDICATOR);
    fmt_kb(f1, sizeof f1, s.heap_free);
    fmt_kb(f2, sizeof f2, s.heap_min);
    snprintf(b, sizeof b, "Internal: %s free (%lu %%) · low %s", f1,
             (unsigned long)(s.heap_total ? s.heap_free * 100 / s.heap_total : 0), f2);
    lv_label_set_text(s_ram_txt, b);
    uint32_t pused = s.psram_total - s.psram_free;
    lv_bar_set_value(s_ps_bar, s.psram_total ? (int32_t)((uint64_t)pused * 1000 / s.psram_total) : 0, LV_ANIM_OFF);
    fmt_kb(f1, sizeof f1, s.psram_free);
    fmt_kb(f2, sizeof f2, s.psram_total);
    snprintf(b, sizeof b, "PSRAM: %s free of %s", f1, f2);
    lv_label_set_text(s_ps_txt, b);

    lv_label_set_text_fmt(s_cpu, "ESP32 %u MHz · %.0f fps\nWakes %lu · light sleeps %lu", s.cpu_mhz, (double)s.fps,
                          (unsigned long)s.wakes, (unsigned long)s.light_sleeps);
    uint32_t u = s.uptime_s;
    if (u >= 86400) snprintf(b, sizeof b, "Up %lu d %02lu:%02lu", (unsigned long)(u / 86400), (unsigned long)(u / 3600 % 24), (unsigned long)(u / 60 % 60));
    else snprintf(b, sizeof b, "Up %02lu:%02lu:%02lu", (unsigned long)(u / 3600), (unsigned long)(u / 60 % 60), (unsigned long)(u % 60));
    lv_label_set_text_fmt(s_up, "%s\nLast reset: %s", b, s.reset_reason);
    lv_label_set_text_fmt(s_fw, "Kronograf %s\nApp %lu KB of %lu KB", s.version, (unsigned long)s.app_kb,
                          (unsigned long)s.app_slot_kb);
}

static void tick(lv_timer_t *t)
{
    (void)t;
    refresh();
}

static lv_obj_t *text_row(lv_coord_t h, const lv_font_t *font, lv_color_t c)
{
    lv_obj_t *r = row(h);
    lv_obj_t *l = kit_label(r, font, c, "");
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 12, 0);
    return l;
}

static lv_obj_t *create(void)
{
    lv_obj_t *scr = kit_screen();
    s_list = lv_obj_create(scr);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_size(s_list, KG_W, KG_H);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_list, 5, 0);
    lv_obj_set_style_pad_ver(s_list, 6, 0);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);
    kit_title(s_list, "SYSTEM");
    lv_obj_set_align(lv_obj_get_child(s_list, 0), LV_ALIGN_DEFAULT);

    section("BATTERY");
    lv_obj_t *r = row(62);
    s_batt_big = kit_label(r, &kg_font_label_20, KG_C_TEXT, "");
    lv_obj_set_pos(s_batt_big, 12, 6);
    s_batt_cur = kit_label(r, &kg_font_label_20, KG_C_WARN, "");
    lv_obj_align(s_batt_cur, LV_ALIGN_TOP_RIGHT, -12, 6);
    s_batt_info = kit_label(r, &kg_font_text_16, KG_C_TEXT_DIM, "");
    lv_obj_set_pos(s_batt_info, 12, 34);

    section("MEMORY");
    r = row(78);
    s_ram_bar = meter(r, 10);
    s_ram_txt = kit_label(r, &kg_font_text_16, KG_C_TEXT, "");
    lv_obj_set_pos(s_ram_txt, 12, 18);
    s_ps_bar = meter(r, 46);
    lv_obj_set_style_bg_color(s_ps_bar, KG_C_RAIN, LV_PART_INDICATOR);
    s_ps_txt = kit_label(r, &kg_font_text_16, KG_C_TEXT, "");
    lv_obj_set_pos(s_ps_txt, 12, 54);

    section("PROCESSOR");
    s_cpu = text_row(48, &kg_font_text_16, KG_C_TEXT);
    s_up = text_row(48, &kg_font_text_16, KG_C_TEXT);
    section("FIRMWARE");
    s_fw = text_row(48, &kg_font_text_16, KG_C_TEXT_DIM);

    s_timer = lv_timer_create(tick, 1000, NULL);
    lv_timer_pause(s_timer);
    refresh();
    return scr;
}

static void on_show(void)
{
    lv_obj_scroll_to_y(s_list, 0, LV_ANIM_OFF);
    refresh();
    lv_timer_resume(s_timer);
}

static void on_hide(void) { lv_timer_pause(s_timer); }

const kg_app_t kg_app_system = {
    .name = "system",
    .label = "System",
    .icon = KG_ICON_SYSTEM,
    .create = create,
    .on_show = on_show,
    .on_hide = on_hide,
};
