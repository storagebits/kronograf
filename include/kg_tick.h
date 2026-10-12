/* Horloge monotone en ms pour LVGL : millis() sur la montre, SDL_GetTicks() en simulateur. */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
uint32_t kg_tick_ms(void);
#ifdef __cplusplus
}
#endif
