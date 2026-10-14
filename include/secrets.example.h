/*
 * Secrets template. Copy to include/secrets.h (ignored by git) and fill in.
 * An empty value cleanly disables the matching feature.
 */
#pragma once

#define KG_WIFI_SSID ""
#define KG_WIFI_PASS ""

/* Workshop mode (watch on USB): password for OTA updates, optional but recommended. */
#define KG_OTA_PASS ""

/* Optional STM API key (GTFS realtime) for bus departures. It can also be entered on the
 * watch (Settings → STM buses) or over the console (bus key <key>). */
#define KG_STM_API_KEY ""
