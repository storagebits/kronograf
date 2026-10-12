#include "wx_icon.h"
#include "theme.h"
#include <math.h>

/* Dessin sur une grille de 24 unités, mise à l'échelle par `s`. */
typedef struct {
    lv_draw_ctx_t *ctx;
    float cx, cy, s;
} pen_t;

static lv_coord_t X(const pen_t *p, float u) { return (lv_coord_t)lroundf(p->cx + u * p->s); }
static lv_coord_t Y(const pen_t *p, float u) { return (lv_coord_t)lroundf(p->cy + u * p->s); }

static void disc(const pen_t *p, float x, float y, float r, lv_color_t c)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = c;
    d.radius = LV_RADIUS_CIRCLE;
    lv_coord_t rr = (lv_coord_t)lroundf(r * p->s);
    lv_area_t a = {X(p, x) - rr, Y(p, y) - rr, X(p, x) + rr, Y(p, y) + rr};
    lv_draw_rect(p->ctx, &d, &a);
}

static void bar(const pen_t *p, float x1, float y1, float x2, float y2, float w, lv_color_t c)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = c;
    d.width = (lv_coord_t)fmaxf(1.0f, lroundf(w * p->s));
    d.round_start = d.round_end = 1;
    lv_point_t a = {X(p, x1), Y(p, y1)}, b = {X(p, x2), Y(p, y2)};
    lv_draw_line(p->ctx, &d, &a, &b);
}

static void sun(const pen_t *p, float x, float y, float r)
{
    disc(p, x, y, r, KG_C_WARN);
    for (int k = 0; k < 8; k++) {
        float a = k * (float)M_PI / 4;
        bar(p, x + (r + 2.2f) * cosf(a), y + (r + 2.2f) * sinf(a), x + (r + 4.2f) * cosf(a),
            y + (r + 4.2f) * sinf(a), 1.6f, KG_C_WARN);
    }
}

/* Nuage plein : base arrondie et deux bosses. */
static void cloud(const pen_t *p, float x, float y, lv_color_t c)
{
    disc(p, x - 4.5f, y + 1.0f, 4.5f, c);
    disc(p, x + 1.5f, y - 1.5f, 6.0f, c);
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = c;
    d.radius = (lv_coord_t)lroundf(3.5f * p->s);
    lv_area_t a = {X(p, x - 9.0f), Y(p, y - 0.5f), X(p, x + 9.0f), Y(p, y + 5.5f)};
    lv_draw_rect(p->ctx, &d, &a);
}

static void draw_cb(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    pen_t p = {lv_event_get_draw_ctx(e), (a.x1 + a.x2) / 2.0f, (a.y1 + a.y2) / 2.0f, lv_area_get_width(&a) / 24.0f};
    kg_wx_icon_t w = (kg_wx_icon_t)(uintptr_t)lv_obj_get_user_data(o);
    lv_color_t grey = KG_C_TEXT_DIM, white = KG_C_TEXT;

    switch (w) {
    case KG_WX_SUN: sun(&p, 0, 0, 5.0f); break;
    case KG_WX_MOON:
        disc(&p, 0, 0, 7.5f, KG_C_LUME);
        disc(&p, 4.0f, -3.0f, 6.5f, lv_obj_get_style_bg_color(lv_obj_get_parent(o), 0));
        break;
    case KG_WX_PARTLY:
        sun(&p, -3.5f, -4.0f, 3.8f);
        cloud(&p, 2.0f, 3.0f, white);
        break;
    case KG_WX_CLOUD: cloud(&p, 0, 0, white); break;
    case KG_WX_FOG:
        for (int i = 0; i < 3; i++) bar(&p, -9.0f + i * 2, -5.0f + i * 5, 9.0f - (2 - i) * 2, -5.0f + i * 5, 2.0f, grey);
        break;
    case KG_WX_DRIZZLE:
    case KG_WX_RAIN:
        cloud(&p, 0, -3.0f, grey);
        for (int i = 0; i < 3; i++) {
            float x = -5.0f + i * 5.0f;
            if (w == KG_WX_RAIN) bar(&p, x + 1.0f, 5.5f, x - 1.0f, 10.0f, 1.8f, KG_C_RAIN);
            else disc(&p, x, 7.5f, 1.2f, KG_C_RAIN);
        }
        break;
    case KG_WX_SNOW:
        cloud(&p, 0, -3.0f, grey);
        for (int i = 0; i < 3; i++) disc(&p, -5.0f + i * 5.0f, 7.5f + (i == 1 ? 1.5f : 0), 1.6f, white);
        break;
    case KG_WX_STORM: {
        cloud(&p, 0, -3.0f, grey);
        bar(&p, 1.0f, 3.0f, -2.0f, 7.5f, 1.8f, KG_C_WARN);
        bar(&p, -2.0f, 7.5f, 2.0f, 7.5f, 1.8f, KG_C_WARN);
        bar(&p, 2.0f, 7.5f, -1.0f, 11.5f, 1.8f, KG_C_WARN);
        break;
    }
    }
}

lv_obj_t *kg_wx_icon_create(lv_obj_t *parent, lv_coord_t size)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, size, size);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o, draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
    return o;
}

void kg_wx_icon_set(lv_obj_t *icon, kg_wx_icon_t which)
{
    if ((kg_wx_icon_t)(uintptr_t)lv_obj_get_user_data(icon) == which) return;
    lv_obj_set_user_data(icon, (void *)(uintptr_t)which);
    lv_obj_invalidate(icon);
}
