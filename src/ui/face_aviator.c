/*
 * Cadran 1 — « Chronographe aviateur ».
 *
 * Fond (guillochage, chemin de fer, index, sous-cadrans, lunette) rendu une fois
 * dans un canevas 240×240 en PSRAM. Les aiguilles sont des lv_line anti-crénelées
 * dont l'objet épouse la boîte englobante : chaque pas de trotteuse n'invalide que
 * la zone balayée, jamais l'écran entier (SPI plafonné à 40 MHz, cf. DECISIONS D14).
 */
#include "ui.h"
#include "faces.h"
#include "model.h"
#include "theme.h"
#include "wx_icon.h"
#include "fonts/kg_fonts.h"
#include "../hal/hal.h"
#include <math.h>
#include <stdio.h>

#define CX 120
#define CY 120
#define R_OUT    119.5f  /* bord extérieur de la lunette */
#define R_BEZEL  104.0f  /* bord intérieur de la lunette */
#define R_TRACK   96.0f  /* bord intérieur du chemin de fer */
#define R_SUB     22.0f  /* rayon des sous-cadrans */

#define SWEEP_MS 125     /* 8 Hz : balayage mécanique de la trotteuse */

#define PI_F 3.14159265f
#define DEG(a) ((a) * PI_F / 180.0f)

typedef struct { float x, y; } pt_t;

static const pt_t SUB_BATT = {72, 120};    /* 9 h */
static const pt_t SUB_CHRONO = {168, 120}; /* 3 h */
static const pt_t SUB_STEPS = {120, 168};  /* 6 h */

#define STEPS_GOAL 10000

/* Une aiguille = une ou deux lv_line superposées (corps + insert luminescent). */
typedef struct {
    lv_obj_t *body, *lume;
    lv_point_t body_pts[2], lume_pts[2];
    pt_t pivot;
    float back, len, lume_from, lume_to;
} hand_t;

static lv_obj_t *s_scr, *s_canvas;
static lv_color_t *s_bg;
static hand_t s_hour, s_min, s_sec, s_batt, s_chrono, s_steps;
static lv_obj_t *s_counterweight;
static lv_obj_t *s_date, *s_weekday, *s_batt_pct, *s_hint, *s_wx_icon, *s_wx_temp, *s_notif;
static float s_bezel_deg;      /* rotation de la lunette */
static lv_coord_t s_drag_last;
static bool s_drag_on_bezel;   /* le geste en cours appartient à la lunette */
static lv_timer_t *s_timer;
static int s_last_mday = -1;

/* ---------------------------------------------------------------- fond */

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static inline lv_color_t mix(lv_color_t a, lv_color_t b, float t) /* t = part de b */
{
    return lv_color_mix(b, a, (uint8_t)(clampf(t, 0, 1) * 255));
}

/* Couverture anti-crénelée d'un bord de disque : 1 dedans, 0 dehors. */
static inline float inside(float r, float edge) { return clampf(edge - r + 0.5f, 0, 1); }

static float dist(pt_t a, float x, float y) { return sqrtf((x - a.x) * (x - a.x) + (y - a.y) * (y - a.y)); }

/* Couleur procédurale d'un pixel du cadran, lunette exclue. */
static lv_color_t dial_pixel(float x, float y)
{
    float dx = x - CX, dy = y - CY;
    float r = sqrtf(dx * dx + dy * dy);
    float th = atan2f(dy, dx);

    /* Lumière douce venant du haut-gauche. */
    float light = 0.5f + 0.5f * (-(dx + dy) / (1.41f * R_OUT));

    lv_color_t c;
    if (r < R_TRACK) {
        /* Rosace guillochée : anneaux concentriques ondulés par 12 lobes. */
        float v = cosf(r * 0.85f + 2.6f * sinf(th * 12.0f));
        float ridge = v * v * v * v;
        c = mix(KG_C_DIAL, KG_C_GUILLOCHE, 0.35f + 0.45f * ridge * (0.55f + 0.45f * light));
    } else {
        /* Chemin de fer : anneau plat légèrement plus clair. */
        c = mix(KG_C_DIAL, KG_C_GUILLOCHE, 0.55f + 0.2f * light);
    }

    /* Sous-cadrans creusés, fini azuré (sillons concentriques fins). */
    const pt_t *subs[3] = {&SUB_BATT, &SUB_CHRONO, &SUB_STEPS};
    for (int i = 0; i < 3; i++) {
        float d = dist(*subs[i], x, y);
        float in = inside(d, R_SUB);
        if (in > 0) {
            float groove = 0.5f + 0.5f * cosf(d * 2.4f);
            lv_color_t sc = mix(KG_C_SUBDIAL, KG_C_GUILLOCHE, 0.18f + 0.22f * groove);
            /* Ombre interne en haut-gauche : effet creusé. */
            float shade = clampf(((x - subs[i]->x) + (y - subs[i]->y)) / (2 * R_SUB), -1, 1);
            sc = mix(sc, KG_C_BG, 0.25f * (1 - shade) * clampf((d - R_SUB + 6) / 6, 0, 1));
            c = mix(c, sc, in);
            /* Liseré clair au bord. */
            float rim = clampf(1 - fabsf(d - R_SUB) / 0.9f, 0, 1);
            c = mix(c, KG_C_HAIRLINE, rim * 0.9f);
        }
    }
    return c;
}

static lv_color_t bezel_pixel(float x, float y)
{
    float dx = x - CX, dy = y - CY;
    float light = 0.5f + 0.5f * (-(dx + dy) / (1.41f * R_OUT));
    return mix(KG_C_BEZEL, KG_C_HAIRLINE, 0.15f + 0.35f * light);
}

static void render_pixels(float r_from, float r_to)
{
    for (int y = 0; y < KG_H; y++) {
        for (int x = 0; x < KG_W; x++) {
            float fx = x + 0.5f, fy = y + 0.5f;
            float r = sqrtf((fx - CX) * (fx - CX) + (fy - CY) * (fy - CY));
            if (r < r_from - 1 || r > r_to + 1) continue;
            lv_color_t c = KG_C_BG;
            float in_out = inside(r, R_OUT);
            if (in_out > 0) {
                lv_color_t inner = dial_pixel(fx, fy);
                lv_color_t bez = bezel_pixel(fx, fy);
                float in_dial = inside(r, R_BEZEL);
                c = mix(KG_C_BG, mix(bez, inner, in_dial), in_out);
                /* Filet sombre entre lunette et chemin de fer. */
                float gap = clampf(1 - fabsf(r - R_BEZEL) / 0.8f, 0, 1);
                c = mix(c, KG_C_BG, gap * 0.8f);
            }
            s_bg[y * KG_W + x] = c;
        }
    }
}

static void canvas_line(pt_t a, pt_t b, lv_coord_t w, lv_color_t color, lv_opa_t opa)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = color;
    d.width = w;
    d.opa = opa;
    d.round_start = d.round_end = 0;
    lv_point_t pts[2] = {{(lv_coord_t)lroundf(a.x), (lv_coord_t)lroundf(a.y)},
                         {(lv_coord_t)lroundf(b.x), (lv_coord_t)lroundf(b.y)}};
    lv_canvas_draw_line(s_canvas, pts, 2, &d);
}

static pt_t polar(pt_t c, float r, float deg) /* 0° = midi, sens horaire */
{
    return (pt_t){c.x + r * sinf(DEG(deg)), c.y - r * cosf(DEG(deg))};
}

static void canvas_text_at(pt_t center, const lv_font_t *font, lv_color_t color, const char *txt)
{
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.font = font;
    d.color = color;
    lv_point_t sz;
    lv_txt_get_size(&sz, txt, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_canvas_draw_text(s_canvas, (lv_coord_t)lroundf(center.x - sz.x / 2.0f),
                        (lv_coord_t)lroundf(center.y - sz.y / 2.0f), sz.x + 2, &d, txt);
}

static void canvas_ring(pt_t c, float r, lv_coord_t w, lv_color_t color, lv_opa_t opa,
                        int16_t a_from, int16_t a_to)
{
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.color = color;
    d.width = w;
    d.opa = opa;
    /* lv_canvas_draw_arc : 0° = 3 h ; on convertit depuis 0° = midi. */
    lv_canvas_draw_arc(s_canvas, (lv_coord_t)c.x, (lv_coord_t)c.y, (lv_coord_t)r,
                       (a_from + 270) % 360, (a_to + 270) % 360, &d);
}

/* Petit sous-cadran : graduations sur un secteur, repère chiffré en haut, nom sous le pivot. */
static void subdial(pt_t c, int ticks, float span_deg, int major_every, const char *top, const char *name)
{
    float start = -span_deg / 2;
    float step = span_deg / (span_deg >= 360 ? ticks : ticks - 1);
    for (int i = 0; i < ticks; i++) {
        float a = start + i * step;
        bool major = i % major_every == 0;
        canvas_line(polar(c, R_SUB - (major ? 5.0f : 3.0f), a), polar(c, R_SUB - 1.0f, a),
                    1, KG_C_TRACK, major ? LV_OPA_COVER : LV_OPA_70);
    }
    if (top) canvas_text_at(polar(c, R_SUB - 11, 0), &kg_font_digits_11, KG_C_TEXT_DIM, top);
    canvas_text_at(polar(c, 10, 180), &kg_font_digits_11, KG_C_TRACK, name);
}

static void render_dial_marks(void)
{
    /* Chemin de fer : 60 minutes, 5 minutes appuyées. */
    for (int m = 0; m < 60; m++) {
        float a = m * 6.0f;
        bool five = m % 5 == 0;
        canvas_line(polar((pt_t){CX, CY}, five ? R_TRACK - 1 : R_TRACK + 1, a),
                    polar((pt_t){CX, CY}, R_BEZEL - 2, a), five ? 2 : 1, KG_C_TRACK, LV_OPA_COVER);
    }

    /* Index bâton appliqués avec insert luminescent (sauf 12, 3, 6, 9). */
    for (int h = 1; h < 12; h++) {
        if (h % 3 == 0) continue;
        float a = h * 30.0f;
        pt_t a0 = polar((pt_t){CX, CY}, 76, a), a1 = polar((pt_t){CX, CY}, 92, a);
        canvas_line(a0, a1, 6, KG_C_HAND_EDGE, LV_OPA_COVER);
        canvas_line(a0, a1, 4, KG_C_HAND, LV_OPA_COVER);
        canvas_line(polar((pt_t){CX, CY}, 79, a), polar((pt_t){CX, CY}, 89, a), 2, KG_C_LUME, LV_OPA_COVER);
    }

    /* « 12 » appliqué, avec ombre portée. */
    canvas_text_at((pt_t){CX + 1, CY - 78 + 1}, &kg_font_digits_30, KG_C_BG, "12");
    canvas_text_at((pt_t){CX, CY - 78}, &kg_font_digits_30, KG_C_HAND, "12");

    /* Inscription. */
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.font = &kg_font_text_16;
    d.color = KG_C_TEXT_DIM;
    d.letter_space = 2;
    lv_point_t sz;
    lv_txt_get_size(&sz, "KRONOGRAF", d.font, d.letter_space, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_canvas_draw_text(s_canvas, CX - sz.x / 2, CY - 44, sz.x + 4, &d, "KRONOGRAF");

    /* 9 h — batterie : secteur de 300°, zone basse en orange. */
    subdial(SUB_BATT, 11, 300, 5, NULL, "BAT");
    canvas_ring(SUB_BATT, R_SUB - 2, 2, KG_C_SECONDS, LV_OPA_80, -150, -90);

    /* 3 h — compteur chrono 30 minutes. */
    subdial(SUB_CHRONO, 30, 360, 5, "30", "MIN");

    /* 6 h — pas du jour, objectif 10 k. */
    subdial(SUB_STEPS, 20, 360, 4, "10k", "PAS");

    /* Guichet date à 4 h 30 : cadre creusé. */
    pt_t dc = polar((pt_t){CX, CY}, 64, 135);
    lv_draw_rect_dsc_t r;
    lv_draw_rect_dsc_init(&r);
    r.bg_color = KG_C_DATE_BG;
    r.radius = 2;
    r.border_color = KG_C_HAND_EDGE;
    r.border_width = 2;
    lv_canvas_draw_rect(s_canvas, (lv_coord_t)dc.x - 13, (lv_coord_t)dc.y - 9, 26, 18, &r);
}

/* Lunette règle à calcul : échelle logarithmique 10–99, chiffres droits. */
static void render_bezel(void)
{
    render_pixels(R_BEZEL, R_OUT);
    const pt_t c = {CX, CY};
    for (int n = 10; n < 100; n++) {
        bool labeled = (n <= 20 && n % 2 == 0) || (n > 20 && n <= 50 && n % 5 == 0) || (n > 50 && n % 10 == 0);
        bool tick = n <= 30 || n % 5 == 0 || (n > 50 && n % 5 == 0);
        if (!tick) continue;
        float a = s_bezel_deg + 360.0f * log10f(n / 10.0f);
        /* Les chiffres servent eux-mêmes d'index ; seules les valeurs intermédiaires ont un trait. */
        if (!labeled)
            canvas_line(polar(c, R_BEZEL + 1.0f, a), polar(c, R_BEZEL + 3.5f, a), 1, KG_C_BEZEL_MARK, LV_OPA_COVER);
        else if (n == 10) /* index de référence : petit triangle orange pointant vers le centre */
            for (int k = 0; k < 3; k++)
                canvas_line(polar(c, R_BEZEL + 1.0f + k, a - (2 - k) * 0.9f), polar(c, R_BEZEL + 1.0f + k, a + (2 - k) * 0.9f),
                            1, KG_C_SECONDS, LV_OPA_COVER);
        if (labeled) {
            char buf[4];
            snprintf(buf, sizeof buf, "%d", n);
            /* Demi-étendue radiale de la boîte du texte selon l'angle : le chiffre
             * vient tangenter le bord extérieur sans toucher les graduations. */
            lv_point_t sz;
            lv_txt_get_size(&sz, buf, &kg_font_digits_11, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            float half = fabsf(sinf(DEG(a))) * sz.x / 2 + fabsf(cosf(DEG(a))) * 4.0f;
            canvas_text_at(polar(c, R_OUT - 1.5f - half, a), &kg_font_digits_11,
                           n == 10 ? KG_C_SECONDS : KG_C_BEZEL_MARK, buf);
        }
    }
}

/* -------------------------------------------------------------- aiguilles */

static lv_obj_t *make_line(lv_obj_t *parent, lv_coord_t w, lv_color_t color)
{
    lv_obj_t *l = lv_line_create(parent);
    lv_obj_set_style_line_width(l, w, 0);
    lv_obj_set_style_line_color(l, color, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

static void hand_init(hand_t *h, lv_obj_t *parent, pt_t pivot, float back, float len, lv_coord_t w,
                      lv_color_t color, float lume_from, float lume_to, lv_coord_t lume_w)
{
    h->pivot = pivot;
    h->back = back;
    h->len = len;
    h->lume_from = lume_from;
    h->lume_to = lume_to;
    h->body = make_line(parent, w, color);
    h->lume = lume_w ? make_line(parent, lume_w, KG_C_LUME) : NULL;
}

/* Place une lv_line sur la boîte englobante du segment, points relatifs. */
static void place_segment(lv_obj_t *line, lv_point_t *pts, pt_t a, pt_t b)
{
    lv_coord_t x0 = (lv_coord_t)floorf(fminf(a.x, b.x)), y0 = (lv_coord_t)floorf(fminf(a.y, b.y));
    pts[0] = (lv_point_t){(lv_coord_t)lroundf(a.x) - x0, (lv_coord_t)lroundf(a.y) - y0};
    pts[1] = (lv_point_t){(lv_coord_t)lroundf(b.x) - x0, (lv_coord_t)lroundf(b.y) - y0};
    lv_obj_set_pos(line, x0, y0);
    lv_line_set_points(line, pts, 2);
}

static void hand_set(hand_t *h, float deg)
{
    place_segment(h->body, h->body_pts, polar(h->pivot, -h->back, deg), polar(h->pivot, h->len, deg));
    if (h->lume)
        place_segment(h->lume, h->lume_pts, polar(h->pivot, h->lume_from, deg), polar(h->pivot, h->lume_to, deg));
}

static lv_obj_t *cap(lv_obj_t *parent, pt_t c, lv_coord_t d, lv_color_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, d, d);
    lv_obj_set_pos(o, (lv_coord_t)c.x - d / 2, (lv_coord_t)c.y - d / 2);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

/* ------------------------------------------------------------ mise à jour */

static const char *const WEEKDAYS[] = {"DIM", "LUN", "MAR", "MER", "JEU", "VEN", "SAM"};

static void update(lv_timer_t *t)
{
    (void)t;
    struct tm now;
    uint32_t ms;
    hal_now(&now, &ms);

    /* Trotteuse quantifiée à 8 Hz : micro-pas mécaniques, pas un mouvement continu. */
    float sec = now.tm_sec + (ms / SWEEP_MS) * (SWEEP_MS / 1000.0f);
    float min = now.tm_min + sec / 60.0f;
    float hour = (now.tm_hour % 12) + min / 60.0f;

    hand_set(&s_hour, hour * 30.0f);
    hand_set(&s_min, min * 6.0f);
    hand_set(&s_sec, sec * 6.0f);
    pt_t cw = polar((pt_t){CX, CY}, -16, sec * 6.0f);
    lv_obj_set_pos(s_counterweight, (lv_coord_t)lroundf(cw.x) - 4, (lv_coord_t)lroundf(cw.y) - 4);

    kg_battery_t b = hal_battery();
    int pct = b.percent < 0 ? 0 : b.percent;
    hand_set(&s_batt, -150.0f + 3.0f * pct);
    lv_label_set_text_fmt(s_batt_pct, "%d%%", pct);

    uint32_t steps = hal_steps_today();
    hand_set(&s_steps, 360.0f * (steps > STEPS_GOAL ? STEPS_GOAL : steps) / STEPS_GOAL);

    /* Compteur 30 minutes du chronomètre : un tour = 30 min. */
    hand_set(&s_chrono, (model_chrono_elapsed_ms() % (30 * 60000)) * 360.0f / (30 * 60000));

    const char *hint = hal_time_hint();
    if (hint) {
        lv_label_set_text(s_hint, hint);
        lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
    }

    /* Complication météo (coin bas gauche), relue toutes les 5 s depuis le cache. */
    static uint32_t n;
    if (n++ % 40 == 0) {
        kg_weather_t wx;
        if (hal_weather(&wx)) {
            int v = (int)lroundf(wx.temp);
            if (v < 0) lv_label_set_text_fmt(s_wx_temp, "\xE2\x88\x92%d°", -v);
            else lv_label_set_text_fmt(s_wx_temp, "%d°", v);
            kg_wx_icon_set(s_wx_icon, kg_weather_icon(wx.code, wx.day));
            lv_obj_clear_flag(s_wx_icon, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_wx_temp, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_wx_icon, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_wx_temp, LV_OBJ_FLAG_HIDDEN);
        }
    }
    /* Pastille des notifications iPhone (coin bas droit) : glisser vers le bas pour les lire. */
    if (n % 8 == 0) {
        int count = hal_notif_count();
        if (count) {
            lv_label_set_text_fmt(s_notif, "%d", count);
            lv_obj_clear_flag(s_notif, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_notif, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (now.tm_mday != s_last_mday) {
        s_last_mday = now.tm_mday;
        lv_label_set_text_fmt(s_date, "%d", now.tm_mday);
        lv_label_set_text(s_weekday, WEEKDAYS[now.tm_wday]);
    }
}

/* Glissé circulaire sur la lunette : on suit l'angle du doigt. */
static void bezel_drag(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    float dx = p.x - CX, dy = p.y - CY;
    lv_coord_t ang = (lv_coord_t)lroundf(atan2f(dx, -dy) * 180.0f / PI_F);
    if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
        /* Seul un appui commencé sur la lunette la fait tourner ; ailleurs, c'est un glissé de cadran. */
        s_drag_on_bezel = dx * dx + dy * dy >= (R_TRACK - 10) * (R_TRACK - 10);
        s_drag_last = ang;
        return;
    }
    if (!s_drag_on_bezel) return;
    int delta = ang - s_drag_last;
    if (delta > 180) delta -= 360;
    if (delta < -180) delta += 360;
    if (delta == 0) return;
    s_drag_last = ang;
    s_bezel_deg = fmodf(s_bezel_deg + delta + 360.0f, 360.0f);
    render_bezel();
    lv_obj_invalidate(s_canvas);
}

static bool swipe_ok(void) { return !s_drag_on_bezel; }

static void set_active(bool on)
{
    if (on) {
        s_last_mday = -1;
        lv_timer_resume(s_timer);
        update(NULL);
    } else {
        lv_timer_pause(s_timer);
    }
}

static lv_obj_t *create(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, KG_C_BG, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    s_bg = hal_alloc_large(LV_CANVAS_BUF_SIZE_TRUE_COLOR(KG_W, KG_H));
    s_canvas = lv_canvas_create(s_scr);
    lv_canvas_set_buffer(s_canvas, s_bg, KG_W, KG_H, LV_IMG_CF_TRUE_COLOR);
    render_pixels(0, 2 * R_OUT); /* toute l'image, coins compris : la PSRAM n'est pas initialisée */
    render_dial_marks();
    render_bezel();
    lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_canvas, bezel_drag, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_canvas, bezel_drag, LV_EVENT_PRESSING, NULL);

    /* Guichet date et coins. */
    pt_t dc = polar((pt_t){CX, CY}, 64, 135);
    s_date = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_date, &kg_font_text_16, 0);
    lv_obj_set_style_text_color(s_date, KG_C_DATE_FG, 0);
    lv_obj_set_style_text_align(s_date, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_date, 26);
    lv_obj_set_pos(s_date, (lv_coord_t)dc.x - 13, (lv_coord_t)dc.y - 9);

    s_weekday = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_weekday, &kg_font_text_16, 0);
    lv_obj_set_style_text_color(s_weekday, KG_C_TEXT_DIM, 0);
    lv_obj_set_style_text_letter_space(s_weekday, 1, 0);
    lv_obj_set_pos(s_weekday, 4, 2);

    s_batt_pct = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_batt_pct, &kg_font_text_16, 0);
    lv_obj_set_style_text_color(s_batt_pct, KG_C_TEXT_DIM, 0);
    lv_obj_align(s_batt_pct, LV_ALIGN_TOP_RIGHT, -4, 2);

    s_wx_icon = kg_wx_icon_create(s_scr, 18);
    lv_obj_set_pos(s_wx_icon, 3, 219);
    lv_obj_add_flag(s_wx_icon, LV_OBJ_FLAG_HIDDEN);
    s_wx_temp = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_wx_temp, &kg_font_text_16, 0);
    lv_obj_set_style_text_color(s_wx_temp, KG_C_TEXT_DIM, 0);
    lv_obj_set_pos(s_wx_temp, 23, 220);
    lv_obj_add_flag(s_wx_temp, LV_OBJ_FLAG_HIDDEN);

    s_notif = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_notif, &kg_font_text_16, 0);
    lv_obj_set_style_text_color(s_notif, KG_C_BG, 0);
    lv_obj_set_style_bg_color(s_notif, KG_C_ACCENT, 0);
    lv_obj_set_style_bg_opa(s_notif, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_notif, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_text_align(s_notif, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(s_notif, 20, 20);
    lv_obj_align(s_notif, LV_ALIGN_BOTTOM_RIGHT, -4, -2);
    lv_obj_add_flag(s_notif, LV_OBJ_FLAG_HIDDEN);

    /* Petites aiguilles des sous-cadrans, puis leurs chapeaux. */
    hand_init(&s_batt, s_scr, SUB_BATT, 4, 17, 2, KG_C_HAND, 0, 0, 0);
    hand_init(&s_chrono, s_scr, SUB_CHRONO, 4, 17, 2, KG_C_SECONDS, 0, 0, 0);
    hand_init(&s_steps, s_scr, SUB_STEPS, 4, 17, 2, KG_C_HAND, 0, 0, 0);
    cap(s_scr, SUB_BATT, 5, KG_C_HAND);
    cap(s_scr, SUB_CHRONO, 5, KG_C_SECONDS);
    cap(s_scr, SUB_STEPS, 5, KG_C_HAND);

    /* Aiguilles centrales : bâtons à insert luminescent, trotteuse à contrepoids. */
    hand_init(&s_hour, s_scr, (pt_t){CX, CY}, 10, 54, 7, KG_C_HAND, 20, 50, 3);
    hand_init(&s_min, s_scr, (pt_t){CX, CY}, 12, 84, 5, KG_C_HAND, 26, 80, 2);
    hand_init(&s_sec, s_scr, (pt_t){CX, CY}, 22, 96, 2, KG_C_SECONDS, 0, 0, 0);
    s_counterweight = cap(s_scr, (pt_t){CX, CY}, 8, KG_C_SECONDS);
    cap(s_scr, (pt_t){CX, CY}, 9, KG_C_SECONDS);
    cap(s_scr, (pt_t){CX, CY}, 3, KG_C_HAND_EDGE);

    /* Heure non réglée : pastille ambre en bas, au-dessus de tout. */
    s_hint = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_hint, &kg_font_text_16, 0);
    lv_obj_set_style_text_color(s_hint, KG_C_BG, 0);
    lv_obj_set_style_bg_color(s_hint, KG_C_WARN, 0);
    lv_obj_set_style_bg_opa(s_hint, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_hint, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(s_hint, 8, 0);
    lv_obj_set_style_pad_ver(s_hint, 1, 0);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);

    s_timer = lv_timer_create(update, SWEEP_MS, NULL);
    update(NULL);
    return s_scr;
}

const kg_face_t kg_face_aviator = {
    .name = "aviateur",
    .create = create,
    .set_active = set_active,
    .swipe_ok = swipe_ok,
    .brightness_pct = 100,
};
