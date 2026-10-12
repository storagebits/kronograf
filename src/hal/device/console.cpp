/*
 * Console série de diagnostic (115200 bauds, une commande par ligne).
 *   status      heap interne / PSRAM, FPS, batterie, uptime, révision
 *   screenshot  capture LVGL de l'écran courant, envoyée en base64 (tools/screenshot.py)
 *   log <n>     niveau de log ESP (0 = rien … 5 = verbeux)
 *   help
 */
#include "../hal.h"
#include "hal_device.h"
#include "lvgl.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <mbedtls/base64.h>

static char s_line[64];
static size_t s_len;

static void cmd_status(void)
{
    const kg_inventory_t *inv = hal_inventory();
    kg_battery_t b = hal_battery();
    size_t int_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t int_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    lv_mem_monitor_t lv;
    lv_mem_monitor(&lv);
    Serial.printf("rev=V%d uptime=%lus\n", inv->rev, (unsigned long)(millis() / 1000));
    Serial.printf("heap_internal free=%u/%u (%u%%) min=%u largest=%u\n", (unsigned)int_free,
                  (unsigned)int_total, (unsigned)(int_free * 100 / int_total),
                  (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    Serial.printf("psram free=%u/%u\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                  (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    Serial.printf("lvgl_mem used=%u%% frag=%u%%\n", lv.used_pct, lv.frag_pct);
    Serial.printf("fps=%.1f cpu=%luMHz\n", display_fps(), (unsigned long)getCpuFrequencyMhz());
    Serial.printf("battery=%d%% %dmV charging=%d usb=%d\n", b.percent, b.millivolts, b.charging, b.usb);
}

/* Format : "KGSHOT <w> <h> rgb565 swap=<0|1>", 1 ligne base64 par rangée, "KGSHOT END". */
static void cmd_screenshot(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_img_dsc_t dsc;
    uint32_t size = lv_snapshot_buf_size_needed(scr, LV_IMG_CF_TRUE_COLOR);
    auto *buf = static_cast<uint8_t *>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM));
    if (!buf) {
        Serial.println("KGSHOT ERR mem");
        return;
    }
    if (lv_snapshot_take_to_buf(scr, LV_IMG_CF_TRUE_COLOR, &dsc, buf, size) != LV_RES_OK) {
        Serial.println("KGSHOT ERR snapshot");
        heap_caps_free(buf);
        return;
    }
    int w = dsc.header.w, h = dsc.header.h;
    Serial.printf("KGSHOT %d %d rgb565 swap=%d\n", w, h, LV_COLOR_16_SWAP);
    static unsigned char line[240 * 2 * 4 / 3 + 8];
    for (int y = 0; y < h; y++) {
        size_t olen = 0;
        mbedtls_base64_encode(line, sizeof line, &olen, buf + y * w * 2, w * 2);
        Serial.write(line, olen);
        Serial.write('\n');
    }
    Serial.println("KGSHOT END");
    heap_caps_free(buf);
}

static void run(char *cmd)
{
    if (!strcmp(cmd, "status")) cmd_status();
    else if (!strcmp(cmd, "screenshot")) cmd_screenshot();
    else if (!strncmp(cmd, "log ", 4)) {
        esp_log_level_set("*", (esp_log_level_t)atoi(cmd + 4));
        Serial.println("ok");
    } else if (!strcmp(cmd, "help")) Serial.println("status | screenshot | log <0-5> | help");
    else if (*cmd) Serial.printf("commande inconnue : %s\n", cmd);
}

void console_poll(void)
{
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            s_line[s_len] = 0;
            run(s_line);
            s_len = 0;
        } else if (s_len < sizeof s_line - 1) {
            s_line[s_len++] = c;
        }
    }
}
