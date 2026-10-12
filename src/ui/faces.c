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

void ui_faces_show(int n, bool animate)
{
    n = ((n % N_FACES) + N_FACES) % N_FACES;
    if (n == s_current) return;
    int prev = s_current;
    lv_obj_t *scr = screen_of(n);
    FACES[n]->set_active(true);
    if (prev >= 0) FACES[prev]->set_active(false);

    if (animate && prev >= 0) {
        /* Sens du glissé : vers la gauche pour le cadran suivant. */
        bool forward = (n - prev + N_FACES) % N_FACES == 1;
        lv_scr_load_anim(scr, forward ? LV_SCR_LOAD_ANIM_MOVE_LEFT : LV_SCR_LOAD_ANIM_MOVE_RIGHT,
                         KG_ANIM_FACE_MS, 0, false);
    } else {
        lv_scr_load(scr);
    }
    s_current = n;
    hal_brightness_scale(FACES[n]->brightness_pct);
    hal_setting_set("face", n);
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
