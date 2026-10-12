/* Composants partagés des apps : même style partout, couleurs via theme.h. */
#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Écran noir sans défilement. */
lv_obj_t *kit_screen(void);

/* Titre au pochoir en haut d'écran, dans une couleur d'accent. */
lv_obj_t *kit_title(lv_obj_t *scr, const char *text);

/* Bouton arrondi avec texte ; `primary` = action principale (orange). */
lv_obj_t *kit_button(lv_obj_t *parent, const char *text, bool primary, lv_coord_t w, lv_coord_t h,
                     lv_event_cb_t cb, void *user);
void kit_button_set_text(lv_obj_t *btn, const char *text);

/* Libellé simple. */
lv_obj_t *kit_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text);

/* Retour haptique court commun à toutes les touches. */
void kit_tick(void);

#ifdef __cplusplus
}
#endif
