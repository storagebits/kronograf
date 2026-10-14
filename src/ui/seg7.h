/*
 * 7-segment display drawn in code (anti-aliased polygons): unlit segments stay faintly
 * visible, lit segments get a glow, and a segment flickers briefly when it lights up.
 */
#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Creates a group of `ndigits` digits, each digit_w × digit_h.
 * colon_after: index of the digit followed by a ':' (-1 for none). */
lv_obj_t *seg7_create(lv_obj_t *parent, int ndigits, lv_coord_t digit_w, lv_coord_t digit_h,
                      int colon_after, lv_color_t color);

/* Text: digits 0-9, '-' and ' ' (space = all segments off). Length = ndigits. */
void seg7_set_text(lv_obj_t *seg, const char *text);

/* Colon on or off (blinks every second). */
void seg7_set_colon(lv_obj_t *seg, bool on);

#ifdef __cplusplus
}
#endif
