#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Écran de diagnostic de la Tranche 0 : révision détectée et inventaire matériel. */
lv_obj_t *ui_diag_create(void);

/* Rafraîchit les valeurs vivantes (heure, batterie). À appeler ~1 Hz. */
void ui_diag_update(void);

/* Cadran 1 — chronographe aviateur (se met à jour seul via un lv_timer). */
lv_obj_t *ui_face_aviator_create(void);

#ifdef __cplusplus
}
#endif
