/*
 * Radio and video on the watch: the portable loops of kg_media.c in FreeRTOS tasks, the
 * speaker stream of feedback.cpp, buffers in PSRAM, URLs in NVS ("kgmedia").
 *   Radio: network task (core 0, prio 2) → 96 KB ring → decoder task (core 0, prio 3) → I2S.
 *          Wi-Fi is kept on while it plays (net_keep), screen off included.
 *   Video: one task (core 0, prio 1) fetching and decoding into three 240 × 240 frames.
 * Stopping cancels the loops; the last task out frees the session.
 */
#include "../hal.h"
#include "hal_device.h"
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

static SemaphoreHandle_t s_mx;
static void lock(void) { xSemaphoreTake(s_mx, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_mx); }
static void *ps_alloc(size_t n) { return heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM); }
static void ps_free(void *p) { free(p); }
static void nap(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }
static bool link_up(void)
{
    if (WiFi.status() == WL_CONNECTED) return true;
    hal_net_hold(); /* (re)connects; the radio's keep or the video app's lease keeps it up */
    return false;
}
static const kg_media_env_t ENV = {lock, unlock, ps_alloc, ps_free, nap, link_up,
                                   audio_stream_open, audio_stream_write, audio_stream_close};

static void ensure_mutex(void)
{
    if (!s_mx) s_mx = xSemaphoreCreateMutex();
}

/* ---------------------------------------------------------------- settings */

static char s_radio_url[256], s_video_url[256];
static bool s_loaded;
static uint8_t s_volume = 10;

static void load(void)
{
    if (s_loaded) return;
    s_loaded = true;
    Preferences p;
    p.begin("kgmedia", true);
    snprintf(s_radio_url, sizeof s_radio_url, "%s", p.getString("radio", "").c_str());
    snprintf(s_video_url, sizeof s_video_url, "%s", p.getString("video", "").c_str());
    s_volume = (uint8_t)p.getUChar("vol", 10);
    p.end();
}

static void save_str(const char *key, const char *v)
{
    Preferences p;
    p.begin("kgmedia", false);
    if (v[0]) p.putString(key, v);
    else p.remove(key);
    p.end();
}

const char *hal_radio_custom_url(void) { return load(), s_radio_url; }
const char *hal_video_url(void) { return load(), s_video_url; }

void hal_radio_set_custom_url(const char *url)
{
    load();
    snprintf(s_radio_url, sizeof s_radio_url, "%s", url);
    save_str("radio", s_radio_url);
}

void hal_video_set_url(const char *url)
{
    load();
    snprintf(s_video_url, sizeof s_video_url, "%s", url);
    save_str("video", s_video_url);
}

/* ------------------------------------------------------------------- radio */

static kg_radio_t *s_radio;        /* session in PSRAM */
static volatile int s_radio_tasks; /* running tasks of the session */
static kg_radio_status_t s_radio_last; /* status once the session is gone */
static TaskHandle_t s_radio_net_h, s_radio_mp3_h;

static void radio_task_done(void)
{
    lock();
    bool last = --s_radio_tasks == 0;
    if (last) {
        s_radio_last = s_radio->st;
        if (s_radio->cancel) s_radio_last.state = KG_RADIO_OFF;
    }
    unlock();
    if (last) {
        kg_radio_free(s_radio);
        net_keep(false);
    }
}

static void radio_net_task(void *)
{
    kg_radio_net_loop(s_radio);
    radio_task_done();
    vTaskDelete(nullptr);
}

static void radio_play_task(void *)
{
    kg_radio_play_loop(s_radio);
    s_radio->cancel = s_radio->cancel || s_radio->st.state == KG_RADIO_ERROR; /* the network side stops too */
    radio_task_done();
    vTaskDelete(nullptr);
}

static bool radio_wait_stopped(uint32_t ms)
{
    for (uint32_t t = 0; s_radio_tasks && t < ms; t += 20) vTaskDelay(pdMS_TO_TICKS(20));
    return !s_radio_tasks;
}

void hal_radio_stop(void)
{
    if (s_radio && s_radio_tasks) s_radio->cancel = true;
}

void hal_radio_play(const char *name, const char *url)
{
    ensure_mutex();
    load();
    hal_radio_stop();
    if (!radio_wait_stopped(2000)) return; /* still tearing down: the user can retry */
    if (!s_radio) s_radio = (kg_radio_t *)ps_alloc(sizeof *s_radio);
    if (!s_radio || !hal_sound_available()) return;
    if (!kg_radio_init(s_radio, &ENV, name, url, s_volume)) {
        kg_radio_free(s_radio);
        snprintf(s_radio_last.error, sizeof s_radio_last.error, "Out of memory");
        s_radio_last.state = KG_RADIO_ERROR;
        return;
    }
    net_keep(true);
    s_radio_tasks = 2;
    /* ~6.2 KB measured at peak (DNS, redirects): 7 KB leaves a safe margin. */
    xTaskCreatePinnedToCore(radio_net_task, "kg_radio_net", 7168, nullptr, 2, &s_radio_net_h, 0);
    /* Measured on the watch: the decoder peaks at ~1.5 KB of stack (its buffers are in PSRAM). */
    xTaskCreatePinnedToCore(radio_play_task, "kg_radio_mp3", 3584, nullptr, 3, &s_radio_mp3_h, 0);
}

void hal_radio_status(kg_radio_status_t *out)
{
    ensure_mutex();
    load();
    lock();
    if (s_radio && s_radio_tasks) *out = s_radio->st;
    else *out = s_radio_last;
    unlock();
    out->volume = s_volume;
}

void hal_radio_set_volume(uint8_t v)
{
    load();
    s_volume = v > KG_RADIO_VOL_MAX ? KG_RADIO_VOL_MAX : v;
    if (s_radio && s_radio_tasks) s_radio->st.volume = s_volume; /* read by the decoder on each frame */
    Preferences p;
    p.begin("kgmedia", false);
    p.putUChar("vol", s_volume);
    p.end();
}

uint8_t hal_radio_volume(void) { return load(), s_volume; }

/* ------------------------------------------------------------------- video */

static kg_video_t *s_video;
static volatile bool s_video_task;
static kg_video_status_t s_video_last;

static void video_task(void *)
{
    kg_video_loop(s_video);
    lock();
    s_video_last = s_video->st;
    if (s_video->cancel) s_video_last.state = KG_VIDEO_OFF;
    s_video_task = false;
    unlock();
    kg_video_free(s_video);
    vTaskDelete(nullptr);
}

void hal_video_stop(void)
{
    if (s_video && s_video_task) s_video->cancel = true;
}

void hal_video_start(const char *url)
{
    ensure_mutex();
    hal_video_stop();
    for (int t = 0; s_video_task && t < 100; t++) vTaskDelay(pdMS_TO_TICKS(20));
    if (s_video_task) return;
    if (!s_video) s_video = (kg_video_t *)ps_alloc(sizeof *s_video);
    if (!s_video) return;
    /* LVGL on the watch stores RGB565 byte-swapped (LV_COLOR_16_SWAP) for the SPI display. */
    if (!kg_video_init(s_video, &ENV, url, true)) {
        kg_video_free(s_video);
        snprintf(s_video_last.error, sizeof s_video_last.error, "Out of memory");
        s_video_last.state = KG_VIDEO_ERROR;
        return;
    }
    s_video_task = true;
    xTaskCreatePinnedToCore(video_task, "kg_video", 6144, nullptr, 1, nullptr, 0);
}

void hal_video_status(kg_video_status_t *out)
{
    ensure_mutex();
    lock();
    *out = s_video && s_video_task ? s_video->st : s_video_last;
    unlock();
}

const uint16_t *hal_video_frame(uint32_t *seq)
{
    if (!s_video || !s_video_task) return nullptr;
    return kg_video_take(s_video, seq);
}

/* Console: one line of radio / video state. */
void media_print(Print &out)
{
    kg_radio_status_t r;
    hal_radio_status(&r);
    static const char *const RS[] = {"off", "connecting", "buffering", "playing", "error"};
    if (s_radio_tasks == 2)
        out.printf("radio stacks free: net %u, mp3 %u bytes\n", (unsigned)uxTaskGetStackHighWaterMark(s_radio_net_h),
                   (unsigned)uxTaskGetStackHighWaterMark(s_radio_mp3_h));
    out.printf("radio: %s \"%s\" %u kbps %lu Hz buffer %u %% vol %u underruns %lu title \"%s\" %s\n", RS[r.state],
               r.station, r.kbps, (unsigned long)r.rate, r.buffer_pct, r.volume, (unsigned long)r.underruns, r.title,
               r.error);
    kg_video_status_t v;
    hal_video_status(&v);
    static const char *const VS[] = {"off", "connecting", "playing", "error"};
    out.printf("video: %s %lu frames %.1f fps %lu kbit/s %ux%u %s\n", VS[v.state], (unsigned long)v.frames, v.fps,
               (unsigned long)v.kbps, v.src_w, v.src_h, v.error);
}
