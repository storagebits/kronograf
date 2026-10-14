/*
 * Theme tokens: screens and faces never hard-code a colour, they go through
 * these tokens.
 */
#pragma once
#include "lvgl.h"

#define KG_W 240
#define KG_H 240

/* Backgrounds */
#define KG_C_BG        lv_color_hex(0x000000)
#define KG_C_SURFACE   lv_color_hex(0x101214)
#define KG_C_HAIRLINE  lv_color_hex(0x2A2E33)

/* Text */
#define KG_C_TEXT      lv_color_hex(0xECE6D8)   /* off-white "dial" */
#define KG_C_TEXT_DIM  lv_color_hex(0x7D7A72)

/* Accents */
#define KG_C_LUME      lv_color_hex(0xC9F2D0)   /* pale green lume */
#define KG_C_OK        lv_color_hex(0x3FD47A)   /* display green */
#define KG_C_WARN      lv_color_hex(0xFFB020)   /* display amber */
#define KG_C_ALERT     lv_color_hex(0xFF3B30)   /* display red */
#define KG_C_RAIN      lv_color_hex(0x5FA8FF)   /* raindrops */

/* "Aviator chronograph" face */
#define KG_C_DIAL        lv_color_hex(0x0B0D10)   /* dial, bluish black */
#define KG_C_GUILLOCHE   lv_color_hex(0x1A1E24)   /* guilloché ridges */
#define KG_C_BEZEL       lv_color_hex(0x15171A)   /* slide-rule bezel */
#define KG_C_BEZEL_MARK  lv_color_hex(0xB9B2A3)   /* bezel graduations */
#define KG_C_TRACK       lv_color_hex(0x8A857A)   /* minute railway track */
#define KG_C_SUBDIAL     lv_color_hex(0x07080A)   /* recessed sub-dials */
#define KG_C_HAND        lv_color_hex(0xE8E2D2)   /* hands, applied numerals */
#define KG_C_HAND_EDGE   lv_color_hex(0x2B2E33)   /* hand outline */
#define KG_C_SECONDS     lv_color_hex(0xFF5A1F)   /* aviation-orange seconds hand */
#define KG_C_DATE_BG     lv_color_hex(0xECE6D8)
#define KG_C_DATE_FG     lv_color_hex(0x111111)

/* "Time circuits" face: 7-segment displays */
#define KG_C_SEG_RED     lv_color_hex(0xFF2E1F)   /* destination time */
#define KG_C_SEG_GREEN   lv_color_hex(0x32FF6E)   /* present time */
#define KG_C_SEG_AMBER   lv_color_hex(0xFFA41C)   /* last time departed */
#define KG_SEG_OFF_MIX   18                       /* unlit segment intensity (/255) */
#define KG_C_BAND        lv_color_hex(0x9C978C)   /* brushed-metal label band */
#define KG_C_BAND_TEXT   lv_color_hex(0x15140F)
#define KG_C_PANEL       lv_color_hex(0x060606)   /* display window background */
#define KG_C_PANEL_EDGE  lv_color_hex(0x2E2C28)

/* "Night" face */
#define KG_C_NIGHT       lv_color_hex(0x9A140C)   /* dark red, preserves night vision */
#define KG_C_NIGHT_DIM   lv_color_hex(0x3A0806)
#define KG_NIGHT_BRIGHTNESS_PCT 25

/* Apps */
#define KG_C_BTN         lv_color_hex(0x1C1F23)   /* neutral button */
#define KG_C_BTN_PRESS   lv_color_hex(0x2C3036)
#define KG_C_ACCENT      KG_C_SECONDS             /* primary action: aviation orange */
#define KG_C_ALERT_BG    lv_color_hex(0x2A0A04)   /* alert screen background */
#define KG_STEPS_GOAL    10000

/* Animations (stage 7: harmonised here) */
#define KG_ANIM_FACE_MS  280                      /* transition between faces */

/* Layout */
#define KG_GUTTER 12
