/*
 * Cadrans et navigation : glissé horizontal entre cadrans, transition animée,
 * cadran par défaut mémorisé.
 */
#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    lv_obj_t *(*create)(void);
    void (*set_active)(bool on);  /* met en pause / relance les minuteurs du cadran */
    bool (*swipe_ok)(void);       /* NULL = toujours ; faux si le glissé appartient au cadran */
    uint8_t brightness_pct;       /* facteur de rétroéclairage demandé */
} kg_face_t;

extern const kg_face_t kg_face_aviator, kg_face_circuits, kg_face_night;

/* Charge le cadran par défaut (réglage « face »). */
void ui_faces_start(void);

/* Affiche le cadran n (0..ui_faces_count()-1), avec ou sans animation. */
void ui_faces_show(int n, bool animate);
int ui_faces_count(void);
int ui_faces_current(void);
const char *ui_faces_name(int n);

/* Format horaire (réglage « fmt24 », 24 h par défaut). Heure à afficher pour h ∈ 0..23. */
bool ui_fmt24(void);
void ui_set_fmt24(bool on);
int ui_display_hour(int h24);

/* Cadran par défaut : mémorisé et affiché tout de suite. */
void ui_faces_set_default(int n);

/* Écran du cadran n (créé au besoin). */
lv_obj_t *ui_faces_screen(int n);

/* Cadran courant caché (lanceur, app) : ses minuteurs sont en pause ; visible : relancés. */
void ui_faces_set_visible(bool visible);
#define ui_faces_refresh_current() ui_faces_set_visible(true)

#ifdef __cplusplus
}
#endif
