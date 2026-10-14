/* Vector weather pictograms (sun, cloud, rain…), at any size. */
#pragma once
#include "lvgl.h"
#include "../data/kg_weather.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Object `size` px square that draws the chosen pictogram. */
lv_obj_t *kg_wx_icon_create(lv_obj_t *parent, lv_coord_t size);
void kg_wx_icon_set(lv_obj_t *icon, kg_wx_icon_t which);

#ifdef __cplusplus
}
#endif
