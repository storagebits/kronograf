/* Shared app components: one style everywhere, colours from theme.h. */
#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Black, non-scrolling screen. */
lv_obj_t *kit_screen(void);

/* Stencil title at the top of the screen. */
lv_obj_t *kit_title(lv_obj_t *scr, const char *text);

/* Rounded text button; `primary` = main action (orange). */
lv_obj_t *kit_button(lv_obj_t *parent, const char *text, bool primary, lv_coord_t w, lv_coord_t h,
                     lv_event_cb_t cb, void *user);
void kit_button_set_text(lv_obj_t *btn, const char *text);

/* Plain label. */
lv_obj_t *kit_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text);

/* Short haptic tick shared by every button. */
void kit_tick(void);

#ifdef __cplusplus
}
#endif
