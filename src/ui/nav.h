/*
 * Navigation: faces ⇄ launcher (vertical swipe) ⇄ apps (tap / swipe right), plus the
 * "shell" that watches alarms and the timer to show the alert screen.
 */
#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { KG_ICON_CHRONO, KG_ICON_TIMER, KG_ICON_ALARM, KG_ICON_STEPS, KG_ICON_SETTINGS, KG_ICON_WEATHER } kg_icon_t;

typedef struct {
    const char *name;    /* identifier (console: "app <name>") */
    const char *label;   /* displayed label */
    kg_icon_t icon;
    lv_obj_t *(*create)(void);
    void (*on_show)(void); /* optional: refresh when opened */
    void (*on_hide)(void); /* optional: stop LVGL timers */
} kg_app_t;

extern const kg_app_t kg_app_chrono, kg_app_timer, kg_app_alarms, kg_app_steps, kg_app_weather, kg_app_settings;

void ui_nav_init(void);         /* after ui_faces_start() */
void ui_nav_open_launcher(void);
void ui_nav_close_launcher(void); /* back to the face */
bool ui_nav_open_app(const char *name);
void ui_nav_back(void);         /* app → launcher */
bool ui_nav_on_face(void);

/* Tests (console "ring", simulator): shows the alert screen and rings. */
void ui_nav_test_alert(bool alarm);

/* For apps: swipe right = back (attach to the app's screen). */
void ui_nav_attach_back_gesture(lv_obj_t *scr);

#ifdef __cplusplus
}
#endif
