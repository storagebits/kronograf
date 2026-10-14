/*
 * Application state shared by faces and apps (hardware-independent): stopwatch, timer,
 * alarms. Time keeps flowing with the screen off: everything is computed from timestamps,
 * nothing is "counted" by an LVGL timer.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------- stopwatch */
#define KG_MAX_LAPS 5
uint32_t model_chrono_elapsed_ms(void);
bool model_chrono_running(void);
void model_chrono_start_stop(void);
void model_chrono_reset(void);
void model_chrono_lap(void);
int model_chrono_laps(uint32_t out[KG_MAX_LAPS]); /* cumulative times, most recent first */

/* ------------------------------------------------------------------ timer */
typedef enum { KG_TIMER_IDLE, KG_TIMER_RUNNING, KG_TIMER_PAUSED, KG_TIMER_DONE } kg_timer_state_t;
void model_timer_set(uint32_t ms);
uint32_t model_timer_duration(void);
void model_timer_start_pause(void);
void model_timer_reset(void);
uint32_t model_timer_remaining_ms(void);
kg_timer_state_t model_timer_state(void);

/* ----------------------------------------------------------------- alarms */
#define KG_MAX_ALARMS 4
typedef struct {
    bool enabled;
    uint8_t hour, min;
} kg_alarm_t;
void model_alarms_load(void);
kg_alarm_t model_alarm_get(int i);
void model_alarm_set(int i, kg_alarm_t a);
bool model_next_alarm(struct tm *out); /* next enabled alarm (snooze included) */
void model_alarm_snooze(void);          /* +5 min */
void model_alarm_dismiss(void);

/* ----------------------------------------------------------------- events */
typedef enum { KG_EVT_NONE, KG_EVT_TIMER_DONE, KG_EVT_ALARM } kg_event_t;
/* Call regularly (and on wake-up): returns a due event, exactly once. */
kg_event_t model_poll(void);
/* Next time an event is due (0 = none): used for the wake-up timer. */
time_t model_next_event_time(void);

#ifdef __cplusplus
}
#endif
