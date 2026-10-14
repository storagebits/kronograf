/*
 * Streaming media, plain C shared by the watch and the simulator:
 *   - Radio: MP3 over HTTP (Icecast / SHOUTcast), ICY metadata for the song title, a
 *     compressed ring buffer between the network and the decoder, mono output with volume.
 *   - Video: MJPEG over HTTP (multipart stream or a single JPEG refreshed), decoded and
 *     fitted into 240 × 240 RGB565, triple-buffered for the UI.
 * The platform runs the loops in its own threads and provides the sink and allocator.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*lock)(void);
    void (*unlock)(void);
    void *(*alloc)(size_t bytes);           /* large buffers (PSRAM on the watch), zeroed */
    void (*release)(void *p);
    void (*sleep_ms)(uint32_t ms);
    bool (*link_up)(void);
    /* Audio sink: 16-bit mono. write blocks until the samples are queued (paces the decoder). */
    bool (*pcm_open)(uint32_t rate);
    void (*pcm_write)(const int16_t *samples, size_t n);
    void (*pcm_close)(void);
} kg_media_env_t;

/* ------------------------------------------------------------- ring buffer */

/* Single producer, single consumer byte ring (one thread each side, no lock). */
typedef struct {
    uint8_t *buf;
    uint32_t size;
    volatile uint32_t head, tail; /* head: written, tail: read; both grow forever */
} kg_ring_t;

uint32_t kg_ring_used(const kg_ring_t *r);
uint32_t kg_ring_write(kg_ring_t *r, const uint8_t *d, uint32_t n);
uint32_t kg_ring_read(kg_ring_t *r, uint8_t *d, uint32_t n);

/* --------------------------------------------------------- ICY metadata */

/* Removes the metadata blocks inserted every `metaint` bytes and keeps the StreamTitle. */
typedef struct {
    uint32_t metaint, until_meta;
    int meta_left;       /* bytes of the current metadata block still to read */
    uint16_t meta_len;
    char meta[512];
    char title[96];
    bool title_changed;
} kg_icy_t;

void kg_icy_init(kg_icy_t *icy, uint32_t metaint);
/* Strips metadata in place; returns the number of audio bytes left at the start of d. */
size_t kg_icy_feed(kg_icy_t *icy, uint8_t *d, size_t n);

/* ------------------------------------------------------------------- radio */

typedef struct {
    const char *name;
    const char *genre;
    const char *url;
} kg_station_t;

/* Built-in stations: international, MP3 over plain HTTP. */
extern const kg_station_t KG_STATIONS[];
extern const int KG_N_STATIONS;

typedef enum { KG_RADIO_OFF, KG_RADIO_CONNECTING, KG_RADIO_BUFFERING, KG_RADIO_PLAYING, KG_RADIO_ERROR } kg_radio_state_t;

typedef struct {
    kg_radio_state_t state;
    char station[32];
    char title[96];       /* ICY StreamTitle, "" if the station sends none */
    uint16_t kbps;
    uint32_t rate;        /* Hz */
    uint8_t buffer_pct;
    uint8_t volume;       /* 0..KG_RADIO_VOL_MAX */
    uint32_t underruns;
    char error[48];
} kg_radio_status_t;

#define KG_RADIO_VOL_MAX 20

typedef struct {
    kg_radio_status_t st;  /* under env->lock */
    char url[256];
    volatile bool cancel;
    volatile bool net_done;
    kg_ring_t ring;
    kg_icy_t icy;
    uint8_t chunk[1460];   /* network → ring staging, off the network task's stack */
    const kg_media_env_t *env;
} kg_radio_t;

/* Prepares a radio session (allocates the ring); false without memory. */
bool kg_radio_init(kg_radio_t *r, const kg_media_env_t *env, const char *name, const char *url, uint8_t volume);
/* Network side: fetches the stream into the ring, reconnects on drops, until cancel. */
void kg_radio_net_loop(kg_radio_t *r);
/* Decoder side: MP3 → mono PCM to the sink, until cancel. */
void kg_radio_play_loop(kg_radio_t *r);
void kg_radio_free(kg_radio_t *r);

/* ------------------------------------------------------------------- video */

#define KG_VIDEO_W 240
#define KG_VIDEO_H 240

/* Pulls whole JPEG images out of a byte stream (multipart MJPEG or a bare JPEG). */
typedef struct {
    uint8_t *buf;
    size_t cap, len;
    bool in_frame, overflow;
    uint8_t prev;
} kg_mjpeg_t;

/* Consumes bytes up to the end of a frame; returns true when buf/len hold a whole JPEG
 * (*used = bytes consumed). Call again with the rest. */
bool kg_mjpeg_feed(kg_mjpeg_t *m, const uint8_t *d, size_t n, size_t *used);

/* Decodes a JPEG into out (KG_VIDEO_W × KG_VIDEO_H RGB565), scaled by 1/1…1/8 to fit and
 * centred (cropped or letterboxed). swap: byte-swapped RGB565 (LVGL on the watch). `work`:
 * KG_JPEG_WORK bytes. Returns false on a bad image; *w, *h get the source size. */
#define KG_JPEG_WORK 5200
bool kg_jpeg_decode(const uint8_t *jpg, size_t len, uint16_t *out, bool swap, void *work, uint16_t *w, uint16_t *h);

typedef enum { KG_VIDEO_OFF, KG_VIDEO_CONNECTING, KG_VIDEO_PLAYING, KG_VIDEO_ERROR } kg_video_state_t;

typedef struct {
    kg_video_state_t state;
    float fps;
    uint32_t frames, kbps;
    uint16_t src_w, src_h;
    char error[48];
} kg_video_status_t;

typedef struct {
    kg_video_status_t st;  /* under env->lock */
    char url[256];
    volatile bool cancel;
    bool swap;
    kg_mjpeg_t mj;
    uint16_t *fb[3];
    int front, busy, back;
    uint32_t seq;
    void *work;
    const kg_media_env_t *env;
    /* statistics */
    int64_t t_window;
    uint32_t frames_window, bytes_window;
} kg_video_t;

bool kg_video_init(kg_video_t *v, const kg_media_env_t *env, const char *url, bool swap);
/* Fetches and decodes until cancel; reconnects, and refreshes single-JPEG URLs every second. */
void kg_video_loop(kg_video_t *v);
/* Latest frame for the UI (stays valid until the next call), NULL if none; *seq changes
 * with every new frame. */
const uint16_t *kg_video_take(kg_video_t *v, uint32_t *seq);
void kg_video_free(kg_video_t *v);

#ifdef __cplusplus
}
#endif
