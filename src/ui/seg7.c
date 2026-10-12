#include "seg7.h"
#include "theme.h"
#include <string.h>

#define MAX_DIGITS 8
#define SLANT 0.09f /* inclinaison vers la droite, en px de x par px de y */

/* Segments a..g, bit 0 = a. */
static const uint8_t GLYPHS[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
#define GLYPH_MINUS 0x40

typedef struct {
    lv_obj_t *obj;
    int n, colon_after;
    lv_coord_t dw, dh, gap, colon_w;
    lv_color_t color;
    uint8_t mask[MAX_DIGITS];
    uint8_t flicker[MAX_DIGITS]; /* 0..255 : intensité de scintillement des segments nouvellement allumés */
    uint8_t fresh[MAX_DIGITS];   /* segments allumés depuis la dernière valeur */
    bool colon;
} seg7_t;

/* ------------------------------------------------------------ géométrie */

typedef struct { float x, y; } fpt;

/* Hexagone d'un segment entre p0 et p1 (horizontal ou vertical), épaisseur t. */
static int segment_poly(fpt p0, fpt p1, float t, float grow, lv_point_t *out, lv_coord_t ox, lv_coord_t oy, float h)
{
    float half = t / 2 + grow;
    bool horiz = p0.y == p1.y;
    fpt pts[6];
    if (horiz) {
        pts[0] = (fpt){p0.x - grow, p0.y};
        pts[1] = (fpt){p0.x + half, p0.y - half};
        pts[2] = (fpt){p1.x - half, p1.y - half};
        pts[3] = (fpt){p1.x + grow, p1.y};
        pts[4] = (fpt){p1.x - half, p1.y + half};
        pts[5] = (fpt){p0.x + half, p0.y + half};
    } else {
        pts[0] = (fpt){p0.x, p0.y - grow};
        pts[1] = (fpt){p0.x + half, p0.y + half};
        pts[2] = (fpt){p1.x + half, p1.y - half};
        pts[3] = (fpt){p1.x, p1.y + grow};
        pts[4] = (fpt){p1.x - half, p1.y - half};
        pts[5] = (fpt){p0.x - half, p0.y + half};
    }
    for (int i = 0; i < 6; i++) {
        float x = pts[i].x + (h / 2 - pts[i].y) * SLANT;
        out[i].x = ox + (lv_coord_t)(x + 0.5f);
        out[i].y = oy + (lv_coord_t)(pts[i].y + 0.5f);
    }
    return 6;
}

static void digit_segments(const seg7_t *s, fpt seg[7][2], float *t)
{
    float w = s->dw, h = s->dh;
    float th = w * 0.17f;  /* épaisseur */
    float g = th * 0.22f;  /* jour entre segments */
    float x0 = th / 2 + 1, x1 = w - th / 2 - 1 - h * SLANT;
    float y0 = th / 2, ym = h / 2, y1 = h - th / 2;
    *t = th;
    seg[0][0] = (fpt){x0 + g, y0}; seg[0][1] = (fpt){x1 - g, y0};   /* a */
    seg[1][0] = (fpt){x1, y0 + g}; seg[1][1] = (fpt){x1, ym - g};   /* b */
    seg[2][0] = (fpt){x1, ym + g}; seg[2][1] = (fpt){x1, y1 - g};   /* c */
    seg[3][0] = (fpt){x0 + g, y1}; seg[3][1] = (fpt){x1 - g, y1};   /* d */
    seg[4][0] = (fpt){x0, ym + g}; seg[4][1] = (fpt){x0, y1 - g};   /* e */
    seg[5][0] = (fpt){x0, y0 + g}; seg[5][1] = (fpt){x0, ym - g};   /* f */
    seg[6][0] = (fpt){x0 + g, ym}; seg[6][1] = (fpt){x1 - g, ym};   /* g */
}

static lv_coord_t digit_x(const seg7_t *s, int i)
{
    lv_coord_t x = i * (s->dw + s->gap);
    if (s->colon_after >= 0 && i > s->colon_after) x += s->colon_w;
    return x;
}

/* ---------------------------------------------------------------- dessin */

static void draw_poly(lv_draw_ctx_t *ctx, const lv_point_t *pts, lv_color_t c, lv_opa_t opa)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = c;
    d.bg_opa = opa;
    lv_draw_polygon(ctx, &d, pts, 6);
}

static void draw_cb(lv_event_t *e)
{
    seg7_t *s = lv_event_get_user_data(e);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t a;
    lv_obj_get_coords(s->obj, &a);

    fpt seg[7][2];
    float t;
    digit_segments(s, seg, &t);
    lv_color_t off = lv_color_mix(s->color, KG_C_PANEL, KG_SEG_OFF_MIX);
    lv_point_t p[6];

    for (int i = 0; i < s->n; i++) {
        lv_coord_t ox = a.x1 + digit_x(s, i), oy = a.y1;
        for (int k = 0; k < 7; k++) {
            bool on = s->mask[i] & (1 << k);
            if (!on) {
                segment_poly(seg[k][0], seg[k][1], t, 0, p, ox, oy, s->dh);
                draw_poly(ctx, p, off, LV_OPA_COVER);
                continue;
            }
            /* Segment nouvellement allumé : intensité modulée par le scintillement. */
            uint8_t level = (s->fresh[i] & (1 << k)) ? s->flicker[i] : 255;
            lv_color_t lit = lv_color_mix(s->color, off, level);
            segment_poly(seg[k][0], seg[k][1], t, 2.5f, p, ox, oy, s->dh); /* halo large */
            draw_poly(ctx, p, s->color, (lv_opa_t)(level * 14 / 255));
            segment_poly(seg[k][0], seg[k][1], t, 1.2f, p, ox, oy, s->dh); /* halo serré */
            draw_poly(ctx, p, s->color, (lv_opa_t)(level * 40 / 255));
            segment_poly(seg[k][0], seg[k][1], t, 0, p, ox, oy, s->dh);
            draw_poly(ctx, p, lit, LV_OPA_COVER);
            /* Cœur plus clair : filament qui chauffe. */
            segment_poly(seg[k][0], seg[k][1], t * 0.35f, 0, p, ox, oy, s->dh);
            draw_poly(ctx, p, lv_color_mix(lv_color_white(), lit, 70), (lv_opa_t)(level * 160 / 255));
        }
    }

    if (s->colon_after >= 0) {
        lv_coord_t cx = a.x1 + digit_x(s, s->colon_after) + s->dw + s->gap / 2 + s->colon_w / 2 - 2;
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.radius = LV_RADIUS_CIRCLE;
        d.bg_color = s->colon ? s->color : off;
        d.shadow_color = s->color;
        d.shadow_width = s->colon ? 6 : 0;
        d.shadow_opa = LV_OPA_50;
        lv_coord_t r = (lv_coord_t)(t * 0.45f) + 1;
        for (int k = 0; k < 2; k++) {
            lv_coord_t cy = a.y1 + (k ? s->dh * 7 / 10 : s->dh * 3 / 10);
            lv_coord_t sx = (lv_coord_t)((s->dh / 2 - (cy - a.y1)) * SLANT);
            lv_area_t dot = {cx + sx - r, cy - r, cx + sx + r, cy + r};
            lv_draw_rect(ctx, &d, &dot);
        }
    }
}

/* ----------------------------------------------------------- scintillement */

static void flicker_cb(void *var, int32_t v)
{
    seg7_t *s = var;
    /* Courbe d'allumage : deux sursauts puis pleine intensité (≈ 180 ms). */
    static const uint8_t curve[] = {40, 200, 70, 230, 150, 255};
    uint8_t level = curve[v * (sizeof curve - 1) / 1000];
    for (int i = 0; i < s->n; i++)
        if (s->fresh[i]) s->flicker[i] = level;
    lv_obj_invalidate(s->obj);
}

static void flicker_done(lv_anim_t *a)
{
    seg7_t *s = a->var;
    memset(s->fresh, 0, sizeof s->fresh);
    lv_obj_invalidate(s->obj);
}

/* Le halo déborde de quelques pixels de la boîte. */
static void ext_size_cb(lv_event_t *e) { lv_event_set_ext_draw_size(e, 4); }

static void delete_cb(lv_event_t *e)
{
    seg7_t *s = lv_event_get_user_data(e);
    lv_anim_del(s, NULL);
    lv_mem_free(s);
}

/* ------------------------------------------------------------------- API */

lv_obj_t *seg7_create(lv_obj_t *parent, int ndigits, lv_coord_t digit_w, lv_coord_t digit_h,
                      int colon_after, lv_color_t color)
{
    seg7_t *s = lv_mem_alloc(sizeof *s);
    memset(s, 0, sizeof *s);
    s->n = ndigits > MAX_DIGITS ? MAX_DIGITS : ndigits;
    s->dw = digit_w;
    s->dh = digit_h;
    s->gap = digit_w / 5;
    s->colon_after = colon_after;
    s->colon_w = colon_after >= 0 ? digit_w / 3 : 0;
    s->color = color;

    s->obj = lv_obj_create(parent);
    lv_obj_remove_style_all(s->obj);
    lv_obj_set_size(s->obj, digit_x(s, s->n - 1) + digit_w + 2, digit_h + 1);
    lv_obj_add_event_cb(s->obj, ext_size_cb, LV_EVENT_REFR_EXT_DRAW_SIZE, NULL);
    lv_obj_refresh_ext_draw_size(s->obj);
    lv_obj_clear_flag(s->obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s->obj, draw_cb, LV_EVENT_DRAW_MAIN, s);
    lv_obj_add_event_cb(s->obj, delete_cb, LV_EVENT_DELETE, s);
    lv_obj_set_user_data(s->obj, s);
    return s->obj;
}

void seg7_set_text(lv_obj_t *obj, const char *text)
{
    seg7_t *s = lv_obj_get_user_data(obj);
    bool any_new = false;
    for (int i = 0; i < s->n; i++) {
        char c = text[i];
        uint8_t m = (c >= '0' && c <= '9') ? GLYPHS[c - '0'] : c == '-' ? GLYPH_MINUS : 0;
        uint8_t newly_on = m & ~s->mask[i];
        if (m != s->mask[i]) {
            s->fresh[i] |= newly_on;
            s->mask[i] = m;
            any_new |= newly_on != 0;
        }
        if (!c) break;
    }
    lv_obj_invalidate(obj);
    if (any_new) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s);
        lv_anim_set_values(&a, 0, 1000);
        lv_anim_set_time(&a, 180);
        lv_anim_set_exec_cb(&a, flicker_cb);
        lv_anim_set_ready_cb(&a, flicker_done);
        lv_anim_start(&a);
    }
}

void seg7_set_colon(lv_obj_t *obj, bool on)
{
    seg7_t *s = lv_obj_get_user_data(obj);
    if (s->colon == on) return;
    s->colon = on;
    lv_obj_invalidate(obj);
}
