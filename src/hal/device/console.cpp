/*
 * Console série de diagnostic (115200 bauds, une commande par ligne).
 *   status      heap interne / PSRAM, FPS, batterie, uptime, révision
 *   screenshot  capture LVGL de l'écran courant, envoyée en base64 (tools/screenshot.py)
 *   sleep|wake  éteint / rallume l'écran
 *   bl <n>      rétroéclairage 8..255
 *   timeout <s> délai d'extinction
 *   battlog     journal d'autonomie de la dernière période sur batterie
 *   face <n>    affiche le cadran n (0 aviateur, 1 circuits, 2 nuit)
 *   egg         déclenche la décharge du cadran Circuits temporels
 *   coredump [erase]  résumé du dernier plantage (et effacement)
 *   reboot      redémarre la montre
 *   diag        affiche l'écran de diagnostic de la Tranche 0
 *   touch [s]   affiche les points touchés pendant s secondes (10 par défaut)
 *   lcdtest     mire rouge | vert | bleu 5 s, hors LVGL
 *   log <n>     niveau de log ESP (0 = rien … 5 = verbeux)
 *   help
 */
#include "../hal.h"
#include "hal_device.h"
#include "lvgl.h"
#include "../../ui/ui.h"
#include "../../ui/faces.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_core_dump.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <esp_log.h>
#include <WiFi.h>
#include <mbedtls/base64.h>

/* La console répond sur le flux d'où vient la commande : série USB ou TCP (mode atelier). */
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
    s_out->printf("rev=V%d uptime=%lus reset=%d\n", inv->rev, (unsigned long)(millis() / 1000), (int)esp_reset_reason());
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

/* Format : "KGSHOT <w> <h> rgb565 swap=<0|1>", puis "R<y>:<base64>" par rangée, "KGSHOT END". */
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
        s_out->printf("R%d:", y); /* index de rangée : les lignes de log intercalées sont ignorées */
        s_out->write(line, olen);
        s_out->write('\n');
    }
    s_out->println("KGSHOT END");
    heap_caps_free(buf);
}

/* Résumé du dernier plantage enregistré en flash (partition coredump). Les adresses se
 * décodent sur le Mac : xtensa-esp32-elf-addr2line -pfiaC -e firmware.elf <adresses>. */
static void cmd_coredump(bool erase)
{
    if (esp_core_dump_image_check() != ESP_OK) {
        s_out->println("coredump : aucun");
        return;
    }
    auto *sum = static_cast<esp_core_dump_summary_t *>(malloc(sizeof(esp_core_dump_summary_t)));
    if (sum && esp_core_dump_get_summary(sum) == ESP_OK) {
        s_out->printf("coredump : tâche=%s pc=0x%08lx cause=%lu vaddr=0x%08lx elf=%.16s\n", sum->exc_task,
                      (unsigned long)sum->exc_pc, (unsigned long)sum->ex_info.exc_cause,
                      (unsigned long)sum->ex_info.exc_vaddr, (const char *)sum->app_elf_sha256);
        s_out->print("backtrace :");
        for (uint32_t i = 0; i < sum->exc_bt_info.depth; i++) s_out->printf(" 0x%08lx", (unsigned long)sum->exc_bt_info.bt[i]);
        s_out->println(sum->exc_bt_info.corrupted ? " (corrompue)" : "");
    }
    free(sum);
    if (erase) {
        esp_core_dump_image_erase();
        s_out->println("coredump effacé");
    }
}

/* Image coredump brute (ELF) en base64, pour espcoredump.py sur le Mac. */
static void cmd_coredump_raw(void)
{
    size_t addr = 0, size = 0;
    if (esp_core_dump_image_get(&addr, &size) != ESP_OK) {
        s_out->println("COREDUMP ERR aucun");
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
        s_out->printf("C%u:", (unsigned)off);
        s_out->write(line, olen);
        s_out->write('\n');
    }
    s_out->println("COREDUMP END");
}

static void run(char *cmd)
{
    if (*cmd) lv_disp_trig_activity(nullptr); /* une commande = de l'activité : pas d'extinction en plein test */
    if (!strcmp(cmd, "status")) cmd_status();
    else if (!strcmp(cmd, "screenshot")) cmd_screenshot();
    else if (!strcmp(cmd, "battlog")) battlog_print(*s_out);
    else if (!strcmp(cmd, "reboot")) {
        s_out->println("redémarrage");
        delay(100);
        ESP.restart();
    }
    else if (!strcmp(cmd, "coredump")) cmd_coredump(false);
    else if (!strcmp(cmd, "coredump erase")) cmd_coredump(true);
    else if (!strcmp(cmd, "coredump raw")) cmd_coredump_raw();
    else if (!strncmp(cmd, "face ", 5)) {
        power_force(true);
        ui_faces_show(atoi(cmd + 5), true);
        s_out->printf("cadran %s\n", ui_faces_name(ui_faces_current()));
    } else if (!strcmp(cmd, "egg")) face_circuits_discharge();
    else if (!strcmp(cmd, "sleep")) power_force(false);
    else if (!strcmp(cmd, "wake")) power_force(true);
    else if (!strncmp(cmd, "bl ", 3)) power_set_brightness((uint8_t)atoi(cmd + 3));
    else if (!strncmp(cmd, "timeout ", 8)) power_set_timeout(1000u * atoi(cmd + 8));
    else if (!strncmp(cmd, "touch", 5)) {
        uint32_t secs = cmd[5] == ' ' ? atoi(cmd + 6) : 10;
        uint32_t end = millis() + 1000 * secs;
        int16_t x, y;
        while (millis() < end) {
            lv_timer_handler(); /* fait tourner la lecture tactile */
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
    } else if (!strcmp(cmd, "help")) s_out->println("status | screenshot | sleep | wake | bl <n> | timeout <s> | touch | battlog | face <n> | egg | coredump [erase] | reboot | diag | lcdtest | log <0-5> | help");
    else if (*cmd) s_out->printf("commande inconnue : %s\n", cmd);
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

/* Console TCP (port 2323), active seulement en mode atelier. */
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
    /* Un nouveau client remplace l'ancien : une déconnexion mal détectée ne bloque jamais la console. */
    WiFiClient c = s_server.available();
    if (c) {
        if (s_client) s_client.stop();
        s_client = c;
        s_tcp_line.len = 0;
    }
    if (s_client && s_client.connected()) feed(s_client, s_client, s_tcp_line);
}
