/*
 * Watch faces: horizontal swipe between faces, animated transition, remembered
 * default face.
 */
#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    lv_obj_t *(*create)(void);
    void (*set_active)(bool on);  /* pauses / resumes the face's timers */
    bool (*swipe_ok)(void);       /* NULL = always; false when the face handles the swipe itself */
    uint8_t brightness_pct;       /* requested backlight scale */
} kg_face_t;

extern const kg_face_t kg_face_aviator, kg_face_circuits, kg_face_night;

/* Loads the default face ("face" setting). */
void ui_faces_start(void);

/* Shows face n (0..ui_faces_count()-1), with or without animation. */
void ui_faces_show(int n, bool animate);
int ui_faces_count(void);
int ui_faces_current(void);
const char *ui_faces_name(int n);

/* Clock format ("fmt24" setting, 24 h by default). Hour to display for h in 0..23. */
bool ui_fmt24(void);
void ui_set_fmt24(bool on);
int ui_display_hour(int h24);

/* Default face: saved and shown immediately. */
void ui_faces_set_default(int n);

/* Screen of face n (created on demand). */
lv_obj_t *ui_faces_screen(int n);

/* Current face hidden (launcher, app): its timers pause; visible again: they resume. */
void ui_faces_set_visible(bool visible);
#define ui_faces_refresh_current() ui_faces_set_visible(true)

#ifdef __cplusplus
}
#endif
