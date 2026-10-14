/* Includes secrets.h when present, otherwise the empty template: the build never breaks. */
#pragma once
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif
#define KG_HAS_WIFI (sizeof(KG_WIFI_SSID) > 1)
/* Keys added later: missing from an older secrets.h = feature disabled. */
#ifndef KG_STM_API_KEY
#define KG_STM_API_KEY ""
#endif
#ifndef KG_OTA_PASS
#define KG_OTA_PASS ""
#endif
