/* Fonctions propres à la cible montre (non vues par l'UI). */
#pragma once
#include <stdint.h>

/* Fuseau America/Toronto avec règles DST nord-américaines. */
#define KG_TZ "EST5EDT,M3.2.0,M11.1.0"

void hal_device_init(void);

void display_init(void);
float display_fps(void);

void console_poll(void);
