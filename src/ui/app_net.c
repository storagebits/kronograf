/*
 * Network toolbox: a menu of ten tools for whoever looks after a network. While the
 * toolbox is open the watch holds Wi-Fi on (a lease renewed at 5 Hz, which lapses on its
 * own when the screen goes off); everything stops when it closes.
 *   Wi-Fi analyser · link details and public IP · ping · traceroute · DNS lookup ·
 *   port check · LAN scan (ARP) · speed test · Bluetooth scan · subnet calculator
 * This file: menu, target picker and the list-style tools; net_screens.c: the others.
 */
#include "nav.h"
#include "kit.h"
#include "net_ui.h"
#include "theme.h"
#include "transition.h"
#include "fonts/kg_fonts.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *s_menu, *s_link;
static lv_timer_t *s_poll;
static const net_tool_t *s_active; /* tool on screen, NULL on the menu */
static lv_obj_t *s_tool_scr[16];
static kg_tool_state_t *s_st; /* latest snapshot, in PSRAM on the watch */
static char s_target[48] = "1.1.1.1";

const kg_tool_state_t *net_state(void) { return s_st; }
const char *net_target(void) { return s_target; }

lv_color_t net_row_color(uint8_t kind)
{
    switch (kind) {
    case KG_ROW_GOOD: return KG_C_OK;
    case KG_ROW_WARN: return KG_C_WARN;
    case KG_ROW_DIM: return KG_C_TEXT_DIM;
    default: return KG_C_TEXT;
    }
}

/* ------------------------------------------------------------ framework */

static void back_to_menu(void)
{
    hal_tool_stop();
    s_active = NULL;
    kg_transition(s_menu, KG_SLIDE_RIGHT);
}

static void tool_gesture(lv_event_t *e)
{
    (void)e;
    if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());
        back_to_menu();
    }
}

lv_obj_t *net_screen(const char *title)
{
    lv_obj_t *scr = kit_screen();
    kit_title(scr, title);
    lv_obj_add_event_cb(scr, tool_gesture, LV_EVENT_GESTURE, NULL);
    return scr;
}

lv_obj_t *net_run_button(lv_obj_t *scr, lv_event_cb_t cb)
{
    lv_obj_t *b = kit_button(scr, "Start", true, 120, 34, cb, NULL);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -6);
    return b;
}

void net_run_button_set(lv_obj_t *btn, bool running)
{
    kit_button_set_text(btn, running ? "Stop" : "Start");
    lv_obj_set_style_bg_color(btn, running ? KG_C_BTN : KG_C_ACCENT, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(btn, 0), running ? KG_C_TEXT : KG_C_BG, 0);
}

static void poll(lv_timer_t *t)
{
    (void)t;
    if (!s_active || !s_active->offline) hal_net_hold();
    hal_tool_get(s_st);
    if (s_active && s_active->refresh) s_active->refresh(s_st);
    if (!s_active) {
        kg_link_t l;
        hal_net_link(&l);
        char ip[16];
        if (l.up) {
            lv_label_set_text_fmt(s_link, "%s · %d dBm · %s", l.ssid, l.rssi, kg_ip_str(l.ip, ip));
            lv_obj_set_style_text_color(s_link, KG_C_OK, 0);
        } else {
            const char *ssid = hal_wifi_ssid();
            if (ssid[0]) lv_label_set_text_fmt(s_link, "Connecting to %s…", ssid);
            else lv_label_set_text(s_link, "No Wi-Fi network (Settings)");
            lv_obj_set_style_text_color(s_link, KG_C_WARN, 0);
        }
    }
}

static void open_tool(const net_tool_t *t, int slot)
{
    if (!s_tool_scr[slot]) s_tool_scr[slot] = t->create();
    s_active = t;
    if (t->show) t->show();
    hal_tool_get(s_st);
    if (t->refresh) t->refresh(s_st);
    kg_transition(s_tool_scr[slot], KG_SLIDE_LEFT);
}

/* -------------------------------------------------------- target picker */

static lv_obj_t *s_pick_scr, *s_pick_kb_panel, *s_pick_ta, *s_pick_return;
static void (*s_pick_changed)(void);
static lv_obj_t *s_pick_chip;

static void picked(const char *target)
{
    snprintf(s_target, sizeof s_target, "%s", target);
    if (s_pick_chip) net_target_chip_update(s_pick_chip);
    kg_transition(s_pick_return, KG_SLIDE_RIGHT);
    if (s_pick_changed) s_pick_changed();
}

static void preset_cb(lv_event_t *e)
{
    const char *t = lv_event_get_user_data(e);
    if (t) picked(t);
    else { /* custom */
        lv_textarea_set_text(s_pick_ta, "");
        lv_obj_clear_flag(s_pick_kb_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

static void pick_kb_cb(lv_event_t *e)
{
    lv_obj_add_flag(s_pick_kb_panel, LV_OBJ_FLAG_HIDDEN);
    if (lv_event_get_code(e) == LV_EVENT_READY && lv_textarea_get_text(s_pick_ta)[0]) picked(lv_textarea_get_text(s_pick_ta));
}

static void pick_gesture(lv_event_t *e)
{
    (void)e;
    if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());
        kg_transition(s_pick_return, KG_SLIDE_RIGHT);
    }
}

static lv_obj_t *pick_screen(void)
{
    lv_obj_t *scr = kit_screen();
    lv_obj_t *list = lv_obj_create(scr);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, KG_W, KG_H);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(list, 5, 0);
    lv_obj_set_style_pad_ver(list, 6, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    kit_title(list, "TARGET");
    lv_obj_set_align(lv_obj_get_child(list, 0), LV_ALIGN_DEFAULT);
    static const char *const P[][2] = {
        {"gateway", "Default gateway"}, {"1.1.1.1", "Cloudflare DNS"}, {"8.8.8.8", "Google DNS"},
        {"9.9.9.9", "Quad9 DNS"}, {"google.com", "Web"}, {NULL, "Type a host or address"},
    };
    for (size_t i = 0; i < sizeof P / sizeof P[0]; i++) {
        lv_obj_t *b = kit_button(list, "", false, 224, 40, preset_cb, (void *)P[i][0]);
        lv_obj_set_style_radius(b, 10, 0);
        lv_obj_t *l = lv_obj_get_child(b, 0);
        lv_label_set_text(l, P[i][0] ? P[i][0] : "Custom…");
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 12, 0);
        lv_obj_t *d = kit_label(b, &kg_font_text_16, KG_C_TEXT_DIM, P[i][1]);
        lv_obj_align(d, LV_ALIGN_RIGHT_MID, -12, 0);
    }

    s_pick_kb_panel = lv_obj_create(scr);
    lv_obj_remove_style_all(s_pick_kb_panel);
    lv_obj_set_size(s_pick_kb_panel, KG_W, KG_H);
    lv_obj_set_style_bg_color(s_pick_kb_panel, KG_C_BG, 0);
    lv_obj_set_style_bg_opa(s_pick_kb_panel, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_pick_kb_panel, LV_OBJ_FLAG_HIDDEN);
    s_pick_ta = lv_textarea_create(s_pick_kb_panel);
    lv_textarea_set_one_line(s_pick_ta, true);
    lv_textarea_set_max_length(s_pick_ta, sizeof s_target - 1);
    lv_textarea_set_placeholder_text(s_pick_ta, "host or address");
    lv_obj_set_size(s_pick_ta, 228, 38);
    lv_obj_align(s_pick_ta, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_set_style_text_font(s_pick_ta, &kg_font_label_20, 0);
    lv_obj_set_style_bg_color(s_pick_ta, KG_C_SURFACE, 0);
    lv_obj_set_style_text_color(s_pick_ta, KG_C_TEXT, 0);
    lv_obj_set_style_border_color(s_pick_ta, KG_C_ACCENT, 0);
    lv_obj_t *kb = lv_keyboard_create(s_pick_kb_panel);
    lv_obj_set_size(kb, KG_W, 172);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, s_pick_ta);
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(kb, KG_C_BG, 0);
    lv_obj_set_style_pad_all(kb, 2, 0);
    lv_obj_set_style_pad_gap(kb, 2, 0);
    lv_obj_set_style_bg_color(kb, KG_C_BTN, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, KG_C_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
    lv_obj_add_event_cb(kb, pick_kb_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(kb, pick_kb_cb, LV_EVENT_CANCEL, NULL);
    lv_obj_add_event_cb(scr, pick_gesture, LV_EVENT_GESTURE, NULL);
    return scr;
}

static void chip_cb(lv_event_t *e)
{
    lv_obj_t *chip = lv_event_get_target(e);
    if (!s_pick_scr) s_pick_scr = pick_screen();
    lv_obj_add_flag(s_pick_kb_panel, LV_OBJ_FLAG_HIDDEN);
    s_pick_return = lv_obj_get_screen(chip);
    s_pick_chip = chip;
    s_pick_changed = (void (*)(void))lv_event_get_user_data(e);
    hal_tool_stop();
    kg_transition(s_pick_scr, KG_SLIDE_LEFT);
}

lv_obj_t *net_target_chip(lv_obj_t *scr, void (*changed)(void))
{
    lv_obj_t *b = kit_button(scr, "", false, 200, 28, chip_cb, (void *)changed);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 22);
    lv_obj_set_style_text_font(lv_obj_get_child(b, 0), &kg_font_text_16, 0);
    net_target_chip_update(b);
    return b;
}

void net_target_chip_update(lv_obj_t *chip)
{
    lv_label_set_text_fmt(lv_obj_get_child(chip, 0), "%s  >", s_target);
}

/* ------------------------------------------------------ list-style tools */

typedef struct {
    kg_tool_t tool;
    const char *title;
    bool target;              /* uses the shared target */
    const char *idle;         /* status before the first run */
    lv_obj_t *scr, *chip, *status, *list, *run, *rows[KG_TOOL_ROWS], *ra[KG_TOOL_ROWS], *rb[KG_TOOL_ROWS];
    uint16_t shown;
    uint32_t seq;
} list_tool_t;

static list_tool_t L_TRACE = {.tool = KG_TOOL_TRACE, .title = "TRACEROUTE", .target = true, .idle = "ICMP, TTL 1 to 24"};
static list_tool_t L_DNS = {.tool = KG_TOOL_DNS, .title = "DNS LOOKUP", .target = true, .idle = "IPv4 addresses (A records)"};
static list_tool_t L_PORTS = {.tool = KG_TOOL_PORTS, .title = "PORT CHECK", .target = true, .idle = "16 common TCP ports"};
static list_tool_t L_LAN = {.tool = KG_TOOL_LAN, .title = "LAN SCAN", .target = false, .idle = "ARP sweep of the local /24"};
static list_tool_t L_BLE = {.tool = KG_TOOL_BLE, .title = "BLUETOOTH SCAN", .target = false, .idle = "5-second active scan"};

static void list_start(list_tool_t *L) { hal_tool_start(L->tool, L->target ? s_target : ""); }

static void list_run_cb(lv_event_t *e)
{
    list_tool_t *L = lv_event_get_user_data(e);
    const kg_tool_state_t *st = net_state();
    if (st->tool == L->tool && st->running) hal_tool_stop();
    else list_start(L);
}

static void list_refresh_tool(list_tool_t *L, const kg_tool_state_t *st)
{
    bool mine = st->tool == L->tool;
    net_run_button_set(L->run, mine && st->running);
    if (!mine) {
        lv_label_set_text(L->status, L->idle);
        lv_obj_set_style_text_color(L->status, KG_C_TEXT_DIM, 0);
        for (int i = 0; i < L->shown; i++) lv_obj_add_flag(L->rows[i], LV_OBJ_FLAG_HIDDEN);
        L->shown = 0;
        L->seq = 0;
        return;
    }
    if (st->seq == L->seq) return;
    L->seq = st->seq;
    lv_label_set_text(L->status, st->status);
    lv_obj_set_style_text_color(L->status, st->failed ? KG_C_ALERT : st->running ? KG_C_WARN : KG_C_TEXT_DIM, 0);
    for (int i = 0; i < st->n; i++) {
        if (!L->rows[i]) {
            lv_obj_t *r = L->rows[i] = lv_obj_create(L->list);
            lv_obj_remove_style_all(r);
            lv_obj_set_size(r, 224, 24);
            lv_obj_set_style_bg_color(r, KG_C_SURFACE, 0);
            lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(r, 6, 0);
            lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
            L->ra[i] = kit_label(r, &kg_font_text_16, KG_C_TEXT, "");
            lv_obj_align(L->ra[i], LV_ALIGN_LEFT_MID, 8, 0);
            L->rb[i] = kit_label(r, &kg_font_text_16, KG_C_TEXT, "");
            lv_obj_align(L->rb[i], LV_ALIGN_RIGHT_MID, -8, 0);
        }
        lv_obj_clear_flag(L->rows[i], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(L->ra[i], st->rows[i].a);
        lv_label_set_text(L->rb[i], st->rows[i].b);
        lv_obj_set_style_text_color(L->rb[i], net_row_color(st->rows[i].kind), 0);
        lv_obj_set_style_text_color(L->ra[i], st->rows[i].kind == KG_ROW_DIM ? KG_C_TEXT_DIM : KG_C_TEXT, 0);
    }
    for (int i = st->n; i < L->shown; i++) lv_obj_add_flag(L->rows[i], LV_OBJ_FLAG_HIDDEN);
    if (st->n > L->shown && L->tool == KG_TOOL_TRACE) { /* traceroute: follow the new hops */
        lv_obj_update_layout(L->list);
        lv_obj_scroll_to_y(L->list, LV_COORD_MAX, LV_ANIM_ON);
    }
    L->shown = st->n;
}

static void list_changed(void)
{
    list_tool_t *all[] = {&L_TRACE, &L_DNS, &L_PORTS};
    for (int i = 0; i < 3; i++)
        if (all[i]->scr == lv_scr_act() || all[i]->scr == s_pick_return) list_start(all[i]);
}

static lv_obj_t *list_create(list_tool_t *L)
{
    lv_obj_t *scr = L->scr = net_screen(L->title);
    lv_coord_t y = 24;
    if (L->target) {
        L->chip = net_target_chip(scr, list_changed);
        y = 54;
    }
    L->status = kit_label(scr, &kg_font_text_16, KG_C_TEXT_DIM, L->idle);
    lv_obj_set_width(L->status, 224);
    lv_label_set_long_mode(L->status, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(L->status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(L->status, 8, y);
    L->list = lv_obj_create(scr);
    lv_obj_remove_style_all(L->list);
    lv_obj_set_pos(L->list, 0, y + 22);
    lv_obj_set_size(L->list, KG_W, KG_H - (y + 22) - 44);
    lv_obj_set_flex_flow(L->list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(L->list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(L->list, 3, 0);
    lv_obj_set_scroll_dir(L->list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(L->list, LV_SCROLLBAR_MODE_OFF);
    L->run = net_run_button(scr, NULL);
    lv_obj_add_event_cb(L->run, list_run_cb, LV_EVENT_CLICKED, L);
    return scr;
}

#define LIST_TOOL(id, var, label, sub)                                                                     \
    static lv_obj_t *id##_create(void) { return list_create(&var); }                                      \
    static void id##_refresh(const kg_tool_state_t *st) { list_refresh_tool(&var, st); }                  \
    static void id##_show(void)                                                                           \
    {                                                                                                     \
        if (var.chip) net_target_chip_update(var.chip);                                                   \
        list_start(&var); /* opening a tool runs it */                                                    \
    }                                                                                                     \
    static const net_tool_t id = {label, sub, id##_create, id##_show, id##_refresh, false};

LIST_TOOL(NET_TRACE, L_TRACE, "Traceroute", "Every hop to a host")
LIST_TOOL(NET_DNS, L_DNS, "DNS lookup", "Resolve a host name")
LIST_TOOL(NET_PORTS, L_PORTS, "Port check", "Open TCP services")
LIST_TOOL(NET_LAN, L_LAN, "LAN scan", "Devices on this network")
LIST_TOOL(NET_BLE, L_BLE, "Bluetooth scan", "BLE devices nearby")

/* ------------------------------------------------------------------ menu */

static const struct {
    const net_tool_t *tool;
    const char *tag;
} MENU[] = {
    {&NET_WIFI, "WIFI"}, {&NET_LINK, "LINK"}, {&NET_PING, "PING"}, {&NET_TRACE, "TRACE"}, {&NET_DNS, "DNS"},
    {&NET_PORTS, "PORTS"}, {&NET_LAN, "LAN"}, {&NET_SPEED, "SPEED"}, {&NET_BLE, "BLE"}, {&NET_SUBNET, "CIDR"},
};
#define N_MENU ((int)(sizeof MENU / sizeof MENU[0]))

static void menu_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    open_tool(MENU[i].tool, i);
}

static lv_obj_t *create(void)
{
    s_st = hal_alloc_large(sizeof *s_st);
    memset(s_st, 0, sizeof *s_st);
    lv_obj_t *scr = s_menu = kit_screen();
    lv_obj_t *list = lv_obj_create(scr);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, KG_W, KG_H);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(list, 5, 0);
    lv_obj_set_style_pad_ver(list, 6, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    kit_title(list, "NETWORK");
    lv_obj_set_align(lv_obj_get_child(list, 0), LV_ALIGN_DEFAULT);
    s_link = kit_label(list, &kg_font_text_16, KG_C_TEXT_DIM, "");
    lv_obj_set_width(s_link, 224);
    lv_label_set_long_mode(s_link, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_link, LV_TEXT_ALIGN_CENTER, 0);

    for (int i = 0; i < N_MENU; i++) {
        lv_obj_t *r = kit_button(list, "", false, 224, 44, menu_cb, (void *)(intptr_t)i);
        lv_obj_set_style_radius(r, 10, 0);
        lv_obj_del(lv_obj_get_child(r, 0));
        lv_obj_t *tag = lv_obj_create(r);
        lv_obj_remove_style_all(tag);
        lv_obj_set_size(tag, 46, 22);
        lv_obj_align(tag, LV_ALIGN_LEFT_MID, 8, 0);
        lv_obj_set_style_radius(tag, 5, 0);
        lv_obj_set_style_border_color(tag, i == 9 ? KG_C_TRACK : KG_C_ACCENT, 0);
        lv_obj_set_style_border_width(tag, 1, 0);
        lv_obj_clear_flag(tag, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t *t = kit_label(tag, &kg_font_stencil_11, i == 9 ? KG_C_TRACK : KG_C_ACCENT, MENU[i].tag);
        lv_obj_center(t);
        lv_obj_t *name = kit_label(r, &kg_font_label_20, KG_C_TEXT, MENU[i].tool->name);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 64, 1);
        lv_obj_t *desc = kit_label(r, &kg_font_text_16, KG_C_TEXT_DIM, MENU[i].tool->desc);
        lv_obj_align(desc, LV_ALIGN_BOTTOM_LEFT, 64, -3);
    }
    s_poll = lv_timer_create(poll, 200, NULL);
    lv_timer_pause(s_poll);
    return scr;
}

static void on_show(void)
{
    s_active = NULL;
    lv_obj_scroll_to_y(lv_obj_get_child(s_menu, 0), 0, LV_ANIM_OFF);
    poll(NULL);
    lv_timer_resume(s_poll);
}

static void on_hide(void)
{
    lv_timer_pause(s_poll);
    hal_tool_stop();
    hal_net_release();
}

/* Console / simulator: "app net:<tool>" opens a tool directly (e.g. net:ping). */
bool ui_net_open(const char *tool)
{
    for (int i = 0; i < N_MENU; i++) {
        char tag[8];
        snprintf(tag, sizeof tag, "%s", MENU[i].tag);
        for (char *c = tag; *c; c++) *c = (char)(*c | 0x20);
        if (strcmp(tag, tool)) continue;
        if (!s_menu) return false;
        if (!s_tool_scr[i]) s_tool_scr[i] = MENU[i].tool->create();
        s_active = MENU[i].tool;
        if (s_active->show) s_active->show();
        hal_tool_get(s_st);
        if (s_active->refresh) s_active->refresh(s_st);
        lv_scr_load(s_tool_scr[i]);
        return true;
    }
    return false;
}

const kg_app_t kg_app_net = {
    .name = "net",
    .label = "Network",
    .icon = KG_ICON_NET,
    .create = create,
    .on_show = on_show,
    .on_hide = on_hide,
};
