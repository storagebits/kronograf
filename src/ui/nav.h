/*
 * Navigation : cadrans ⇄ lanceur (glissé vertical) ⇄ apps (toucher / glissé à droite),
 * et « shell » qui surveille alarmes et minuteur pour afficher l'écran d'alerte.
 */
#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { KG_ICON_CHRONO, KG_ICON_TIMER, KG_ICON_ALARM, KG_ICON_STEPS, KG_ICON_SETTINGS, KG_ICON_WEATHER } kg_icon_t;

typedef struct {
    const char *name;    /* identifiant (console : « app <name> ») */
    const char *label;   /* libellé affiché, en français */
    kg_icon_t icon;
    lv_obj_t *(*create)(void);
    void (*on_show)(void); /* facultatif : rafraîchir à l'ouverture */
    void (*on_hide)(void); /* facultatif : arrêter les minuteurs LVGL */
} kg_app_t;

extern const kg_app_t kg_app_chrono, kg_app_timer, kg_app_alarms, kg_app_steps, kg_app_settings;

void ui_nav_init(void);         /* après ui_faces_start() */
void ui_nav_open_launcher(void);
void ui_nav_close_launcher(void); /* retour au cadran */
bool ui_nav_open_app(const char *name);
void ui_nav_back(void);         /* app → lanceur */
bool ui_nav_on_face(void);

/* Tests (console « ring », simulateur) : affiche l'écran d'alerte et fait sonner. */
void ui_nav_test_alert(bool alarm);

/* Appelé par une app : glissé vers la droite = retour (à brancher sur l'écran de l'app). */
void ui_nav_attach_back_gesture(lv_obj_t *scr);

#ifdef __cplusplus
}
#endif
