/*
 * Jetons de thème : les écrans et cadrans ne codent jamais une couleur en dur,
 * ils passent par ces jetons.
 */
#pragma once
#include "lvgl.h"

#define KG_W 240
#define KG_H 240

/* Fonds */
#define KG_C_BG        lv_color_hex(0x000000)
#define KG_C_SURFACE   lv_color_hex(0x101214)
#define KG_C_HAIRLINE  lv_color_hex(0x2A2E33)

/* Texte */
#define KG_C_TEXT      lv_color_hex(0xECE6D8)   /* blanc cassé « cadran » */
#define KG_C_TEXT_DIM  lv_color_hex(0x7D7A72)

/* Accents */
#define KG_C_LUME      lv_color_hex(0xC9F2D0)   /* luminescent vert pâle */
#define KG_C_OK        lv_color_hex(0x3FD47A)   /* vert afficheur */
#define KG_C_WARN      lv_color_hex(0xFFB020)   /* ambre afficheur */
#define KG_C_ALERT     lv_color_hex(0xFF3B30)   /* rouge afficheur */

/* Mise en page */
#define KG_GUTTER 12
