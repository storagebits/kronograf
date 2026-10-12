/* Notifications iPhone : carte à l'arrivée, liste, écran d'appairage (voir notif.c). */
#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_notif_init(void);
void ui_notif_poll(void);       /* appelé par le shell, écran allumé */
void ui_notif_open_list(void);  /* glissé vers le bas depuis un cadran */

#ifdef __cplusplus
}
#endif
