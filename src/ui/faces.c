#include "faces.h"
#include "theme.h"
#include "transition.h"
#include "../hal/hal.h"

static const kg_face_t *const FACES[] = {&kg_face_aviator, &kg_face_circuits, &kg_face_night};
#define N_FACES ((int)(sizeof FACES / sizeof FACES[0]))

static lv_obj_t *s_screens[N_FACES];
static int s_current = -1;
static int s_current_default_pending = -1; /* applied when returning to the faces */

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

/* ------------------------------------------------------------ saved setting
 * The chosen face is written to NVS only after 5 s on screen: no flash write during
 * transitions (cache disabled, rendering stalled) and no wear on every swipe. */

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

/* Face changes during a transition are queued (see transition.h). */
static int s_pending = -1;
static bool s_pending_anim;
static lv_timer_t *s_pending_timer;

static void pending_cb(lv_timer_t *t)
{
    if (s_pending < 0 || kg_transition_running()) return;
    int n = s_pending;
    s_pending = -1;
    lv_timer_pause(t);
    ui_faces_show(n, s_pending_anim);
}

void ui_faces_show(int n, bool animate)
{
    n = ((n % N_FACES) + N_FACES) % N_FACES;
    if (kg_transition_running()) {
        if (!s_pending_timer) s_pending_timer = lv_timer_create(pending_cb, 20, NULL);
        s_pending = n;
        s_pending_anim = animate;
        lv_timer_resume(s_pending_timer);
        return;
    }
    if (n == s_current) return;
    int prev = s_current;
    lv_obj_t *scr = screen_of(n);
    FACES[n]->set_active(true); /* content up to date before the snapshot */
    if (prev >= 0) FACES[prev]->set_active(false);

    bool forward = prev >= 0 && (n - prev + N_FACES) % N_FACES == 1;
    if (animate && prev >= 0) kg_transition(scr, forward ? KG_SLIDE_LEFT : KG_SLIDE_RIGHT);
    else lv_scr_load(scr);
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

lv_obj_t *ui_faces_screen(int n) { return screen_of(n); }

void ui_faces_set_visible(bool visible)
{
    if (visible && s_current_default_pending >= 0) { /* default face changed in Settings */
        int n = s_current_default_pending;
        s_current_default_pending = -1;
        if (n != s_current) {
            FACES[s_current]->set_active(false);
            screen_of(n);
            s_current = n;
        }
    }
    if (s_current >= 0) FACES[s_current]->set_active(visible);
    if (visible) hal_brightness_scale(FACES[s_current]->brightness_pct);
    else hal_brightness_scale(100); /* the Night face dims; apps do not */
}

static int s_fmt24 = -1;

bool ui_fmt24(void)
{
    if (s_fmt24 < 0) s_fmt24 = hal_setting_get("fmt24", 1) != 0;
    return s_fmt24;
}

void ui_set_fmt24(bool on)
{
    s_fmt24 = on;
    hal_setting_set("fmt24", on);
    for (int i = 0; i < N_FACES; i++) /* force the hours to redraw */
        if (s_screens[i] && i == s_current) {
            FACES[i]->set_active(false);
            FACES[i]->set_active(true);
        }
}

int ui_display_hour(int h24)
{
    if (ui_fmt24()) return h24;
    int h = h24 % 12;
    return h ? h : 12;
}

void ui_faces_set_default(int n)
{
    hal_setting_set("face", n);
    s_current_default_pending = n;
}
