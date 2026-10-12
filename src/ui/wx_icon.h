/* Pictogrammes météo vectoriels (soleil, nuage, pluie…), à toute taille. */
#pragma once
#include "lvgl.h"
#include "../data/kg_weather.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Objet de `size` px de côté qui dessine le pictogramme choisi. */
lv_obj_t *kg_wx_icon_create(lv_obj_t *parent, lv_coord_t size);
void kg_wx_icon_set(lv_obj_t *icon, kg_wx_icon_t which);

#ifdef __cplusplus
}
#endif
