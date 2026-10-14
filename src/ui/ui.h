#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Stage 0 diagnostic screen: detected board revision and hardware inventory. */
lv_obj_t *ui_diag_create(void);

/* Refreshes live values (time, battery). Call at ~1 Hz. */
void ui_diag_update(void);

/* Time circuits face easter egg (console, tests). */
void face_circuits_discharge(void);

#ifdef __cplusplus
}
#endif
