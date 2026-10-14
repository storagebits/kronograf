/* Monotonic ms clock for LVGL: millis() on the watch, SDL_GetTicks() in the simulator. */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
uint32_t kg_tick_ms(void);
#ifdef __cplusplus
}
#endif
