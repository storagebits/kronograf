/*
 * Modèle des secrets. Copier en include/secrets.h (ignoré par git) et remplir.
 * Une valeur vide désactive proprement la fonctionnalité correspondante.
 */
#pragma once

#define KG_WIFI_SSID ""
#define KG_WIFI_PASS ""

/* Mode atelier (montre sur USB) : mot de passe des mises à jour OTA, facultatif mais conseillé. */
#define KG_OTA_PASS ""

/* Tranche 4 : clé API STM (GTFS temps réel), facultative. */
#define KG_STM_API_KEY ""
