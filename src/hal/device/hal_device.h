/* Fonctions propres à la cible montre (non vues par l'UI). */
#pragma once
#include <stddef.h>
#include <stdint.h>
class Print;

/* Fuseau America/Toronto avec règles DST nord-américaines. */
#define KG_TZ "EST5EDT,M3.2.0,M11.1.0"

void hal_device_init(void);

void display_init(void);
float display_fps(void);
/* Temps moyen de rendu + envoi d'une image (ms) et pixels redessinés par image. */
void display_frame_stats(float *ms, uint32_t *px);
void display_full_stats(float *avg_ms, uint32_t *max_ms, uint32_t *n);
void display_test_pattern(void);

void touch_init(void);
bool touch_last(int16_t *x, int16_t *y);

/* Horloge PCF8563 */
void rtc_from_system(void);

/* AXP202 : interruptions bouton / USB (aucun rail modifié) */
#define PMU_IRQ_BUTTON 0x1
#define PMU_IRQ_VBUS   0x2
void pmu_enable_button_irq(void);
void pmu_clear_irq(void);
uint32_t pmu_read_irq(void);
float pmu_discharge_ma(void);
float pmu_vbus_ma(void);
float pmu_discharged_mah(void);
void pmu_audio_power(bool on);

/* Vibreur et sons */
void feedback_init(void);

/* Journal d'autonomie (sur batterie) */
void battlog_print(Print &out);

/* Écran */
void display_sleep(bool sleep);

/* Énergie */
void power_init(void);
void power_loop(void);
bool power_screen_on(void);
const char *power_wake_reason(void);
void power_set_brightness(uint8_t level);
uint8_t power_brightness(void);
void power_set_timeout(uint32_t ms);
void power_force(bool on);
void power_counters(uint32_t *wakes, uint32_t *light_sleeps);

/* Réseau : synchro NTP en tâche de fond si secrets.h fournit un Wi-Fi */
void net_start_time_sync(void);
const char *net_status(void);
int net_bus_last_error(void);

/* Garde-fous (guard.cpp) : chien de garde RTC et miettes de pain en mémoire RTC. */
enum {
    KG_CRUMB_NONE, KG_CRUMB_LOOP, KG_CRUMB_LVGL, KG_CRUMB_CONSOLE, KG_CRUMB_POWER, KG_CRUMB_PRE_SLEEP,
    KG_CRUMB_POST_SLEEP, KG_CRUMB_SCREEN_OFF, KG_CRUMB_SCREEN_ON, KG_CRUMB_WORKSHOP_ON, KG_CRUMB_WORKSHOP_OFF,
    KG_CRUMB_NET,
};
void guard_init(void);
void guard_feed(void);
void guard_suspend(bool off);
bool guard_safe_mode(void);
void guard_crumb(uint32_t id);
const char *guard_report(char *buf, size_t n);

/* USB stable (anti-rebond 3 s) : base des décisions Wi-Fi et sommeil léger. */
bool power_usb_stable(void);
void power_battery_sim(bool on);

void console_poll(void);
void console_tcp_enable(bool on);

/* Mode atelier : sur USB, Wi-Fi + OTA + console TCP (cf. DECISIONS). */
void net_workshop_loop(void);

/* BLE / notifications iPhone (ble.cpp) */
void ble_start(void);
bool ble_active(void);     /* contrôleur allumé : pas de sommeil léger (pas de quartz 32 kHz) */
bool ble_take_wake(void);  /* une notification ou un appairage demande l'écran */
