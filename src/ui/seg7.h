/*
 * Afficheur 7 segments dessiné en code (polygones anti-crénelés) :
 * segments éteints visibles en faible intensité, halo sur les segments allumés,
 * scintillement bref quand un segment s'allume.
 */
#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Crée un groupe de `ndigits` chiffres de taille digit_w × digit_h.
 * colon_after : index du chiffre suivi d'un « : » (‑1 si aucun). */
lv_obj_t *seg7_create(lv_obj_t *parent, int ndigits, lv_coord_t digit_w, lv_coord_t digit_h,
                      int colon_after, lv_color_t color);

/* Texte : chiffres 0-9, « - » et « » (espace = tout éteint). Longueur = ndigits. */
void seg7_set_text(lv_obj_t *seg, const char *text);

/* « : » allumé ou éteint (clignotement à la seconde). */
void seg7_set_colon(lv_obj_t *seg, bool on);

#ifdef __cplusplus
}
#endif
