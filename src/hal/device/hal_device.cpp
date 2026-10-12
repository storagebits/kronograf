/*
 * HAL T-Watch 2020 : inventaire, heure, batterie, rétroéclairage, vibreur.
 *
 * Règle d'or (DECISIONS.md D6) : on ne touche JAMAIS à DCDC3 (cœur ESP32).
 * Le seul rail écrit ici est LDO2 sur V1 (écran + rétroéclairage), comme la
 * bibliothèque officielle LilyGO.
 */
#include "../hal.h"
#include "board.h"
#include "hal_device.h"
#include <Arduino.h>
#include <Wire.h>
#define XPOWERS_CHIP_AXP202
#include <XPowersLib.h>
#include <esp_heap_caps.h>
#include <sys/time.h>

static kg_inventory_t s_inv;
static XPowersAXP202 s_pmu;
static TwoWire &s_sensors = Wire;
static TwoWire &s_touch = Wire1;

static constexpr int BL_CHANNEL = 0;

extern "C" uint32_t kg_tick_ms(void) { return millis(); }

static bool probe(TwoWire &bus, uint8_t addr)
{
    bus.beginTransmission(addr);
    return bus.endTransmission() == 0;
}

static uint8_t bcd2bin(uint8_t v) { return (v & 0x0F) + 10 * (v >> 4); }

/* Lit le PCF8563 (heure stockée en UTC) et règle l'horloge système. */
static void rtc_to_system(void)
{
    s_sensors.beginTransmission(I2C_PCF8563);
    s_sensors.write(0x02);
    if (s_sensors.endTransmission(false) != 0) return;
    if (s_sensors.requestFrom(I2C_PCF8563, 7) != 7) return;
    uint8_t r[7];
    for (auto &b : r) b = s_sensors.read();
    if (r[0] & 0x80) {
        log_w("RTC : bit VL (horloge non fiable, pile vide ou jamais réglée)");
    }
    struct tm t = {};
    t.tm_sec = bcd2bin(r[0] & 0x7F);
    t.tm_min = bcd2bin(r[1] & 0x7F);
    t.tm_hour = bcd2bin(r[2] & 0x3F);
    t.tm_mday = bcd2bin(r[3] & 0x3F);
    t.tm_mon = bcd2bin(r[5] & 0x1F) - 1;
    t.tm_year = bcd2bin(r[6]) + ((r[5] & 0x80) ? 0 : 100);
    setenv("TZ", "UTC0", 1);
    tzset();
    time_t utc = mktime(&t);
    setenv("TZ", KG_TZ, 1);
    tzset();
    struct timeval tv = {.tv_sec = utc, .tv_usec = 0};
    settimeofday(&tv, nullptr);
}

void hal_device_init(void)
{
    setenv("TZ", KG_TZ, 1);
    tzset();

    s_sensors.begin(PIN_SEN_SDA, PIN_SEN_SCL, 400000);
    s_touch.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL, 400000);

    s_inv.rev = KG_REV;
    s_inv.pmu = probe(s_sensors, I2C_AXP202);
    s_inv.rtc = probe(s_sensors, I2C_PCF8563);
    s_inv.accel = probe(s_sensors, I2C_BMA423);
    s_inv.touch = probe(s_touch, I2C_FT6336);
    s_inv.flash_mb = ESP.getFlashChipSize() / (1024 * 1024);
    s_inv.psram = psramFound();
    s_inv.psram_kb = ESP.getPsramSize() / 1024;
#if defined(KG_BOARD_V1) || defined(KG_BOARD_V3)
    s_inv.motor = true;
    s_inv.speaker = true;
#endif
    s_inv.ir_tx = true;
#if defined(KG_BOARD_V2)
    s_inv.gps = true;   /* confirmé à l'exécution en Tranche 6 (trames NMEA) */
    s_inv.sdcard = true;
#endif
#if defined(KG_BOARD_V3)
    s_inv.mic = true;
#endif

    if (s_inv.pmu && s_pmu.begin(s_sensors, I2C_AXP202, PIN_SEN_SDA, PIN_SEN_SCL)) {
#if defined(KG_BOARD_V1)
        /* V1 : LDO2 alimente le ST7789 et le rétroéclairage ; il doit rester allumé. */
        s_pmu.setLDO2Voltage(3300);
        s_pmu.enableLDO2();
#endif
        s_pmu.enableBattDetection();
        s_pmu.enableBattVoltageMeasure();
        s_pmu.enableVbusVoltageMeasure();
        s_pmu.setChargingLedMode(XPOWERS_CHG_LED_OFF);
    } else {
        s_inv.pmu = false;
    }

    if (s_inv.rtc) rtc_to_system();

    ledcSetup(BL_CHANNEL, 12000, 8);
    ledcAttachPin(PIN_TFT_BL, BL_CHANNEL);
    ledcWrite(BL_CHANNEL, 0);

#ifdef PIN_MOTOR
    pinMode(PIN_MOTOR, OUTPUT);
    digitalWrite(PIN_MOTOR, LOW);
#endif
}

const kg_inventory_t *hal_inventory(void) { return &s_inv; }

void hal_now(struct tm *out, uint32_t *ms)
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    time_t now = tv.tv_sec;
    localtime_r(&now, out);
    if (ms) *ms = tv.tv_usec / 1000;
}

kg_battery_t hal_battery(void)
{
    kg_battery_t b = {-1, 0, false, false};
    if (!s_inv.pmu) return b;
    b.percent = s_pmu.getBatteryPercent();
    b.millivolts = s_pmu.getBattVoltage();
    b.charging = s_pmu.isCharging();
    b.usb = s_pmu.isVbusIn();
    return b;
}

uint32_t hal_steps_today(void) { return 0; /* Tranche 1 : BMA423 */ }

void hal_backlight(uint8_t level) { ledcWrite(BL_CHANNEL, level); }

void hal_vibrate(uint16_t ms)
{
#ifdef PIN_MOTOR
    digitalWrite(PIN_MOTOR, HIGH);
    delay(ms); /* Tranche 0 seulement ; remplacé par un minuteur non bloquant en Tranche 1 */
    digitalWrite(PIN_MOTOR, LOW);
#else
    (void)ms;
#endif
}

void *hal_alloc_large(size_t bytes) { return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM); }
