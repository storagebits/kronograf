/*
 * App Alarmes : quatre alarmes quotidiennes, persistantes (NVS). Toucher l'heure ouvre
 * le réglage à deux rouleaux ; l'interrupteur active ou désactive.
 */
#include "nav.h"
#include "kit.h"
#include "model.h"
#include "theme.h"
#include "fonts/kg_fonts.h"
#include <stdio.h>

static lv_obj_t *s_time[KG_MAX_ALARMS], *s_sw[KG_MAX_ALARMS];
static lv_obj_t *s_editor, *s_roll_h, *s_roll_m;
static int s_editing = -1;

static void refresh(void)
{
    for (int i = 0; i < KG_MAX_ALARMS; i++) {
        kg_alarm_t a = model_alarm_get(i);
        lv_label_set_text_fmt(s_time[i], "%02d:%02d", a.hour, a.min);
        lv_obj_set_style_text_color(s_time[i], a.enabled ? KG_C_TEXT : KG_C_TEXT_DIM, 0);
        if (a.enabled) lv_obj_add_state(s_sw[i], LV_STATE_CHECKED);
        else lv_obj_clear_state(s_sw[i], LV_STATE_CHECKED);
    }
}

static void switch_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    kg_alarm_t a = model_alarm_get(i);
    a.enabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    model_alarm_set(i, a);
    kit_tick();
    refresh();
}

static void edit_cb(lv_event_t *e)
{
    s_editing = (int)(intptr_t)lv_event_get_user_data(e);
    kg_alarm_t a = model_alarm_get(s_editing);
    lv_roller_set_selected(s_roll_h, a.hour, LV_ANIM_OFF);
    lv_roller_set_selected(s_roll_m, a.min, LV_ANIM_OFF);
    lv_obj_clear_flag(s_editor, LV_OBJ_FLAG_HIDDEN);
    kit_tick();
}

static void ok_cb(lv_event_t *e)
{
    (void)e;
    kg_alarm_t a = model_alarm_get(s_editing);
    a.hour = (uint8_t)lv_roller_get_selected(s_roll_h);
    a.min = (uint8_t)lv_roller_get_selected(s_roll_m);
    a.enabled = true; /* régler une alarme, c'est vouloir qu'elle sonne */
    model_alarm_set(s_editing, a);
    lv_obj_add_flag(s_editor, LV_OBJ_FLAG_HIDDEN);
    s_editing = -1;
    refresh();
}

static lv_obj_t *roller(lv_obj_t *parent, int count)
{
    static char opts[2][60 * 3 + 1];
    char *buf = opts[count == 24 ? 0 : 1];
    char *p = buf;
    for (int i = 0; i < count; i++) p += sprintf(p, i ? "\n%02d" : "%02d", i);
    lv_obj_t *r = lv_roller_create(parent);
    lv_roller_set_options(r, buf, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(r, 3);
    lv_obj_set_width(r, 80);
    lv_obj_set_style_text_font(r, &kg_font_digits_30, 0);
    lv_obj_set_style_text_color(r, KG_C_TEXT_DIM, 0);
    lv_obj_set_style_bg_color(r, KG_C_BG, 0);
    lv_obj_set_style_border_color(r, KG_C_HAIRLINE, 0);
    lv_obj_set_style_text_color(r, KG_C_TEXT, LV_PART_SELECTED);
    lv_obj_set_style_bg_color(r, KG_C_BTN, LV_PART_SELECTED);
    lv_obj_set_style_text_align(r, LV_TEXT_ALIGN_CENTER, 0);
    return r;
}

static lv_obj_t *create(void)
{
    lv_obj_t *scr = kit_screen();
    kit_title(scr, "ALARMES");

    for (int i = 0; i < KG_MAX_ALARMS; i++) {
        lv_coord_t y = 26 + i * 52;
        lv_obj_t *row = lv_obj_create(scr);
        lv_obj_remove_style_all(row);
        lv_obj_set_pos(row, 10, y);
        lv_obj_set_size(row, 220, 46);
        lv_obj_set_style_bg_color(row, KG_C_SURFACE, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 10, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        s_time[i] = kit_label(row, &kg_font_digits_30, KG_C_TEXT, "--:--");
        lv_obj_align(s_time[i], LV_ALIGN_LEFT_MID, 14, 0);
        lv_obj_add_flag(s_time[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(s_time[i], 12);
        lv_obj_add_event_cb(s_time[i], edit_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        s_sw[i] = lv_switch_create(row);
        lv_obj_set_size(s_sw[i], 50, 26);
        lv_obj_align(s_sw[i], LV_ALIGN_RIGHT_MID, -12, 0);
        lv_obj_set_style_bg_color(s_sw[i], KG_C_BTN, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_sw[i], KG_C_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(s_sw[i], KG_C_HAND, LV_PART_KNOB);
        lv_obj_add_event_cb(s_sw[i], switch_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)i);
    }

    /* Réglage : panneau plein écran par-dessus la liste. */
    s_editor = lv_obj_create(scr);
    lv_obj_remove_style_all(s_editor);
    lv_obj_set_size(s_editor, KG_W, KG_H);
    lv_obj_set_style_bg_color(s_editor, KG_C_BG, 0);
    lv_obj_set_style_bg_opa(s_editor, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_editor, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *t = kit_label(s_editor, &kg_font_stencil_11, KG_C_TEXT_DIM, "HEURE DU RÉVEIL");
    lv_obj_set_style_text_letter_space(t, 2, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 6);
    s_roll_h = roller(s_editor, 24);
    lv_obj_align(s_roll_h, LV_ALIGN_TOP_MID, -46, 26);
    s_roll_m = roller(s_editor, 60);
    lv_obj_align(s_roll_m, LV_ALIGN_TOP_MID, 46, 26);
    lv_obj_t *colon = kit_label(s_editor, &kg_font_digits_30, KG_C_TEXT, ":");
    lv_obj_align(colon, LV_ALIGN_TOP_MID, 0, 72);
    lv_obj_t *ok = kit_button(s_editor, "Valider", true, 140, 44, ok_cb, NULL);
    lv_obj_align(ok, LV_ALIGN_BOTTOM_MID, 0, -12);

    refresh();
    return scr;
}

static void on_show(void)
{
    lv_obj_add_flag(s_editor, LV_OBJ_FLAG_HIDDEN);
    refresh();
}

const kg_app_t kg_app_alarms = {
    .name = "alarms",
    .label = "Alarmes",
    .icon = KG_ICON_ALARM,
    .create = create,
    .on_show = on_show,
};
