/*
 * État applicatif partagé entre cadrans et apps (indépendant du matériel) :
 * chronomètre, minuteur, alarmes. Le temps s'écoule même écran éteint : tout est
 * calculé à partir d'horodatages, rien n'est « compté » par un minuteur LVGL.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------ chronomètre */
#define KG_MAX_LAPS 5
uint32_t model_chrono_elapsed_ms(void);
bool model_chrono_running(void);
void model_chrono_start_stop(void);
void model_chrono_reset(void);
void model_chrono_lap(void);
int model_chrono_laps(uint32_t out[KG_MAX_LAPS]); /* temps cumulés, le plus récent en premier */

/* --------------------------------------------------------------- minuteur */
typedef enum { KG_TIMER_IDLE, KG_TIMER_RUNNING, KG_TIMER_PAUSED, KG_TIMER_DONE } kg_timer_state_t;
void model_timer_set(uint32_t ms);
uint32_t model_timer_duration(void);
void model_timer_start_pause(void);
void model_timer_reset(void);
uint32_t model_timer_remaining_ms(void);
kg_timer_state_t model_timer_state(void);

/* ---------------------------------------------------------------- alarmes */
#define KG_MAX_ALARMS 4
typedef struct {
    bool enabled;
    uint8_t hour, min;
} kg_alarm_t;
void model_alarms_load(void);
kg_alarm_t model_alarm_get(int i);
void model_alarm_set(int i, kg_alarm_t a);
bool model_next_alarm(struct tm *out); /* prochaine alarme active (y compris répétition) */
void model_alarm_snooze(void);          /* +5 min */
void model_alarm_dismiss(void);

/* --------------------------------------------------------------- événements */
typedef enum { KG_EVT_NONE, KG_EVT_TIMER_DONE, KG_EVT_ALARM } kg_event_t;
/* À appeler régulièrement (et au réveil) : retourne un événement dû, une seule fois. */
kg_event_t model_poll(void);
/* Prochain instant où un événement doit se produire (0 = aucun) : réveil programmé. */
time_t model_next_event_time(void);

#ifdef __cplusplus
}
#endif
