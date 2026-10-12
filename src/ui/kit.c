#include "kit.h"
#include "theme.h"
#include "fonts/kg_fonts.h"
#include "../hal/hal.h"

lv_obj_t *kit_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, KG_C_BG, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    return scr;
}

lv_obj_t *kit_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text);
    return l;
}

lv_obj_t *kit_title(lv_obj_t *scr, const char *text)
{
    lv_obj_t *l = kit_label(scr, &kg_font_stencil_11, KG_C_TEXT_DIM, text);
    lv_obj_set_style_text_letter_space(l, 2, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 6);
    return l;
}

void kit_tick(void) { hal_vibrate(12); }

static void press_cb(lv_event_t *e)
{
    (void)e;
    kit_tick();
}

lv_obj_t *kit_button(lv_obj_t *parent, const char *text, bool primary, lv_coord_t w, lv_coord_t h,
                     lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, h / 2, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, primary ? KG_C_ACCENT : KG_C_BTN, 0);
    lv_obj_set_style_bg_color(b, primary ? lv_color_darken(KG_C_ACCENT, 50) : KG_C_BTN_PRESS, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(b, KG_C_HAIRLINE, 0);
    lv_obj_set_style_border_width(b, primary ? 0 : 1, 0);
    lv_obj_set_style_transform_width(b, -2, LV_STATE_PRESSED); /* léger enfoncement */
    lv_obj_set_style_transform_height(b, -2, LV_STATE_PRESSED);
    lv_obj_t *l = kit_label(b, &kg_font_label_20, primary ? KG_C_BG : KG_C_TEXT, text);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, press_cb, LV_EVENT_PRESSED, NULL);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    return b;
}

void kit_button_set_text(lv_obj_t *btn, const char *text)
{
    lv_label_set_text(lv_obj_get_child(btn, 0), text);
}
