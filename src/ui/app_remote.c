/*
 * TV remote over the infrared LED: pick the brand, then power, volume, channels, mute and
 * input. Volume and channels repeat while held. "All TVs" sends the power code of every
 * brand in turn.
 */
#include "nav.h"
#include "kit.h"
#include "theme.h"
#include "fonts/kg_fonts.h"
#include "../hal/hal.h"

static lv_obj_t *s_brand_btn[4], *s_led, *s_hint;
static lv_timer_t *s_led_timer;
static int s_brand;
static bool s_toggle; /* RC5 toggle bit: flips on every new press */

static void choose_brand(int b)
{
    s_brand = b;
    for (int i = 0; i < KG_IR_N_BRANDS && i < 4; i++) {
        lv_obj_set_style_bg_color(s_brand_btn[i], i == b ? KG_C_ACCENT : KG_C_BTN, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(s_brand_btn[i], 0), i == b ? KG_C_BG : KG_C_TEXT, 0);
    }
}

static void led_off(lv_timer_t *t)
{
    lv_timer_pause(t);
    lv_obj_set_style_bg_opa(s_led, LV_OPA_20, 0);
}

static void send(int brand, kg_ir_key_t key, bool repeat)
{
    const kg_ir_brand_t *b = &KG_IR_BRANDS[brand];
    uint32_t code = b->keys[key];
    if (b->proto == KG_IR_RC5) {
        if (!repeat) s_toggle = !s_toggle;
        code |= (uint32_t)s_toggle << 11;
    }
    hal_ir_send(b->proto, code);
    lv_obj_set_style_bg_opa(s_led, LV_OPA_COVER, 0);
    lv_timer_reset(s_led_timer);
    lv_timer_resume(s_led_timer);
}

static void key_cb(lv_event_t *e)
{
    kg_ir_key_t key = (kg_ir_key_t)(intptr_t)lv_event_get_user_data(e);
    bool repeat = lv_event_get_code(e) == LV_EVENT_LONG_PRESSED_REPEAT;
    if (repeat && key != KG_KEY_VOL_UP && key != KG_KEY_VOL_DOWN && key != KG_KEY_CH_UP && key != KG_KEY_CH_DOWN) return;
    send(s_brand, key, repeat);
}

static void all_cb(lv_event_t *e)
{
    (void)e;
    for (int i = 0; i < KG_IR_N_BRANDS; i++) send(i, KG_KEY_POWER, false);
    lv_label_set_text(s_hint, "Power sent to every brand");
}

static void brand_cb(lv_event_t *e)
{
    int b = (int)(intptr_t)lv_event_get_user_data(e);
    choose_brand(b);
    hal_setting_set("ir_brand", b);
}

/* Power symbol on the main button. */
static void power_draw(lv_event_t *e)
{
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target(e), &a);
    lv_point_t c = {(a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2 + 1};
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.color = KG_C_BG;
    d.width = 3;
    d.rounded = 1;
    lv_draw_arc(ctx, &d, &c, 12, 300, 240);
    lv_draw_line_dsc_t l;
    lv_draw_line_dsc_init(&l);
    l.color = KG_C_BG;
    l.width = 3;
    l.round_start = l.round_end = 1;
    lv_point_t p1 = {c.x, c.y - 15}, p2 = {c.x, c.y - 4};
    lv_draw_line(ctx, &l, &p1, &p2);
}

static lv_obj_t *round_key(lv_obj_t *scr, const char *text, lv_coord_t cx, lv_coord_t cy, lv_coord_t d, kg_ir_key_t key)
{
    lv_obj_t *b = kit_button(scr, text, false, d, d, key_cb, (void *)(intptr_t)key);
    lv_obj_set_pos(b, cx - d / 2, cy - d / 2);
    lv_obj_add_event_cb(b, key_cb, LV_EVENT_LONG_PRESSED_REPEAT, (void *)(intptr_t)key);
    return b;
}

static void caption(lv_obj_t *scr, const char *text, lv_coord_t cx, lv_coord_t y)
{
    lv_obj_t *l = kit_label(scr, &kg_font_stencil_11, KG_C_TEXT_DIM, text);
    lv_obj_set_style_text_letter_space(l, 2, 0);
    lv_obj_set_width(l, 60);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(l, cx - 30, y);
}

static lv_obj_t *create(void)
{
    lv_obj_t *scr = kit_screen();
    kit_title(scr, "TV REMOTE");
    s_led = lv_obj_create(scr);
    lv_obj_remove_style_all(s_led);
    lv_obj_set_size(s_led, 7, 7);
    lv_obj_set_pos(s_led, 170, 8);
    lv_obj_set_style_radius(s_led, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_led, KG_C_ALERT, 0);
    lv_obj_set_style_bg_opa(s_led, LV_OPA_20, 0);

    static const lv_coord_t W[] = {66, 40, 50, 58}; /* fitted to the names */
    for (int i = 0, x = 7; i < KG_IR_N_BRANDS && i < 4; x += W[i] + 6, i++) {
        s_brand_btn[i] = kit_button(scr, KG_IR_BRANDS[i].name, false, W[i], 26, brand_cb, (void *)(intptr_t)i);
        lv_obj_set_style_text_font(lv_obj_get_child(s_brand_btn[i], 0), &kg_font_text_16, 0);
        lv_obj_set_pos(s_brand_btn[i], x, 24);
    }

    round_key(scr, "+", 40, 84, 46, KG_KEY_VOL_UP);
    round_key(scr, "−", 40, 150, 46, KG_KEY_VOL_DOWN);
    caption(scr, "VOL", 40, 112);
    round_key(scr, "+", 200, 84, 46, KG_KEY_CH_UP);
    round_key(scr, "−", 200, 150, 46, KG_KEY_CH_DOWN);
    caption(scr, "CH", 200, 112);

    lv_obj_t *pw = kit_button(scr, "", true, 64, 64, key_cb, (void *)(intptr_t)KG_KEY_POWER);
    lv_obj_set_style_bg_color(pw, KG_C_ALERT, 0);
    lv_obj_set_pos(pw, 120 - 32, 60);
    lv_obj_add_event_cb(pw, power_draw, LV_EVENT_DRAW_MAIN_END, NULL);
    lv_obj_t *mute = kit_button(scr, "Mute", false, 64, 34, key_cb, (void *)(intptr_t)KG_KEY_MUTE);
    lv_obj_set_pos(mute, 120 - 32, 134);

    lv_obj_t *in = kit_button(scr, "Input", false, 100, 36, key_cb, (void *)(intptr_t)KG_KEY_INPUT);
    lv_obj_set_pos(in, 12, 184);
    lv_obj_t *all = kit_button(scr, "All TVs", false, 100, 36, all_cb, NULL);
    lv_obj_set_pos(all, KG_W - 112, 184);
    s_hint = kit_label(scr, &kg_font_text_16, KG_C_TEXT_DIM, "");
    lv_obj_set_width(s_hint, KG_W);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_hint, 0, 222);

    s_led_timer = lv_timer_create(led_off, 150, NULL);
    lv_timer_pause(s_led_timer);
    return scr;
}

static void on_show(void)
{
    int b = (int)hal_setting_get("ir_brand", 0);
    choose_brand(b >= 0 && b < KG_IR_N_BRANDS ? b : 0);
    lv_label_set_text(s_hint, hal_ir_available() ? "" : "No infrared LED on this watch");
}

const kg_app_t kg_app_remote = {
    .name = "remote",
    .label = "Remote",
    .icon = KG_ICON_REMOTE,
    .create = create,
    .on_show = on_show,
};
