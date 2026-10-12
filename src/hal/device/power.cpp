/*
 * Gestion d'énergie et réveils.
 *
 *   ACTIF   écran allumé, CPU 240 MHz, LVGL à pleine cadence
 *   ÉTEINT  rétroéclairage 0, ST7789 en SLPIN, LVGL figé ;
 *           sur batterie : sommeil léger (réveil bouton / poignet levé / double tap) ;
 *           sur USB : CPU 80 MHz sans sommeil, pour garder la console série.
 *
 * Interdits (DECISIONS D6) : aucun rail de l'AXP202 n'est modifié ici.
 */
#include "../hal.h"
#include "board.h"
#include "hal_device.h"
#include "lvgl.h"
#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>
#include <esp_sleep.h>
#define XPOWERS_CHIP_AXP202
#include <XPowersLib.h>
#include <SensorBMA423.hpp>

static SensorBMA423 s_bma;
static bool s_bma_ok;
static volatile bool s_bma_irq, s_axp_irq;
static bool s_screen_on = true;
static uint32_t s_timeout_ms = 10000;
static uint8_t s_brightness = 160;
static uint32_t s_wake_ms;
static const char *s_wake_reason = "démarrage";

static void IRAM_ATTR on_bma_irq() { s_bma_irq = true; }
static void IRAM_ATTR on_axp_irq() { s_axp_irq = true; }

/* ------------------------------------------------------------- pas du jour */

/* Le BMA423 garde son compteur tant qu'il est alimenté (redémarrages ESP compris).
 * On le remet à zéro au premier passage d'un nouveau jour, date mémorisée en NVS. */
static void steps_midnight_check(void)
{
    if (!s_bma_ok || !hal_time_valid()) return;
    struct tm t;
    hal_now(&t, nullptr);
    int day = (t.tm_year + 1900) * 1000 + t.tm_yday;
    Preferences p;
    p.begin("kg", false);
    if (p.getInt("steps_day", 0) != day) {
        s_bma.resetPedometer();
        p.putInt("steps_day", day);
    }
    p.end();
}

uint32_t hal_steps_today(void) { return s_bma_ok ? s_bma.getPedometerCounter() : 0; }

/* ------------------------------------------------------------------ init */

void power_init(void)
{
    s_bma_ok = s_bma.begin(Wire, I2C_BMA423, PIN_SEN_SDA, PIN_SEN_SCL);
    if (s_bma_ok) {
        s_bma.configAccelerometer(SensorBMA423::RANGE_2G, SensorBMA423::ODR_100HZ);
        s_bma.enableAccelerometer();
        s_bma.setReampAxes(SensorBMA423::REMAP_TOP_LAYER_RIGHT_CORNER /* 0x88 = axes identité (à valider : poignet levé) */);
        s_bma.enablePedometer();
        s_bma.enableFeature(SensorBMA423::FEATURE_STEP_CNTR | SensorBMA423::FEATURE_TILT |
                                SensorBMA423::FEATURE_WAKEUP, true);
        s_bma.enableTiltIRQ();
        s_bma.enableWakeupIRQ();
        s_bma.configInterrupt(); /* INT1, niveau haut, push-pull */
        s_bma.readIrqStatus();
        pinMode(PIN_BMA_INT, INPUT);
        attachInterrupt(PIN_BMA_INT, on_bma_irq, RISING);
    } else {
        log_e("BMA423 : initialisation impossible");
    }

    pmu_enable_button_irq();
    pinMode(PIN_AXP_INT, INPUT);
    attachInterrupt(PIN_AXP_INT, on_axp_irq, FALLING);

    steps_midnight_check();
    s_wake_ms = millis();
}

/* ------------------------------------------------------------ transitions */

static void screen_off(void)
{
    if (!s_screen_on) return;
    s_screen_on = false;
    for (int b = s_brightness; b > 0; b -= 16) { /* extinction douce, ~80 ms */
        hal_backlight(b);
        delay(8);
    }
    hal_backlight(0);
    display_sleep(true);
}

static void screen_on(const char *reason)
{
    s_wake_reason = reason;
    s_wake_ms = millis();
    lv_disp_trig_activity(nullptr);
    if (s_screen_on) return;
    s_screen_on = true;
    setCpuFrequencyMhz(240);
    display_sleep(false);
    steps_midnight_check();
    lv_obj_invalidate(lv_scr_act());
    lv_timer_handler();               /* l'heure a sauté pendant la veille : redessiner avant d'éclairer */
    lv_refr_now(nullptr);
    for (int b = 16; b < s_brightness; b += 24) {
        hal_backlight(b);
        delay(6);
    }
    hal_backlight(s_brightness);
}

/* Sommeil léger jusqu'au prochain réveil matériel. Retourne la raison. */
static const char *light_sleep(void)
{
    s_bma.readIrqStatus(); /* libère INT1 avant de s'endormir */
    pmu_clear_irq();
    s_bma_irq = s_axp_irq = false;
    gpio_wakeup_enable((gpio_num_t)PIN_AXP_INT, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable((gpio_num_t)PIN_BMA_INT, GPIO_INTR_HIGH_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_sleep_enable_timer_wakeup(60ULL * 1000 * 1000); /* minuit, VBUS : vérifié chaque minute */
    Serial.flush();
    esp_light_sleep_start();
    gpio_wakeup_disable((gpio_num_t)PIN_AXP_INT);
    gpio_wakeup_disable((gpio_num_t)PIN_BMA_INT);
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) return nullptr;
    if (digitalRead(PIN_AXP_INT) == LOW) s_axp_irq = true;
    if (digitalRead(PIN_BMA_INT) == HIGH) s_bma_irq = true;
    return "gpio";
}

/* Traite les interruptions ; retourne la raison du réveil s'il y en a une. */
static const char *poll_irqs(void)
{
    const char *reason = nullptr;
    if (s_axp_irq) {
        s_axp_irq = false;
        uint32_t what = pmu_read_irq();
        if (what & PMU_IRQ_BUTTON) {
            if (s_screen_on && millis() - s_wake_ms > 400) {
                screen_off(); /* bouton écran allumé : on éteint */
                return nullptr;
            }
            reason = "bouton";
        } else if (what & PMU_IRQ_VBUS) {
            reason = "usb";
        }
    }
    if (s_bma_irq && s_bma_ok) {
        s_bma_irq = false;
        s_bma.readIrqStatus();
        if (s_bma.isTilt()) reason = "poignet";
        else if (s_bma.isDoubleTap()) reason = "double tap";
    }
    return reason;
}

void power_loop(void)
{
    const char *reason = poll_irqs();
    if (reason) screen_on(reason);

    if (s_screen_on && lv_disp_get_inactive_time(nullptr) > s_timeout_ms &&
        millis() - s_wake_ms > s_timeout_ms) {
        screen_off();
    }

    if (!s_screen_on) {
        if (hal_battery().usb) {
            setCpuFrequencyMhz(80);
        } else {
            const char *r = light_sleep();
            if (r) {
                const char *why = poll_irqs();
                screen_on(why ? why : "gpio");
            } else {
                steps_midnight_check();
            }
        }
    }
}

bool power_screen_on(void) { return s_screen_on; }
const char *power_wake_reason(void) { return s_wake_reason; }

void power_set_brightness(uint8_t level)
{
    s_brightness = level < 8 ? 8 : level;
    if (s_screen_on) hal_backlight(s_brightness);
}

void power_set_timeout(uint32_t ms) { s_timeout_ms = ms; }

void power_force(bool on)
{
    if (on) screen_on("console");
    else screen_off();
}
