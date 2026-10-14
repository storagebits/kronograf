/* LVGL memory pool: PSRAM on the watch, malloc in the simulator. */
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *kg_lv_pool_alloc(size_t bytes);

#ifdef __cplusplus
}
#endif
