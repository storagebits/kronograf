#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Écran de diagnostic de la Tranche 0 : révision détectée et inventaire matériel. */
lv_obj_t *ui_diag_create(void);

/* Rafraîchit les valeurs vivantes (heure, batterie). À appeler ~1 Hz. */
void ui_diag_update(void);

/* Easter egg du cadran Circuits temporels (console, tests). */
void face_circuits_discharge(void);

#ifdef __cplusplus
}
#endif
