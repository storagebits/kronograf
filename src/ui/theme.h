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

/* Cadran « Chronographe aviateur » */
#define KG_C_DIAL        lv_color_hex(0x0B0D10)   /* fond de cadran, noir bleuté */
#define KG_C_GUILLOCHE   lv_color_hex(0x1A1E24)   /* crêtes du guillochage */
#define KG_C_BEZEL       lv_color_hex(0x15171A)   /* lunette règle à calcul */
#define KG_C_BEZEL_MARK  lv_color_hex(0xB9B2A3)   /* graduations de lunette */
#define KG_C_TRACK       lv_color_hex(0x8A857A)   /* chemin de fer des minutes */
#define KG_C_SUBDIAL     lv_color_hex(0x07080A)   /* fond des sous-cadrans (creusés) */
#define KG_C_HAND        lv_color_hex(0xE8E2D2)   /* aiguilles, chiffres appliqués */
#define KG_C_HAND_EDGE   lv_color_hex(0x2B2E33)   /* liseré des aiguilles */
#define KG_C_SECONDS     lv_color_hex(0xFF5A1F)   /* trotteuse orange aviation */
#define KG_C_DATE_BG     lv_color_hex(0xECE6D8)
#define KG_C_DATE_FG     lv_color_hex(0x111111)

/* Mise en page */
#define KG_GUTTER 12
