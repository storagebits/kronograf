/* Inclut secrets.h s'il existe, sinon le modèle vide : la compilation ne casse jamais. */
#pragma once
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif
#define KG_HAS_WIFI (sizeof(KG_WIFI_SSID) > 1)
