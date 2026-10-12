/*
 * État applicatif partagé entre cadrans et apps (indépendant du matériel).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Prochaine alarme active (Tranche 3). Faux s'il n'y en a pas. */
bool model_next_alarm(struct tm *out);

/* Chronomètre (Tranche 3) : relié au sous-cadran du cadran aviateur. */
uint32_t model_chrono_elapsed_ms(void);
bool model_chrono_running(void);

#ifdef __cplusplus
}
#endif
