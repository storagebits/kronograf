/* iPhone notifications: card on arrival, list, pairing screen (see notif.c). */
#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_notif_init(void);
void ui_notif_poll(void);       /* called by the shell while the screen is on */
void ui_notif_open_list(void);  /* swipe down from a face */

#ifdef __cplusplus
}
#endif
