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

/* Cadran « Circuits temporels » : afficheurs 7 segments */
#define KG_C_SEG_RED     lv_color_hex(0xFF2E1F)   /* destination */
#define KG_C_SEG_GREEN   lv_color_hex(0x32FF6E)   /* présent */
#define KG_C_SEG_AMBER   lv_color_hex(0xFFA41C)   /* dernier départ */
#define KG_SEG_OFF_MIX   18                       /* intensité des segments éteints (/255) */
#define KG_C_BAND        lv_color_hex(0x9C978C)   /* bandeau métal brossé */
#define KG_C_BAND_TEXT   lv_color_hex(0x15140F)
#define KG_C_PANEL       lv_color_hex(0x060606)   /* fond des fenêtres d'afficheur */
#define KG_C_PANEL_EDGE  lv_color_hex(0x2E2C28)

/* Cadran « Nuit » */
#define KG_C_NIGHT       lv_color_hex(0x9A140C)   /* rouge sombre, préserve la vision nocturne */
#define KG_C_NIGHT_DIM   lv_color_hex(0x3A0806)
#define KG_NIGHT_BRIGHTNESS_PCT 25

/* Apps */
#define KG_C_BTN         lv_color_hex(0x1C1F23)   /* bouton neutre */
#define KG_C_BTN_PRESS   lv_color_hex(0x2C3036)
#define KG_C_ACCENT      KG_C_SECONDS             /* action principale : orange aviation */
#define KG_C_ALERT_BG    lv_color_hex(0x2A0A04)   /* fond de l'écran d'alerte */
#define KG_STEPS_GOAL    10000

/* Animations (Tranche 7 : harmonisées ici) */
#define KG_ANIM_FACE_MS  280                      /* transition entre cadrans */

/* Mise en page */
#define KG_GUTTER 12
