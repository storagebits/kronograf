/*
 * FT6336 touch panel (I²C 0x38 on Wire1, SDA 23 / SCL 32) → LVGL input device.
 * Polled at the LVGL period (16 ms), only while the screen is on.
 */
#include "board.h"
#include "hal_device.h"
#include "lvgl.h"
#include <Arduino.h>
#include <Wire.h>

/* With a 180° rotation (setRotation(2)) the LilyGO library uses raw coordinates;
 * "PANEL_V1" panels invert them. Set to 1 if touch is mirrored. */
#ifndef KG_TOUCH_FLIP
#define KG_TOUCH_FLIP 0
#endif

static lv_indev_drv_t s_drv;
static lv_indev_t *s_indev;
static int16_t s_last_x, s_last_y;
static bool s_pressed;

static bool ft_read(int16_t *x, int16_t *y)
{
    Wire1.beginTransmission(I2C_FT6336);
    Wire1.write(0x02); /* TD_STATUS, then P1_XH, P1_XL, P1_YH, P1_YL */
    if (Wire1.endTransmission(false) != 0) return false;
    if (Wire1.requestFrom(I2C_FT6336, 5) != 5) return false;
    uint8_t n = Wire1.read() & 0x0F;
    uint8_t xh = Wire1.read(), xl = Wire1.read(), yh = Wire1.read(), yl = Wire1.read();
    if (n == 0 || n > 2) return false;
    int16_t px = ((xh & 0x0F) << 8) | xl;
    int16_t py = ((yh & 0x0F) << 8) | yl;
#if KG_TOUCH_FLIP
    px = 239 - px;
    py = 239 - py;
#endif
    *x = constrain(px, 0, 239);
    *y = constrain(py, 0, 239);
    return true;
}

static void read_cb(lv_indev_drv_t *, lv_indev_data_t *data)
{
    s_pressed = ft_read(&s_last_x, &s_last_y);
    data->point.x = s_last_x;
    data->point.y = s_last_y;
    data->state = s_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void touch_init(void)
{
    lv_indev_drv_init(&s_drv);
    s_drv.type = LV_INDEV_TYPE_POINTER;
    s_drv.read_cb = read_cb;
    s_indev = lv_indev_drv_register(&s_drv);
}

bool touch_last(int16_t *x, int16_t *y)
{
    *x = s_last_x;
    *y = s_last_y;
    return s_pressed;
}
