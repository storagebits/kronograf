/*
 * Diagnostic console (serial at 115200 baud, or TCP in workshop mode; one command per line).
 *   status      internal heap / PSRAM, FPS, battery, uptime, revision
 *   screenshot  LVGL capture of the current screen, sent as base64 (tools/kg.py)
 *   sleep|wake  turns the screen off / on
 *   bl <n>      backlight 8..255
 *   timeout <s> screen timeout
 *   battlog     battery-life log of the last period on battery
 *   face <n>    shows face n (0 aviator, 1 circuits, 2 night)
 *   launcher    opens the app launcher
 *   app <name>  opens an app (chrono, timer, alarms, world, weather, sky, steps, level, torch,
 *               net, remote, system, settings); "app net:<tool>" opens a network tool
 *   ring <alarm|timer>  alert screen + ringing (test)
 *   timer <s>   starts the timer (deadline test)
 *   battsim <0|1>  simulates battery on USB (light-sleep test)
 *   power [min]    measures current on USB, with simulated battery and the screen off
 *   weather [refresh]   latest weather reading
 *   bus [refresh | stops <codes> | key <key> | key clear]  departures, stops, STM API key
 *   ble [on | off | forget]  BLE state, ANCS counters, notifications
 *   net [<tool> [target]]  network toolbox: ping, trace, dns, ports, lan, speed, public, ble
 *                          (starts it; "net" alone prints its progress and renews the Wi-Fi lease)
 *   sys         system information (as in the System app)
 *   accel       accelerometer reading in g
 *   ir [<brand> <key>]  sends a TV code (brand 0-3, key 0 power … 6 input)
 *   beep        short beep (speaker test)
 *   egg         triggers the Time circuits discharge
 *   coredump [erase]  summary of the last crash (and erase it)
 *   reboot      reboots the watch
 *   diag        shows the stage 0 diagnostic screen
 *   touch [s]   prints touch points for s seconds (10 by default)
 *   lcdtest     red | green | blue pattern for 5 s, outside LVGL
 *   log <n>     ESP log level (0 = none … 5 = verbose)
 *   help
 */
#include "../hal.h"
#include "hal_device.h"
#include "lvgl.h"
#include "../../ui/ui.h"
#include "../../ui/faces.h"
#include "../../ui/nav.h"
#include "../../ui/model.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_core_dump.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <rom/rtc.h>
#include <esp_sleep.h>
#include <esp_task_wdt.h>
#include <esp_log.h>
#include <WiFi.h>
#include <mbedtls/base64.h>

/* The console answers on the stream the command came from: USB serial or TCP (workshop mode). */
static Print *s_out = &Serial;

struct line_buf {
    char text[64];
    size_t len;
};
static line_buf s_serial_line, s_tcp_line;

static void cmd_status(void)
{
    const kg_inventory_t *inv = hal_inventory();
    kg_battery_t b = hal_battery();
    size_t int_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t int_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    lv_mem_monitor_t lv;
    lv_mem_monitor(&lv);
    char gbuf[120];
    s_out->printf("rev=V%d uptime=%lus reset=%d rom=%d\n", inv->rev, (unsigned long)(millis() / 1000), (int)esp_reset_reason(),
                  (int)rtc_get_reset_reason(0));
    s_out->printf("guard: %s\n", guard_report(gbuf, sizeof gbuf));
    s_out->printf("heap_internal free=%u/%u (%u%%) min=%u largest=%u\n", (unsigned)int_free,
                  (unsigned)int_total, (unsigned)(int_free * 100 / int_total),
                  (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    s_out->printf("psram free=%u/%u\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                  (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    s_out->printf("lvgl_mem used=%u%% frag=%u%%\n", lv.used_pct, lv.frag_pct);
    float frame_ms;
    uint32_t frame_px;
    display_frame_stats(&frame_ms, &frame_px);
    s_out->printf("fps=%.1f frame=%.1fms px/frame=%lu cpu=%luMHz\n", display_fps(), frame_ms,
                  (unsigned long)frame_px, (unsigned long)getCpuFrequencyMhz());
    float full_avg;
    uint32_t full_max, full_n;
    display_full_stats(&full_avg, &full_max, &full_n);
    s_out->printf("full_frames n=%lu avg=%.1fms max=%lums\n", (unsigned long)full_n, full_avg, (unsigned long)full_max);
    s_out->printf("current batt_discharge=%.1fmA vbus=%.1fmA\n", pmu_discharge_ma(), pmu_vbus_ma());
    s_out->printf("battery=%d%% %dmV charging=%d usb=%d\n", b.percent, b.millivolts, b.charging, b.usb);
    uint32_t wakes, sleeps;
    power_counters(&wakes, &sleeps);
    s_out->printf("face=%s brightness=%u\n", ui_faces_name(ui_faces_current()), power_brightness());
    s_out->printf("screen=%s wake=%s wakes=%lu light_sleeps=%lu steps=%lu\n", power_screen_on() ? "on" : "off",
                  power_wake_reason(), (unsigned long)wakes, (unsigned long)sleeps,
                  (unsigned long)hal_steps_today());
    struct tm t;
    hal_now(&t, nullptr);
    s_out->printf("time=%04d-%02d-%02d %02d:%02d:%02d valid=%d net=%s\n", t.tm_year + 1900, t.tm_mon + 1,
                  t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, hal_time_valid(), net_status());
}

/* Writes the whole buffer: over TCP, write() may accept only part of it when lwIP's send
 * buffer is full; retry (2 s max) rather than lose bytes. */
static void write_all(const uint8_t *data, size_t len)
{
    uint32_t t0 = millis();
    while (len && millis() - t0 < 2000) {
        esp_task_wdt_reset(); /* long commands (a capture takes ~9 s over Wi-Fi): feed the watchdog */
        size_t n = s_out->write(data, len);
        data += n;
        len -= n;
        if (!n) delay(2);
    }
}

static void print_all(const char *text) { write_all((const uint8_t *)text, strlen(text)); }

/* Format: "KGSHOT <w> <h> rgb565 swap=<0|1>", then "R<y>:<base64>" per row, "KGSHOT END". */
static void cmd_screenshot(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_img_dsc_t dsc;
    uint32_t size = lv_snapshot_buf_size_needed(scr, LV_IMG_CF_TRUE_COLOR);
    auto *buf = static_cast<uint8_t *>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM));
    if (!buf) {
        s_out->println("KGSHOT ERR mem");
        return;
    }
    if (lv_snapshot_take_to_buf(scr, LV_IMG_CF_TRUE_COLOR, &dsc, buf, size) != LV_RES_OK) {
        s_out->println("KGSHOT ERR snapshot");
        heap_caps_free(buf);
        return;
    }
    int w = dsc.header.w, h = dsc.header.h;
    s_out->printf("KGSHOT %d %d rgb565 swap=%d\n", w, h, LV_COLOR_16_SWAP);
    static unsigned char line[240 * 2 * 4 / 3 + 8];
    for (int y = 0; y < h; y++) {
        size_t olen = 0;
        mbedtls_base64_encode(line, sizeof line, &olen, buf + y * w * 2, w * 2);
        char head[8];
        snprintf(head, sizeof head, "R%d:", y); /* row index: interleaved log lines are ignored */
        print_all(head);
        line[olen] = '\n';
        write_all(line, olen + 1);
    }
    s_out->println("KGSHOT END");
    heap_caps_free(buf);
}

/* Summary of the last crash stored in flash (coredump partition). Decode the addresses on the
 * Mac: xtensa-esp32-elf-addr2line -pfiaC -e firmware.elf <addresses>. */
static void cmd_coredump(bool erase)
{
    if (esp_core_dump_image_check() != ESP_OK) {
        s_out->println("coredump: none");
        return;
    }
    auto *sum = static_cast<esp_core_dump_summary_t *>(malloc(sizeof(esp_core_dump_summary_t)));
    if (sum && esp_core_dump_get_summary(sum) == ESP_OK) {
        s_out->printf("coredump: task=%s pc=0x%08lx cause=%lu vaddr=0x%08lx elf=%.16s\n", sum->exc_task,
                      (unsigned long)sum->exc_pc, (unsigned long)sum->ex_info.exc_cause,
                      (unsigned long)sum->ex_info.exc_vaddr, (const char *)sum->app_elf_sha256);
        s_out->print("backtrace:");
        for (uint32_t i = 0; i < sum->exc_bt_info.depth; i++) s_out->printf(" 0x%08lx", (unsigned long)sum->exc_bt_info.bt[i]);
        s_out->println(sum->exc_bt_info.corrupted ? " (corrupted)" : "");
    }
    free(sum);
    if (erase) {
        esp_core_dump_image_erase();
        s_out->println("coredump erased");
    }
}

/* Raw coredump image (ELF) as base64, for espcoredump.py on the Mac. */
static void cmd_coredump_raw(void)
{
    size_t addr = 0, size = 0;
    if (esp_core_dump_image_get(&addr, &size) != ESP_OK) {
        s_out->println("COREDUMP ERR none");
        return;
    }
    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, nullptr);
    if (!part) {
        s_out->println("COREDUMP ERR partition");
        return;
    }
    s_out->printf("COREDUMP %u\n", (unsigned)size);
    uint8_t chunk[384];
    unsigned char line[520];
    for (size_t off = 0; off < size; off += sizeof chunk) {
        size_t n = size - off < sizeof chunk ? size - off : sizeof chunk;
        esp_partition_read(part, addr - part->address + off, chunk, n);
        size_t olen = 0;
        mbedtls_base64_encode(line, sizeof line, &olen, chunk, n);
        char head[16];
        snprintf(head, sizeof head, "C%u:", (unsigned)off);
        print_all(head);
        line[olen] = '\n';
        write_all(line, olen + 1);
    }
    s_out->println("COREDUMP END");
}

/* Network toolbox from the console: starts a tool, or prints the running one's state. */
static void cmd_net(const char *args)
{
    static const char *const NAMES[] = {"", "ping", "trace", "dns", "ports", "lan", "speed", "public", "ble"};
    hal_net_hold();
    char name[12] = "", target[48] = "";
    sscanf(args, "%11s %47s", name, target);
    if (name[0]) {
        for (int t = 1; t < (int)(sizeof NAMES / sizeof NAMES[0]); t++) {
            if (strcmp(name, NAMES[t])) continue;
            hal_tool_start((kg_tool_t)t, target[0] ? target : (t == KG_TOOL_DNS ? "example.com" : "gateway"));
            s_out->printf("net: %s started; \"net\" shows progress\n", name);
            return;
        }
        s_out->println("net: unknown tool (ping, trace, dns, ports, lan, speed, public, ble)");
        return;
    }
    static kg_tool_state_t *stp; /* 2.6 KB, in PSRAM */
    if (!stp && !(stp = (kg_tool_state_t *)hal_alloc_large(sizeof *stp))) return;
    kg_tool_state_t &st = *stp;
    hal_tool_get(&st);
    kg_link_t l;
    hal_net_link(&l);
    char ip[16], gw[16];
    s_out->printf("link: %s %s rssi %d ch %d ip %s gw %s\n", l.up ? "up" : "down", l.ssid, l.rssi, l.channel,
                  kg_ip_str(l.ip, ip), kg_ip_str(l.gw, gw));
    s_out->printf("tool: %s %s target \"%s\" — %s\n", NAMES[st.tool < 9 ? st.tool : 0], st.running ? "running" : "done",
                  st.target, st.status);
    for (int i = 0; i < st.n; i++) s_out->printf("  %-24s %s\n", st.rows[i].a, st.rows[i].b);
    if (st.tool == KG_TOOL_PING && st.recv)
        s_out->printf("  rtt min %.1f avg %.1f max %.1f ms\n", st.min_us / 1000.0, st.sum_us / 1000.0 / st.recv, st.max_us / 1000.0);
    if (st.tool == KG_TOOL_SPEED)
        s_out->printf("  %.1f Mbit/s (peak %.1f), %lu bytes in %lu ms, ttfb %lu ms\n", st.mbps, st.peak_mbps,
                      (unsigned long)st.bytes, (unsigned long)st.elapsed_ms, (unsigned long)st.ttfb_ms);
}

static void cmd_sys(void)
{
    kg_sysinfo_t s;
    hal_sysinfo(&s);
    s_out->printf("sys: Kronograf %s, up %lu s, reset: %s, CPU %u MHz, %.0f fps\n", s.version, (unsigned long)s.uptime_s,
                  s.reset_reason, s.cpu_mhz, s.fps);
    s_out->printf("  internal %lu / %lu free (low %lu), PSRAM %lu / %lu free\n", (unsigned long)s.heap_free,
                  (unsigned long)s.heap_total, (unsigned long)s.heap_min, (unsigned long)s.psram_free, (unsigned long)s.psram_total);
    s_out->printf("  battery %d mV %d %% %+.1f mA, VBUS %d mV, AXP %.1f °C, usb %d charging %d\n", s.batt_mv, s.batt_pct,
                  s.batt_ma, s.vbus_mv, s.pmu_temp, s.usb, s.charging);
    s_out->printf("  wakes %lu, light sleeps %lu, app %lu / %lu KB\n", (unsigned long)s.wakes, (unsigned long)s.light_sleeps,
                  (unsigned long)s.app_kb, (unsigned long)s.app_slot_kb);
}

static void run(char *cmd)
{
    if (*cmd) lv_disp_trig_activity(nullptr); /* a command counts as activity: no screen-off in the middle of a test */
    if (!strcmp(cmd, "status")) cmd_status();
    else if (!strcmp(cmd, "screenshot")) cmd_screenshot();
    else if (!strcmp(cmd, "battlog")) battlog_print(*s_out);
    else if (!strcmp(cmd, "reboot")) {
        s_out->println("rebooting");
        delay(100);
        ESP.restart();
    }
    else if (!strcmp(cmd, "coredump")) cmd_coredump(false);
    else if (!strcmp(cmd, "coredump erase")) cmd_coredump(true);
    else if (!strcmp(cmd, "coredump raw")) cmd_coredump_raw();
    else if (!strncmp(cmd, "face ", 5)) {
        power_force(true);
        ui_faces_show(atoi(cmd + 5), true);
        s_out->printf("face %s\n", ui_faces_name(ui_faces_current()));
    } else if (!strcmp(cmd, "egg")) face_circuits_discharge();
    else if (!strcmp(cmd, "launcher")) {
        power_force(true);
        ui_nav_open_launcher();
    } else if (!strncmp(cmd, "app net:", 8)) {
        power_force(true);
        ui_nav_open_app("net");
        if (!ui_net_open(cmd + 8)) s_out->println("unknown tool");
    } else if (!strncmp(cmd, "app ", 4)) {
        power_force(true);
        s_out->println(ui_nav_open_app(cmd + 4) ? "ok" : "unknown app (or transition running)");
    } else if (!strncmp(cmd, "ring ", 5)) {
        power_force(true);
        ui_nav_test_alert(!strcmp(cmd + 5, "alarm"));
    } else if (!strncmp(cmd, "timer ", 6)) {
        model_timer_set(1000u * atoi(cmd + 6));
        model_timer_start_pause();
        s_out->println("ok");
    } else if (!strncmp(cmd, "battsim ", 8)) {
        power_battery_sim(atoi(cmd + 8));
        s_out->println("ok (serial console only: Wi-Fi is about to switch off)");
    } else if (!strcmp(cmd, "power")) {
        power_measure_print(*s_out);
    } else if (!strncmp(cmd, "power ", 6)) {
        power_measure_start(atoi(cmd + 6));
        s_out->println("ok: Wi-Fi off during the measurement, the console comes back at the end");
    } else if (!strcmp(cmd, "weather") || !strcmp(cmd, "weather refresh")) {
        if (cmd[7]) hal_weather_refresh(0);
        kg_weather_t wx;
        const char *st = hal_weather_status();
        if (hal_weather(&wx))
            s_out->printf("weather %.1f° (feels %.1f°) %s, low %.1f high %.1f, read %ld s ago%s%s\n", wx.temp,
                          wx.feels, kg_weather_label(wx.code), wx.tmin, wx.tmax, (long)(time(nullptr) - wx.fetched),
                          st ? " — " : "", st ? st : "");
        else s_out->printf("weather: no reading (%s)\n", st ? st : "?");
        if (hal_weather_busy()) s_out->println("update in progress");
    } else if (!strncmp(cmd, "bus key ", 8)) {
        hal_bus_set_key(strcmp(cmd + 8, "clear") ? cmd + 8 : "");
        char hint[16];
        hal_bus_key_hint(hint, sizeof hint);
        s_out->printf("bus: STM API key %s%s\n", hint[0] ? "set " : "cleared", hint); /* never echoed in full */
    } else if (!strcmp(cmd, "bus") || !strcmp(cmd, "bus refresh") || !strncmp(cmd, "bus stops ", 10)) {
        if (!strncmp(cmd, "bus stops ", 10)) hal_bus_set_stops(cmd + 10);
        if (cmd[3]) hal_bus_refresh(0);
        kg_bus_t bus;
        const char *st = hal_bus_status();
        s_out->printf("bus: stops \"%s\"%s%s (last HTTP error %d)\n", hal_bus_stops(), st ? " — " : "", st ? st : "",
                      net_bus_last_error());
        if (hal_bus(&bus)) {
            time_t now = time(nullptr);
            for (int i = 0; i < bus.n; i++)
                s_out->printf("  %s at stop %s in %ld s\n", bus.deps[i].route, bus.deps[i].stop, (long)(bus.deps[i].t - now));
        }
    } else if (!strcmp(cmd, "ble") || !strncmp(cmd, "ble ", 4)) {
        if (!strcmp(cmd, "ble on")) hal_ble_enable(true);
        else if (!strcmp(cmd, "ble off")) hal_ble_enable(false);
        else if (!strcmp(cmd, "ble forget")) hal_ble_forget();
        static const char *const ST[] = {"off", "waiting", "pairing", "connected", "ANCS ready"};
        s_out->printf("ble: %s%s, %d notifications, internal heap free %lu\n", ST[hal_ble_state()],
                      guard_safe_mode() ? " (safe mode)" : "", hal_notif_count(),
                      (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        ble_diag(*s_out);
        for (int i = 0; i < hal_notif_count(); i++) {
            kg_notif_t n;
            if (hal_notif_get(i, &n)) s_out->printf("  [%u] %s — %s\n", n.category, n.app, n.title);
        }
    } else if (!strcmp(cmd, "net") || !strncmp(cmd, "net ", 4)) {
        cmd_net(cmd[3] ? cmd + 4 : "");
    } else if (!strcmp(cmd, "sys")) {
        cmd_sys();
    } else if (!strcmp(cmd, "accel")) {
        float x, y, z;
        if (hal_accel(&x, &y, &z)) s_out->printf("accel: x %+.3f y %+.3f z %+.3f g\n", x, y, z);
        else s_out->println("accel: no accelerometer");
    } else if (!strcmp(cmd, "ir") || !strncmp(cmd, "ir ", 3)) {
        int b, k;
        if (sscanf(cmd, "ir %d %d", &b, &k) == 2 && b >= 0 && b < KG_IR_N_BRANDS && k >= 0 && k < KG_N_KEYS) {
            hal_ir_send(KG_IR_BRANDS[b].proto, KG_IR_BRANDS[b].keys[k]);
            s_out->printf("ir: sent %s key %d\n", KG_IR_BRANDS[b].name, k);
        }
        ir_print(*s_out);
    } else if (!strcmp(cmd, "beep")) {
        static const kg_tone_t t[] = {{1320, 80}, {0, 60}, {1760, 120}};
        hal_tones(t, 3);
        s_out->println(hal_sound_available() ? "ok" : "no speaker");
    }
    else if (!strcmp(cmd, "sleep")) power_force(false);
    else if (!strcmp(cmd, "wake")) power_force(true);
    else if (!strncmp(cmd, "bl ", 3)) power_set_brightness((uint8_t)atoi(cmd + 3));
    else if (!strncmp(cmd, "timeout ", 8)) power_set_timeout(1000u * atoi(cmd + 8));
    else if (!strncmp(cmd, "touch", 5)) {
        uint32_t secs = cmd[5] == ' ' ? atoi(cmd + 6) : 10;
        uint32_t end = millis() + 1000 * secs;
        int16_t x, y;
        while (millis() < end) {
            esp_task_wdt_reset();
            lv_timer_handler(); /* keeps the touch polling running */
            if (touch_last(&x, &y)) s_out->printf("touch %d %d\n", x, y);
            delay(50);
        }
        s_out->println("ok");
    } else if (!strcmp(cmd, "diag")) {
        lv_scr_load(ui_diag_create());
        s_out->println("ok");
    } else if (!strcmp(cmd, "lcdtest")) {
        display_test_pattern();
        s_out->println("ok");
    }
    else if (!strncmp(cmd, "log ", 4)) {
        esp_log_level_set("*", (esp_log_level_t)atoi(cmd + 4));
        s_out->println("ok");
    } else if (!strcmp(cmd, "help")) s_out->println("status | screenshot | sleep | wake | bl <n> | timeout <s> | touch | battlog | power [min] | face <n> | egg | launcher | app <name> | ring <alarm|timer> | timer <s> | beep | weather [refresh] | bus [refresh | stops <codes> | key <key>] | ble [on|off|forget] | net [<tool> [target]] | sys | accel | ir [<brand> <key>] | coredump [erase] | reboot | diag | lcdtest | log <0-5> | help");
    else if (*cmd) s_out->printf("unknown command: %s\n", cmd);
}

static void feed(Stream &in, Print &out, line_buf &lb)
{
    while (in.available()) {
        char c = in.read();
        if (c == '\r') continue;
        if (c == '\n') {
            lb.text[lb.len] = 0;
            s_out = &out;
            run(lb.text);
            s_out = &Serial;
            lb.len = 0;
        } else if (lb.len < sizeof lb.text - 1) {
            lb.text[lb.len++] = c;
        }
    }
}

/* TCP console (port 2323), only active in workshop mode. */
static WiFiServer s_server(2323);
static WiFiClient s_client;
static bool s_server_on;

void console_tcp_enable(bool on)
{
    if (on && !s_server_on) {
        s_server.begin();
        s_server.setNoDelay(true);
    } else if (!on && s_server_on) {
        s_client.stop();
        s_server.end();
    }
    s_server_on = on;
}

void console_poll(void)
{
    feed(Serial, Serial, s_serial_line);
    if (!s_server_on) return;
    /* A new client replaces the old one: a missed disconnect never locks the console. */
    WiFiClient c = s_server.available();
    if (c) {
        if (s_client) s_client.stop();
        s_client = c;
        s_tcp_line.len = 0;
    }
    if (s_client && s_client.connected()) feed(s_client, s_client, s_tcp_line);
}
