/*
 * Snapshot transition between two screens: both screens are frozen into images (PSRAM)
 * and slid. One image per frame = one copy, bounded by SPI (~40 fps).
 * One transition at a time; see DECISIONS D22.
 */
#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { KG_SLIDE_LEFT, KG_SLIDE_RIGHT, KG_SLIDE_UP, KG_SLIDE_DOWN } kg_slide_t;

/* Slides from the active screen to `to` (the new screen enters from the side opposite
 * `dir`). Returns false if a transition is already running (nothing done). Without
 * memory for the snapshots, loads `to` directly. */
bool kg_transition(lv_obj_t *to, kg_slide_t dir);

bool kg_transition_running(void);

#ifdef __cplusplus
}
#endif
