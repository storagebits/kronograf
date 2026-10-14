/*
 * ANCS (Apple Notification Center Service): frame decoding, attribute requests, response
 * reassembly, text clean-up for our fonts. Plain C shared by the watch and the simulator
 * (host-tested: tests/test_data.c).
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KG_ANCS_SERVICE "7905F431-B5CE-4E99-A40F-4B1E122D00D0"
#define KG_ANCS_NOTIF_SOURCE "9FBF120D-6301-42D9-8C58-25E699A21DBD"
#define KG_ANCS_CONTROL_POINT "69D1D8F3-45E1-49A8-9821-9BBDFDAAD9D9"
#define KG_ANCS_DATA_SOURCE "22EAC6E9-24D6-4BB5-BE44-B36ACE7C7BFB"

enum { KG_ANCS_ADDED = 0, KG_ANCS_MODIFIED = 1, KG_ANCS_REMOVED = 2 };
enum { KG_ANCS_FLAG_SILENT = 1, KG_ANCS_FLAG_IMPORTANT = 2, KG_ANCS_FLAG_PREEXISTING = 4 };

typedef enum {
    KG_CAT_OTHER, KG_CAT_CALL, KG_CAT_MISSED_CALL, KG_CAT_VOICEMAIL, KG_CAT_SOCIAL, KG_CAT_SCHEDULE,
    KG_CAT_EMAIL, KG_CAT_NEWS, KG_CAT_FITNESS, KG_CAT_FINANCE, KG_CAT_LOCATION, KG_CAT_ENTERTAINMENT,
} kg_notif_cat_t;

typedef struct {
    uint8_t event, flags, category, count;
    uint32_t uid;
} kg_ancs_event_t;

typedef struct {
    uint32_t uid;
    uint8_t category;
    char app[40];    /* iOS app identifier (com.apple.MobileSMS…) */
    char title[64];
    char message[192];
    char date[16];   /* "20261012T143005" */
} kg_notif_t;

/* Notification Source frame (8 bytes). */
bool kg_ancs_parse_event(const uint8_t *data, size_t len, kg_ancs_event_t *out);

/* "Get Notification Attributes" request: app, title, message, date. Returns its size. */
size_t kg_ancs_build_request(uint8_t *out, size_t cap, uint32_t uid);

/* "Perform Notification Action" request (0: positive, 1: negative — clear, decline a call). */
size_t kg_ancs_build_action(uint8_t *out, size_t cap, uint32_t uid, uint8_t action);

/* Data Source reassembly: a response may span several frames. */
typedef struct {
    uint8_t buf[512];
    size_t len;
} kg_ancs_rx_t;

void kg_ancs_rx_reset(kg_ancs_rx_t *rx);
/* Adds a frame; true once a complete response has been decoded into `out`. */
bool kg_ancs_rx_feed(kg_ancs_rx_t *rx, const uint8_t *data, size_t len, kg_notif_t *out);

/* Readable app name ("Messages", "WhatsApp"…); otherwise the identifier's last segment. */
const char *kg_ancs_app_name(const char *bundle, char *buf, size_t n);

/* UTF-8 copy keeping only characters our fonts have (Latin-1 and typographic
 * punctuation); emoji removed, never cut in the middle of a character. */
void kg_text_sanitize(char *out, size_t n, const char *in, size_t in_len);

#ifdef __cplusplus
}
#endif
