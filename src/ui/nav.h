/*
 * Navigation: faces ⇄ launcher (vertical swipe) ⇄ apps (tap / swipe right), plus the
 * "shell" that watches alarms and the timer to show the alert screen.
 */
#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    KG_ICON_CHRONO, KG_ICON_TIMER, KG_ICON_ALARM, KG_ICON_STEPS, KG_ICON_SETTINGS, KG_ICON_WEATHER,
    KG_ICON_NET, KG_ICON_REMOTE, KG_ICON_SYSTEM, KG_ICON_LEVEL, KG_ICON_WORLD, KG_ICON_SKY, KG_ICON_TORCH,
} kg_icon_t;

typedef struct {
    const char *name;    /* identifier (console: "app <name>") */
    const char *label;   /* displayed label */
    kg_icon_t icon;
    lv_obj_t *(*create)(void);
    void (*on_show)(void); /* optional: refresh when opened */
    void (*on_hide)(void); /* optional: stop LVGL timers */
} kg_app_t;

extern const kg_app_t kg_app_chrono, kg_app_timer, kg_app_alarms, kg_app_steps, kg_app_weather, kg_app_settings,
    kg_app_net, kg_app_remote, kg_app_system, kg_app_level, kg_app_world, kg_app_sky, kg_app_torch;

/* Opens a network tool directly ("ping", "wifi"…), with the toolbox already created. */
bool ui_net_open(const char *tool);

void ui_nav_init(void);         /* after ui_faces_start() */
void ui_nav_open_launcher(void);
void ui_nav_close_launcher(void); /* back to the face */
void ui_nav_launcher_page(int page); /* shows a launcher page (tests, screenshots) */
bool ui_nav_open_app(const char *name);
void ui_nav_back(void);         /* app → launcher */
bool ui_nav_on_face(void);

/* Tests (console "ring", simulator): shows the alert screen and rings. */
void ui_nav_test_alert(bool alarm);

/* For apps: swipe right = back (attach to the app's screen). */
void ui_nav_attach_back_gesture(lv_obj_t *scr);

/* Draws an app pictogram centred on (cx, cy), about 22 px (launcher, toolbox menu). */
void ui_nav_draw_icon(lv_draw_ctx_t *ctx, kg_icon_t icon, lv_coord_t cx, lv_coord_t cy);

#ifdef __cplusplus
}
#endif
