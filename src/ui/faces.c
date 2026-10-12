#include "faces.h"
#include "theme.h"
#include "../hal/hal.h"

static const kg_face_t *const FACES[] = {&kg_face_aviator, &kg_face_circuits, &kg_face_night};
#define N_FACES ((int)(sizeof FACES / sizeof FACES[0]))

static lv_obj_t *s_screens[N_FACES];
static int s_current = -1;

static void gesture_cb(lv_event_t *e);

static lv_obj_t *screen_of(int n)
{
    if (!s_screens[n]) {
        s_screens[n] = FACES[n]->create();
        lv_obj_add_event_cb(s_screens[n], gesture_cb, LV_EVENT_GESTURE, NULL);
        FACES[n]->set_active(false);
    }
    return s_screens[n];
}

/* ---------------------------------------------------- réglage mémorisé
 * Le cadran choisi n'est écrit en NVS qu'après 5 s d'affichage : pas d'écriture flash
 * pendant les transitions (cache coupé, rendu bloqué) ni d'usure à chaque glissé. */

static lv_timer_t *s_save_timer;

static void save_cb(lv_timer_t *t)
{
    hal_setting_set("face", s_current);
    lv_timer_pause(t);
}

static void schedule_save(void)
{
    if (!s_save_timer) s_save_timer = lv_timer_create(save_cb, 5000, NULL);
    lv_timer_reset(s_save_timer);
    lv_timer_resume(s_save_timer);
}

/* -------------------------------------------------- transition par instantanés
 * Recomposer deux cadrans complets à chaque image coûtait ~43 ms (≈ 23 fps). On fige
 * l'ancien et le nouveau cadran en deux images (une seule fois), puis on ne fait que
 * les déplacer : chaque image de transition est une copie, limitée par le SPI.
 *
 * On n'utilise pas lv_scr_load_anim : en LVGL 8.3.11, l'appeler pendant une transition
 * en cours déréférence NULL (scr_load_internal remet scr_to_load à NULL avant
 * lv_obj_set_pos). Les demandes reçues pendant une transition sont mises en attente. */

static lv_obj_t *s_trans_scr, *s_trans_old, *s_trans_new;
static lv_img_dsc_t s_snap_old, s_snap_new;
static uint8_t *s_buf_old, *s_buf_new;
static int s_trans_target = -1;
static int s_trans_dir;
static int s_pending = -1;
static bool s_pending_anim;
static lv_timer_t *s_pending_timer;

static bool snapshot(lv_obj_t *scr, uint8_t *buf, lv_img_dsc_t *dsc)
{
    uint32_t size = lv_snapshot_buf_size_needed(scr, LV_IMG_CF_TRUE_COLOR);
    return lv_snapshot_take_to_buf(scr, LV_IMG_CF_TRUE_COLOR, dsc, buf, size) == LV_RES_OK;
}

static void trans_exec(void *var, int32_t x)
{
    (void)var;
    lv_obj_set_x(s_trans_old, -s_trans_dir * x);
    lv_obj_set_x(s_trans_new, s_trans_dir * (KG_W - x));
}

static void trans_done(lv_anim_t *a)
{
    (void)a;
    lv_scr_load(s_screens[s_trans_target]);
    s_trans_target = -1;
}

static bool start_transition(int prev, int n, bool forward)
{
    size_t bytes = LV_CANVAS_BUF_SIZE_TRUE_COLOR(KG_W, KG_H);
    if (!s_buf_old) s_buf_old = hal_alloc_large(bytes);
    if (!s_buf_new) s_buf_new = hal_alloc_large(bytes);
    if (!s_buf_old || !s_buf_new) return false;
    if (!snapshot(s_screens[prev], s_buf_old, &s_snap_old) || !snapshot(s_screens[n], s_buf_new, &s_snap_new))
        return false;

    if (!s_trans_scr) {
        s_trans_scr = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(s_trans_scr, KG_C_BG, 0);
        lv_obj_clear_flag(s_trans_scr, LV_OBJ_FLAG_SCROLLABLE);
        s_trans_old = lv_img_create(s_trans_scr);
        s_trans_new = lv_img_create(s_trans_scr);
    }
    lv_img_set_src(s_trans_old, &s_snap_old);
    lv_img_set_src(s_trans_new, &s_snap_new);
    s_trans_dir = forward ? 1 : -1;
    s_trans_target = n;
    trans_exec(NULL, 0);
    lv_scr_load(s_trans_scr);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_trans_scr);
    lv_anim_set_values(&a, 0, KG_W);
    lv_anim_set_time(&a, KG_ANIM_FACE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, trans_exec);
    lv_anim_set_ready_cb(&a, trans_done);
    lv_anim_start(&a);
    return true;
}

static void pending_cb(lv_timer_t *t)
{
    if (s_pending < 0 || s_trans_target >= 0) return;
    int n = s_pending;
    s_pending = -1;
    lv_timer_pause(t);
    ui_faces_show(n, s_pending_anim);
}

void ui_faces_show(int n, bool animate)
{
    n = ((n % N_FACES) + N_FACES) % N_FACES;
    if (s_trans_target >= 0) {
        if (!s_pending_timer) s_pending_timer = lv_timer_create(pending_cb, 20, NULL);
        s_pending = n;
        s_pending_anim = animate;
        lv_timer_resume(s_pending_timer);
        return;
    }
    if (n == s_current) return;
    int prev = s_current;
    lv_obj_t *scr = screen_of(n);
    FACES[n]->set_active(true); /* contenu à jour avant l'instantané */
    if (prev >= 0) FACES[prev]->set_active(false);

    bool forward = prev >= 0 && (n - prev + N_FACES) % N_FACES == 1;
    if (!(animate && prev >= 0 && start_transition(prev, n, forward))) lv_scr_load(scr);
    s_current = n;
    hal_brightness_scale(FACES[n]->brightness_pct);
    schedule_save();
}

static void gesture_cb(lv_event_t *e)
{
    (void)e;
    const kg_face_t *f = FACES[s_current];
    if (f->swipe_ok && !f->swipe_ok()) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_LEFT) ui_faces_show(s_current + 1, true);
    else if (dir == LV_DIR_RIGHT) ui_faces_show(s_current - 1, true);
    else return;
    lv_indev_wait_release(lv_indev_get_act());
}

void ui_faces_start(void)
{
    ui_faces_show((int)hal_setting_get("face", 0), false);
}

int ui_faces_count(void) { return N_FACES; }
int ui_faces_current(void) { return s_current; }
const char *ui_faces_name(int n) { return FACES[n]->name; }
