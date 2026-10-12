/* Inclut secrets.h s'il existe, sinon le modèle vide : la compilation ne casse jamais. */
#pragma once
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif
#define KG_HAS_WIFI (sizeof(KG_WIFI_SSID) > 1)
/* Clés ajoutées après coup : absentes d'un ancien secrets.h = fonctionnalité désactivée. */
#ifndef KG_STM_API_KEY
#define KG_STM_API_KEY ""
#endif
#ifndef KG_OTA_PASS
#define KG_OTA_PASS ""
#endif
