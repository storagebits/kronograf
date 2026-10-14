/*
 * Network toolbox internals shared by app_net.c (menu, target picker, list screens) and
 * net_screens.c (Wi-Fi analyser, link, ping, speed test, subnet calculator).
 */
#pragma once
#include "lvgl.h"
#include "../hal/hal.h"

typedef struct {
    const char *name;            /* menu title */
    const char *desc;            /* menu subtitle */
    lv_obj_t *(*create)(void);
    void (*show)(void);          /* each time the screen opens */
    void (*refresh)(const kg_tool_state_t *st); /* 5 Hz while on screen */
    bool offline;                /* no Wi-Fi lease needed */
} net_tool_t;

/* Screen with the toolbox title and the swipe-right gesture back to the menu. */
lv_obj_t *net_screen(const char *title);
/* Start / Stop button at the bottom; its label follows the tool state. */
lv_obj_t *net_run_button(lv_obj_t *scr, lv_event_cb_t cb);
void net_run_button_set(lv_obj_t *btn, bool running);

/* Current target (host name or address), shared by ping, traceroute, DNS and ports. */
const char *net_target(void);
/* Target chip: shows the target, tap → picker; `changed` runs after a new choice. */
lv_obj_t *net_target_chip(lv_obj_t *scr, void (*changed)(void));
void net_target_chip_update(lv_obj_t *chip);

/* Latest tool snapshot (copied at 5 Hz by the app). */
const kg_tool_state_t *net_state(void);

/* Row colour for KG_ROW_*. */
lv_color_t net_row_color(uint8_t kind);

extern const net_tool_t NET_WIFI, NET_LINK, NET_PING, NET_SPEED, NET_SUBNET;
