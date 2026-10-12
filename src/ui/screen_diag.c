/*
 * Tranche 0 — écran de diagnostic : révision détectée et inventaire matériel réel.
 */
#include "ui.h"
#include "theme.h"
#include "fonts/kg_fonts.h"
#include "../hal/hal.h"
#include <stdio.h>

static lv_obj_t *s_clock;
static lv_obj_t *s_batt;

static const char *rev_name(kg_rev_t rev)
{
    switch (rev) {
    case KG_REV_V1: return "T-Watch 2020 V1";
    case KG_REV_V2: return "T-Watch 2020 V2";
    case KG_REV_V3: return "T-Watch 2020 V3";
    default:        return "Révision inconnue";
    }
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text);
    return l;
}

/* Une ligne d'inventaire : pastille (présent / absent) + nom. */
static void item(lv_obj_t *grid, int col, int row, const char *name, bool present)
{
    lv_obj_t *cell = lv_obj_create(grid);
    lv_obj_remove_style_all(cell);
    lv_obj_set_size(cell, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(cell, 6, 0);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, col, 1, LV_GRID_ALIGN_CENTER, row, 1);

    lv_obj_t *dot = lv_obj_create(cell);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 7, 7);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    if (present) {
        lv_obj_set_style_bg_color(dot, KG_C_OK, 0);
        lv_obj_set_style_shadow_color(dot, KG_C_OK, 0);
        lv_obj_set_style_shadow_width(dot, 8, 0);
        lv_obj_set_style_shadow_opa(dot, LV_OPA_60, 0);
    } else {
        lv_obj_set_style_bg_color(dot, KG_C_HAIRLINE, 0);
    }

    label(cell, &kg_font_text_16, present ? KG_C_TEXT : KG_C_TEXT_DIM, name);
}

lv_obj_t *ui_diag_create(void)
{
    const kg_inventory_t *inv = hal_inventory();

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, KG_C_BG, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *kicker = label(scr, &kg_font_text_16, KG_C_TEXT_DIM, "KRONOGRAF · DIAGNOSTIC");
    lv_obj_set_style_text_letter_space(kicker, 2, 0);
    lv_obj_align(kicker, LV_ALIGN_TOP_LEFT, KG_GUTTER, 10);

    lv_obj_t *title = label(scr, &kg_font_title_34, KG_C_TEXT, rev_name(inv->rev));
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, KG_GUTTER, 28);

    char mem[64];
    if (inv->psram)
        snprintf(mem, sizeof mem, "Flash %lu Mo · PSRAM %lu Ko",
                 (unsigned long)inv->flash_mb, (unsigned long)inv->psram_kb);
    else
        snprintf(mem, sizeof mem, "Flash %lu Mo · sans PSRAM", (unsigned long)inv->flash_mb);
    lv_obj_t *sub = label(scr, &kg_font_text_16, KG_C_WARN, mem);
    lv_obj_align(sub, LV_ALIGN_TOP_LEFT, KG_GUTTER, 66);

    lv_obj_t *rule = lv_obj_create(scr);
    lv_obj_remove_style_all(rule);
    lv_obj_set_size(rule, KG_W - 2 * KG_GUTTER, 1);
    lv_obj_set_style_bg_color(rule, KG_C_HAIRLINE, 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
    lv_obj_align(rule, LV_ALIGN_TOP_LEFT, KG_GUTTER, 92);

    static const lv_coord_t cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t rows[] = {22, 22, 22, 22, 22, LV_GRID_TEMPLATE_LAST};
    lv_obj_t *grid = lv_obj_create(scr);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, KG_W - 2 * KG_GUTTER, 5 * 22);
    lv_obj_set_grid_dsc_array(grid, cols, rows);
    lv_obj_align(grid, LV_ALIGN_TOP_LEFT, KG_GUTTER, 100);

    item(grid, 0, 0, "PMU AXP202",      inv->pmu);
    item(grid, 0, 1, "RTC PCF8563",     inv->rtc);
    item(grid, 0, 2, "Accél. BMA423",   inv->accel);
    item(grid, 0, 3, "Tactile FT6336",  inv->touch);
    item(grid, 0, 4, "Vibreur",         inv->motor);
    item(grid, 1, 0, "Haut-parleur",    inv->speaker);
    item(grid, 1, 1, "Infrarouge",      inv->ir_tx);
    item(grid, 1, 2, "GPS",             inv->gps);
    item(grid, 1, 3, "Carte microSD",   inv->sdcard);
    item(grid, 1, 4, "Microphone",      inv->mic);

    s_clock = label(scr, &kg_font_label_20, KG_C_LUME, "--:--:--");
    lv_obj_align(s_clock, LV_ALIGN_BOTTOM_LEFT, KG_GUTTER, -8);

    s_batt = label(scr, &kg_font_label_20, KG_C_TEXT, "");
    lv_obj_align(s_batt, LV_ALIGN_BOTTOM_RIGHT, -KG_GUTTER, -8);

    ui_diag_update();
    return scr;
}

void ui_diag_update(void)
{
    struct tm t;
    hal_now(&t, NULL);
    lv_label_set_text_fmt(s_clock, "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);

    kg_battery_t b = hal_battery();
    if (b.percent < 0)
        lv_label_set_text(s_batt, "Batterie ?");
    else
        lv_label_set_text_fmt(s_batt, "%s%d %%  %d,%02d V", b.charging ? "Charge · " : "",
                              b.percent, b.millivolts / 1000, (b.millivolts % 1000) / 10);
}
