/*
 * Transition par instantanés entre deux écrans : les deux écrans sont figés en images
 * (PSRAM) puis glissés. Une image par trame = une copie, limitée par le SPI (~40 fps).
 * Une seule transition à la fois ; voir DECISIONS D22.
 */
#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { KG_SLIDE_LEFT, KG_SLIDE_RIGHT, KG_SLIDE_UP, KG_SLIDE_DOWN } kg_slide_t;

/* Glisse de l'écran actif vers `to` (le nouvel écran entre par le côté opposé à `dir`).
 * Retourne faux si une transition est déjà en cours (rien n'est fait). Sans mémoire
 * pour les instantanés, charge `to` directement. */
bool kg_transition(lv_obj_t *to, kg_slide_t dir);

bool kg_transition_running(void);

#ifdef __cplusplus
}
#endif
