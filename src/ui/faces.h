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

#ifdef __cplusplus
}
#endif
