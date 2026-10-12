/* Fonctions propres à la cible montre (non vues par l'UI). */
#pragma once
#include <stdint.h>

/* Fuseau America/Toronto avec règles DST nord-américaines. */
#define KG_TZ "EST5EDT,M3.2.0,M11.1.0"

void hal_device_init(void);

void display_init(void);
float display_fps(void);
/* Temps moyen de rendu + envoi d'une image (ms) et pixels redessinés par image. */
void display_frame_stats(float *ms, uint32_t *px);
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

/* Écran */
void display_sleep(bool sleep);

/* Énergie */
void power_init(void);
void power_loop(void);
bool power_screen_on(void);
const char *power_wake_reason(void);
void power_set_brightness(uint8_t level);
void power_set_timeout(uint32_t ms);
void power_force(bool on);

/* Réseau : synchro NTP en tâche de fond si secrets.h fournit un Wi-Fi */
void net_start_time_sync(void);
const char *net_status(void);

void console_poll(void);
