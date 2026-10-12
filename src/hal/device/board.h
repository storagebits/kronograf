/*
 * Brochage T-Watch 2020 par révision (source : TTGO_TWatch_Library 1.4.1, board/twatch2020_v*.h).
 * Voir docs/AUDIT.md §3.
 */
#pragma once

#if !defined(KG_BOARD_V1) && !defined(KG_BOARD_V2) && !defined(KG_BOARD_V3)
#error "Définir KG_BOARD_V1, KG_BOARD_V2 ou KG_BOARD_V3 (platformio.ini)"
#endif

#define PIN_TFT_MOSI 19
#define PIN_TFT_SCLK 18
#define PIN_TFT_CS   5
#define PIN_TFT_DC   27

#define PIN_TOUCH_SDA 23
#define PIN_TOUCH_SCL 32
#define PIN_TOUCH_INT 38

#define PIN_SEN_SDA 21
#define PIN_SEN_SCL 22

#define PIN_RTC_INT 37
#define PIN_AXP_INT 35
#define PIN_BMA_INT 39

#if defined(KG_BOARD_V1)
#define KG_REV        KG_REV_V1
#define PIN_TFT_BL    12
#define PIN_MOTOR     4
#define PIN_IR        13
#define PIN_I2S_BCK   26
#define PIN_I2S_WS    25
#define PIN_I2S_DOUT  33
#elif defined(KG_BOARD_V2)
#define KG_REV        KG_REV_V2
#define PIN_TFT_BL    25
#define PIN_IR        2
#define PIN_GPS_RX    36
#define PIN_GPS_TX    26
#define PIN_GPS_PPS   34
#define PIN_GPS_WAKE  33
#define PIN_SD_CS     13
#define PIN_SD_MISO   4
#define PIN_SD_MOSI   15
#define PIN_SD_SCLK   14
#else
#define KG_REV        KG_REV_V3
#define PIN_TFT_BL    15
#define PIN_TOUCH_RST 14
#define PIN_MOTOR     4
#define PIN_IR        13
#define PIN_I2S_BCK   26
#define PIN_I2S_WS    25
#define PIN_I2S_DOUT  33
#endif

/* Adresses I²C */
#define I2C_AXP202  0x35
#define I2C_PCF8563 0x51
#define I2C_BMA423  0x19
#define I2C_FT6336  0x38
