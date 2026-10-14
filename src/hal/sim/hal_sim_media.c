/*
 * Simulated radio and video. Demo data by default (screenshots):
 *   KG_RADIO=play        the radio playing a station, with a song title
 *   KG_VIDEO_URL=…       a camera URL; without KG_NET_REAL the frame is sim/demo-camera.jpg
 * With KG_NET_REAL=1 both run for real from the Mac, as on the watch: the same loops
 * (kg_media.c) in threads, the radio through SDL audio (try tools/mjpeg_test_server.py for
 * the video).
 */
#include "../hal.h"
#include <SDL.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static pthread_mutex_t s_mx = PTHREAD_MUTEX_INITIALIZER;
static void lock(void) { pthread_mutex_lock(&s_mx); }
static void unlock(void) { pthread_mutex_unlock(&s_mx); }
static void *zalloc(size_t n) { return calloc(1, n); }
static void nap(uint32_t ms) { usleep(ms * 1000); }
static bool up(void) { return true; }

static SDL_AudioDeviceID s_dev;
static uint32_t s_rate;

static bool pcm_open(uint32_t rate)
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return false;
    SDL_AudioSpec want = {0}, have;
    want.freq = (int)rate;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    s_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    s_rate = rate;
    if (s_dev) SDL_PauseAudioDevice(s_dev, 0);
    return s_dev != 0;
}

static void pcm_write(const int16_t *s, size_t n)
{
    if (!s_dev) return;
    while (SDL_GetQueuedAudioSize(s_dev) > s_rate / 2 * 2) usleep(10000); /* ~0.5 s queued, as DMA would block */
    SDL_QueueAudio(s_dev, s, (Uint32)(n * 2));
}

static void pcm_close(void)
{
    if (s_dev) SDL_CloseAudioDevice(s_dev);
    s_dev = 0;
}

static const kg_media_env_t ENV = {lock, unlock, zalloc, free, nap, up, pcm_open, pcm_write, pcm_close};
static bool real(void) { return getenv("KG_NET_REAL") != NULL; }

/* ------------------------------------------------------------------ radio */

static kg_radio_t s_radio;
static pthread_t s_rnet, s_rplay;
static bool s_radio_on;
static uint8_t s_volume = 12;
static char s_radio_url[256];
static kg_radio_status_t s_demo;

static void *rnet(void *a)
{
    kg_radio_net_loop(a);
    return NULL;
}
static void *rplay(void *a)
{
    kg_radio_play_loop(a);
    return NULL;
}

void hal_radio_stop(void)
{
    if (s_radio_on) {
        s_radio.cancel = true;
        pthread_join(s_rnet, NULL);
        pthread_join(s_rplay, NULL);
        kg_radio_free(&s_radio);
        s_radio_on = false;
    }
    s_demo.state = KG_RADIO_OFF;
}

void hal_radio_play(const char *name, const char *url)
{
    hal_radio_stop();
    if (!real()) { /* demo */
        memset(&s_demo, 0, sizeof s_demo);
        s_demo.state = KG_RADIO_PLAYING;
        snprintf(s_demo.station, sizeof s_demo.station, "%s", name);
        snprintf(s_demo.title, sizeof s_demo.title, "The Clock Tower Ensemble - 88 Miles per Hour");
        s_demo.kbps = 128, s_demo.rate = 44100, s_demo.buffer_pct = 78;
        return;
    }
    if (!kg_radio_init(&s_radio, &ENV, name, url, s_volume)) return;
    s_radio_on = true;
    pthread_create(&s_rnet, NULL, rnet, &s_radio);
    pthread_create(&s_rplay, NULL, rplay, &s_radio);
}

void hal_radio_status(kg_radio_status_t *out)
{
    static bool demo_started;
    if (!demo_started && getenv("KG_RADIO")) { /* screenshot: already playing on arrival */
        demo_started = true;
        hal_radio_play(KG_STATIONS[0].name, KG_STATIONS[0].url);
    }
    lock();
    *out = s_radio_on ? s_radio.st : s_demo;
    unlock();
    out->volume = s_volume;
}

void hal_radio_set_volume(uint8_t v)
{
    s_volume = v > KG_RADIO_VOL_MAX ? KG_RADIO_VOL_MAX : v;
    s_radio.st.volume = s_volume;
}
uint8_t hal_radio_volume(void) { return s_volume; }
const char *hal_radio_custom_url(void) { return s_radio_url; }
void hal_radio_set_custom_url(const char *url) { snprintf(s_radio_url, sizeof s_radio_url, "%s", url); }

/* ------------------------------------------------------------------ video */

static kg_video_t s_video;
static pthread_t s_vthread;
static bool s_video_on, s_demo_video;
static char s_video_url[256];
static uint16_t *s_demo_frame;

static void *vrun(void *a)
{
    kg_video_loop(a);
    return NULL;
}

const char *hal_video_url(void)
{
    if (!s_video_url[0] && getenv("KG_VIDEO_URL")) snprintf(s_video_url, sizeof s_video_url, "%s", getenv("KG_VIDEO_URL"));
    return s_video_url;
}
void hal_video_set_url(const char *url) { snprintf(s_video_url, sizeof s_video_url, "%s", url); }

void hal_video_stop(void)
{
    if (s_video_on) {
        s_video.cancel = true;
        pthread_join(s_vthread, NULL);
        kg_video_free(&s_video);
        s_video_on = false;
    }
    s_demo_video = false;
}

void hal_video_start(const char *url)
{
    hal_video_stop();
    if (!real()) { /* demo: one decoded test card */
        static uint8_t jpg[64 * 1024];
        FILE *f = fopen(KG_ROOT "/sim/demo-camera.jpg", "rb");
        size_t len = f ? fread(jpg, 1, sizeof jpg, f) : 0;
        if (f) fclose(f);
        static uint8_t work[KG_JPEG_WORK];
        uint16_t w, h;
        if (!s_demo_frame) s_demo_frame = malloc(KG_VIDEO_W * KG_VIDEO_H * 2);
        s_demo_video = len && kg_jpeg_decode(jpg, len, s_demo_frame, false, work, &w, &h);
        return;
    }
    if (!kg_video_init(&s_video, &ENV, url, false)) return;
    s_video_on = true;
    pthread_create(&s_vthread, NULL, vrun, &s_video);
}

void hal_video_status(kg_video_status_t *out)
{
    if (s_video_on) {
        lock();
        *out = s_video.st;
        unlock();
        return;
    }
    memset(out, 0, sizeof *out);
    if (s_demo_video) *out = (kg_video_status_t){KG_VIDEO_PLAYING, 12.4f, 372, 940, 320, 240, ""};
}

const uint16_t *hal_video_frame(uint32_t *seq)
{
    if (s_video_on) return kg_video_take(&s_video, seq);
    *seq = 1;
    return s_demo_video ? s_demo_frame : NULL;
}
