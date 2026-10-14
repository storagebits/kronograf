#include "transition.h"
#include "theme.h"
#include "../hal/hal.h"

static lv_obj_t *s_scr, *s_old, *s_new, *s_target;
static lv_img_dsc_t s_snap_old, s_snap_new;
static uint8_t *s_buf_old, *s_buf_new;
static kg_slide_t s_dir;

static bool snapshot(lv_obj_t *scr, uint8_t *buf, lv_img_dsc_t *dsc)
{
    uint32_t size = lv_snapshot_buf_size_needed(scr, LV_IMG_CF_TRUE_COLOR);
    return lv_snapshot_take_to_buf(scr, LV_IMG_CF_TRUE_COLOR, dsc, buf, size) == LV_RES_OK;
}

static void exec_cb(void *var, int32_t p) /* p: 0..1000 */
{
    (void)var;
    bool horiz = s_dir == KG_SLIDE_LEFT || s_dir == KG_SLIDE_RIGHT;
    int sign = (s_dir == KG_SLIDE_LEFT || s_dir == KG_SLIDE_UP) ? 1 : -1;
    lv_coord_t span = horiz ? KG_W : KG_H;
    lv_coord_t d = (lv_coord_t)(span * p / 1000);
    if (horiz) {
        lv_obj_set_pos(s_old, -sign * d, 0);
        lv_obj_set_pos(s_new, sign * (span - d), 0);
    } else {
        lv_obj_set_pos(s_old, 0, -sign * d);
        lv_obj_set_pos(s_new, 0, sign * (span - d));
    }
}

static void done_cb(lv_anim_t *a)
{
    (void)a;
    lv_obj_t *t = s_target;
    s_target = NULL;
    lv_scr_load(t);
}

bool kg_transition_running(void) { return s_target != NULL; }

bool kg_transition(lv_obj_t *to, kg_slide_t dir)
{
    if (s_target) return false;
    lv_obj_t *from = lv_scr_act();
    size_t bytes = LV_CANVAS_BUF_SIZE_TRUE_COLOR(KG_W, KG_H);
    if (!s_buf_old) s_buf_old = hal_alloc_large(bytes);
    if (!s_buf_new) s_buf_new = hal_alloc_large(bytes);
    if (from == to || !s_buf_old || !s_buf_new || !snapshot(from, s_buf_old, &s_snap_old) ||
        !snapshot(to, s_buf_new, &s_snap_new)) {
        lv_scr_load(to);
        return true;
    }
    if (!s_scr) {
        s_scr = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(s_scr, KG_C_BG, 0);
        lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
        s_old = lv_img_create(s_scr);
        s_new = lv_img_create(s_scr);
    }
    lv_img_set_src(s_old, &s_snap_old);
    lv_img_set_src(s_new, &s_snap_new);
    s_dir = dir;
    s_target = to;
    exec_cb(NULL, 0);
    lv_scr_load(s_scr);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_scr);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_time(&a, KG_ANIM_FACE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, exec_cb);
    lv_anim_set_ready_cb(&a, done_cb);
    lv_anim_start(&a);
    return true;
}
