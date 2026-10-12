/* Réserve mémoire de LVGL : PSRAM sur la montre, malloc dans le simulateur. */
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *kg_lv_pool_alloc(size_t bytes);

#ifdef __cplusplus
}
#endif
