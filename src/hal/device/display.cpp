/*
 * Écran ST7789 240×240 via esp_lcd (SPI + DMA) et pilote d'affichage LVGL.
 * Double tampon partiel en RAM interne DMA : LVGL dessine dans l'un pendant
 * que l'autre part en SPI.
 */
#include "board.h"
#include "hal_device.h"
#include "lvgl.h"
#include <Arduino.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>

#ifndef KG_SPI_HZ
#define KG_SPI_HZ (40 * 1000 * 1000) /* 80 MHz testé en Tranche 0 (cf. DECISIONS) */
#endif

static constexpr int W = 240, H = 240;
static constexpr int BUF_LINES = 30; /* 2 × 14,4 Ko de DMA interne : marge pour BLE + Wi-Fi (D44) */
/* La RAM de l'ST7789 fait 240×320 ; en rotation 180° l'image visible commence à la ligne 80. */
static constexpr int GAP_Y = 80;

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static lv_disp_drv_t s_drv;
static lv_disp_draw_buf_t s_draw_buf;

static uint32_t s_frames, s_fps_t0;
static uint32_t s_mon_time, s_mon_px, s_mon_n;
static uint32_t s_full_time, s_full_n, s_full_max; /* images plein écran (transitions) */

/* Appelé par LVGL après chaque rafraîchissement : durée (rendu + envoi) et surface. */
static void monitor_cb(lv_disp_drv_t *, uint32_t time, uint32_t px)
{
    s_mon_time += time;
    s_mon_px += px;
    s_mon_n++;
    if (px >= W * H) {
        s_full_time += time;
        s_full_n++;
        if (time > s_full_max) s_full_max = time;
    }
}

void display_frame_stats(float *ms, uint32_t *px)
{
    *ms = s_mon_n ? (float)s_mon_time / s_mon_n : 0;
    *px = s_mon_n ? s_mon_px / s_mon_n : 0;
    s_mon_time = s_mon_px = s_mon_n = 0;
}

void display_full_stats(float *avg_ms, uint32_t *max_ms, uint32_t *n)
{
    *avg_ms = s_full_n ? (float)s_full_time / s_full_n : 0;
    *max_ms = s_full_max;
    *n = s_full_n;
    s_full_time = s_full_n = s_full_max = 0;
}
static float s_fps;

static bool on_trans_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *ctx)
{
    lv_disp_flush_ready(static_cast<lv_disp_drv_t *>(ctx));
    return false;
}

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *px)
{
    esp_lcd_panel_draw_bitmap(s_panel, a->x1, a->y1, a->x2 + 1, a->y2 + 1, px);
    if (lv_disp_flush_is_last(drv)) s_frames++;
}

void display_init(void)
{
    spi_bus_config_t bus = {};
    bus.mosi_io_num = PIN_TFT_MOSI;
    bus.miso_io_num = -1;
    bus.sclk_io_num = PIN_TFT_SCLK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = W * BUF_LINES * sizeof(lv_color_t);
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t io_cfg = {};
    io_cfg.dc_gpio_num = PIN_TFT_DC;
    io_cfg.cs_gpio_num = PIN_TFT_CS;
    io_cfg.pclk_hz = KG_SPI_HZ;
    io_cfg.lcd_cmd_bits = 8;
    io_cfg.lcd_param_bits = 8;
    io_cfg.spi_mode = 0;
    io_cfg.trans_queue_depth = 10;
    io_cfg.on_color_trans_done = on_trans_done;
    io_cfg.user_ctx = &s_drv;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &io));
    s_io = io;

    esp_lcd_panel_dev_config_t panel_cfg = {};
    panel_cfg.reset_gpio_num = -1;
    panel_cfg.color_space = ESP_LCD_COLOR_SPACE_RGB;
    panel_cfg.bits_per_pixel = 16;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &panel_cfg, &s_panel));
    /* Pas de broche RST : reset logiciel. Si la dalle était active (ancien firmware, redémarrage
     * à chaud), la fiche ST7789 exige 120 ms avant SLPOUT ; esp_lcd n'en attend que 20 et la dalle
     * reste alors en veille (rétroéclairage allumé, image noire). */
    esp_lcd_panel_reset(s_panel);
    vTaskDelay(pdMS_TO_TICKS(150));
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_io_tx_param(io, 0x11 /* SLPOUT */, nullptr, 0);
    vTaskDelay(pdMS_TO_TICKS(120));
    esp_lcd_panel_invert_color(s_panel, true);       /* dalle IPS : INVON */
    esp_lcd_panel_mirror(s_panel, true, true);       /* rotation 180°, comme setRotation(2) */
    esp_lcd_panel_set_gap(s_panel, 0, GAP_Y);
    esp_lcd_panel_disp_on_off(s_panel, true);

    size_t bytes = W * BUF_LINES * sizeof(lv_color_t);
    auto *b1 = static_cast<lv_color_t *>(heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    auto *b2 = static_cast<lv_color_t *>(heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    assert(b1 && b2);
    lv_disp_draw_buf_init(&s_draw_buf, b1, b2, W * BUF_LINES);

    lv_disp_drv_init(&s_drv);
    s_drv.hor_res = W;
    s_drv.ver_res = H;
    s_drv.flush_cb = flush_cb;
    s_drv.draw_buf = &s_draw_buf;
    s_drv.monitor_cb = monitor_cb;
    lv_disp_drv_register(&s_drv);
    s_fps_t0 = millis();
}

/* Veille de la dalle (SLPIN/SLPOUT) : quelques µA au lieu de ~6 mA. */
void display_sleep(bool sleep)
{
    esp_lcd_panel_disp_on_off(s_panel, !sleep);
    esp_lcd_panel_io_handle_t io = s_io;
    esp_lcd_panel_io_tx_param(io, sleep ? 0x10 /* SLPIN */ : 0x11 /* SLPOUT */, nullptr, 0);
    vTaskDelay(pdMS_TO_TICKS(sleep ? 5 : 120)); /* 120 ms exigés après SLPOUT */
}

/* Images complètes envoyées par seconde, moyennées depuis le dernier appel. */
float display_fps(void)
{
    uint32_t now = millis();
    if (now - s_fps_t0 >= 1000) {
        s_fps = s_frames * 1000.0f / (now - s_fps_t0);
        s_frames = 0;
        s_fps_t0 = now;
    }
    return s_fps;
}

/* Mire de test hors LVGL : trois bandes verticales rouge | vert | bleu pendant 5 s.
 * Sert à valider l'écran lui-même et l'ordre des couleurs (RGB/BGR). */
void display_test_pattern(void)
{
    static const uint16_t colors[3] = {0xF800, 0x07E0, 0x001F};
    size_t bytes = 80 * BUF_LINES * 2;
    auto *band = static_cast<uint8_t *>(heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (!band) return;
    for (int c = 0; c < 3; c++) {
        for (size_t i = 0; i < bytes; i += 2) { /* octets poids fort d'abord, comme sur le bus SPI */
            band[i] = colors[c] >> 8;
            band[i + 1] = colors[c] & 0xFF;
        }
        for (int y = 0; y < H; y += BUF_LINES) {
            esp_lcd_panel_draw_bitmap(s_panel, c * 80, y, c * 80 + 80, y + BUF_LINES, band);
            vTaskDelay(pdMS_TO_TICKS(5)); /* le tampon est réutilisé : laisser la DMA finir */
        }
    }
    vTaskDelay(pdMS_TO_TICKS(5000));
    heap_caps_free(band);
    lv_obj_invalidate(lv_scr_act());
}
