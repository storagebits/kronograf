#include "kg_media.h"
#include "kg_ancs.h"
#include "kg_net.h"
#include "../third_party/tjpgd/tjpgd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* minimp3: MP3 only, portable C, its 16 KB scratch from the platform allocator (the
 * watch's task stacks live in internal RAM). One decoder at a time. */
static void *s_mp3_scratch;
#define MINIMP3_SCRATCH s_mp3_scratch
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_IMPLEMENTATION
#include "../third_party/minimp3/minimp3.h"

/* ------------------------------------------------------------- ring buffer */

uint32_t kg_ring_used(const kg_ring_t *r) { return r->head - r->tail; }

uint32_t kg_ring_write(kg_ring_t *r, const uint8_t *d, uint32_t n)
{
    uint32_t space = r->size - (r->head - r->tail);
    if (n > space) n = space;
    for (uint32_t i = 0; i < n;) {
        uint32_t at = (r->head + i) % r->size, chunk = r->size - at < n - i ? r->size - at : n - i;
        memcpy(r->buf + at, d + i, chunk);
        i += chunk;
    }
    __sync_synchronize(); /* data before the index */
    r->head += n;
    return n;
}

uint32_t kg_ring_read(kg_ring_t *r, uint8_t *d, uint32_t n)
{
    uint32_t used = r->head - r->tail;
    __sync_synchronize();
    if (n > used) n = used;
    for (uint32_t i = 0; i < n;) {
        uint32_t at = (r->tail + i) % r->size, chunk = r->size - at < n - i ? r->size - at : n - i;
        memcpy(d + i, r->buf + at, chunk);
        i += chunk;
    }
    __sync_synchronize();
    r->tail += n;
    return n;
}

/* --------------------------------------------------------- ICY metadata */

void kg_icy_init(kg_icy_t *icy, uint32_t metaint)
{
    memset(icy, 0, sizeof *icy);
    icy->metaint = metaint;
    icy->until_meta = metaint;
    icy->meta_left = -1;
}

/* "StreamTitle='Artist - Song';StreamUrl='';" → title. */
static void icy_parse(kg_icy_t *icy)
{
    icy->meta[icy->meta_len < sizeof icy->meta ? icy->meta_len : sizeof icy->meta - 1] = 0;
    const char *p = strstr(icy->meta, "StreamTitle='");
    if (!p) return;
    p += 13;
    const char *end = strstr(p, "';");
    if (!end) end = p + strlen(p);
    /* Titles come in UTF-8 or, from older servers, Latin-1: invalid UTF-8 is read as Latin-1.
     * Then only what our fonts can draw is kept. */
    char raw[2 * sizeof icy->title], t[sizeof icy->title];
    size_t len = (size_t)(end - p) < sizeof icy->title - 1 ? (size_t)(end - p) : sizeof icy->title - 1, n = 0;
    bool utf8 = true;
    for (size_t i = 0; i < len && utf8;) {
        uint8_t c = (uint8_t)p[i];
        size_t follow = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : 9;
        utf8 = i + follow < len;
        for (size_t k = 1; utf8 && k <= follow; k++) utf8 = ((uint8_t)p[i + k] & 0xC0) == 0x80;
        i += follow + 1;
    }
    for (size_t i = 0; i < len && n + 2 < sizeof raw; i++) {
        uint8_t c = (uint8_t)p[i];
        if (utf8 || c < 0x80) raw[n++] = (char)c;
        else raw[n++] = (char)(0xC0 | c >> 6), raw[n++] = (char)(0x80 | (c & 0x3F));
    }
    kg_text_sanitize(t, sizeof t, raw, n);
    if (strcmp(t, icy->title)) {
        memcpy(icy->title, t, sizeof t);
        icy->title_changed = true;
    }
}

size_t kg_icy_feed(kg_icy_t *icy, uint8_t *d, size_t n)
{
    if (!icy->metaint) return n;
    size_t out = 0;
    for (size_t i = 0; i < n;) {
        if (icy->meta_left < 0 && icy->until_meta == 0) { /* length byte: ×16 bytes of metadata */
            icy->meta_left = d[i++] * 16;
            icy->meta_len = 0;
            if (!icy->meta_left) {
                icy->meta_left = -1;
                icy->until_meta = icy->metaint;
            }
            continue;
        }
        if (icy->meta_left > 0) {
            if (icy->meta_len < sizeof icy->meta) icy->meta[icy->meta_len++] = (char)d[i];
            i++;
            if (--icy->meta_left == 0) {
                icy_parse(icy);
                icy->meta_left = -1;
                icy->until_meta = icy->metaint;
            }
            continue;
        }
        size_t take = n - i < icy->until_meta ? n - i : icy->until_meta; /* audio */
        memmove(d + out, d + i, take);
        out += take;
        i += take;
        icy->until_meta -= (uint32_t)take;
    }
    return out;
}

/* ------------------------------------------------------------------ radio */

const kg_station_t KG_STATIONS[] = {
    {"Radio Paradise", "Eclectic · California", "http://stream.radioparadise.com/mp3-128"},
    {"BBC World Service", "News · London", "http://stream.live.vc.bbcmedia.co.uk/bbc_world_service"},
    {"KEXP", "Indie · Seattle", "http://live-mp3-128.kexp.org/kexp128.mp3"},
    {"FIP", "Eclectic · Paris", "http://icecast.radiofrance.fr/fip-midfi.mp3"},
    {"Radio Swiss Classic", "Classical · Bern", "http://stream.srg-ssr.ch/m/rsc_de/mp3_128"},
};
const int KG_N_STATIONS = sizeof KG_STATIONS / sizeof KG_STATIONS[0];

#define RING_BYTES (96 * 1024)  /* ~6 s at 128 kbit/s */
#define PREBUFFER (32 * 1024)   /* start after ~2 s */
#define REBUFFER (16 * 1024)    /* after an underrun */

static void radio_set(kg_radio_t *r, kg_radio_state_t s, const char *error)
{
    r->env->lock();
    r->st.state = s;
    snprintf(r->st.error, sizeof r->st.error, "%s", error ? error : "");
    r->env->unlock();
}

bool kg_radio_init(kg_radio_t *r, const kg_media_env_t *env, const char *name, const char *url, uint8_t volume)
{
    memset(r, 0, sizeof *r);
    r->env = env;
    snprintf(r->url, sizeof r->url, "%s", url);
    snprintf(r->st.station, sizeof r->st.station, "%s", name);
    r->st.volume = volume > KG_RADIO_VOL_MAX ? KG_RADIO_VOL_MAX : volume;
    r->st.state = KG_RADIO_CONNECTING;
    r->ring.size = RING_BYTES;
    r->ring.buf = env->alloc(RING_BYTES);
    if (!s_mp3_scratch) s_mp3_scratch = env->alloc(sizeof(mp3dec_scratch_t));
    return r->ring.buf && s_mp3_scratch;
}

void kg_radio_free(kg_radio_t *r)
{
    if (r->ring.buf) r->env->release(r->ring.buf);
    r->ring.buf = NULL;
}

static void radio_header(const char *name, const char *value, void *ctx)
{
    kg_radio_t *r = ctx;
    if (!strcasecmp(name, "icy-metaint")) kg_icy_init(&r->icy, (uint32_t)strtoul(value, NULL, 10));
    else if (!strcasecmp(name, "icy-br")) r->st.kbps = (uint16_t)strtoul(value, NULL, 10);
    else if (!strcasecmp(name, "content-type") && (strstr(value, "aac") || strstr(value, "ogg"))) {
        radio_set(r, KG_RADIO_ERROR, "Only MP3 streams are supported");
        r->cancel = true;
    }
}

static bool radio_sink(const uint8_t *data, size_t len, void *ctx)
{
    kg_radio_t *r = ctx;
    uint8_t tmp[1460];
    while (len && !r->cancel) {
        size_t n = len < sizeof tmp ? len : sizeof tmp;
        memcpy(tmp, data, n);
        data += n;
        len -= n;
        size_t audio = kg_icy_feed(&r->icy, tmp, n);
        if (r->icy.title_changed) {
            r->icy.title_changed = false;
            r->env->lock();
            memcpy(r->st.title, r->icy.title, sizeof r->st.title);
            r->env->unlock();
        }
        for (size_t done = 0; done < audio && !r->cancel;) { /* ring full: the decoder catches up */
            done += kg_ring_write(&r->ring, tmp + done, (uint32_t)(audio - done));
            if (done < audio) r->env->sleep_ms(20);
        }
    }
    return !r->cancel;
}

void kg_radio_net_loop(kg_radio_t *r)
{
    int failures = 0;
    while (!r->cancel) {
        if (!r->env->link_up()) {
            radio_set(r, KG_RADIO_CONNECTING, "Waiting for Wi-Fi");
            r->env->sleep_ms(500);
            continue;
        }
        kg_icy_init(&r->icy, 0);
        kg_http_req_t req = {.url = r->url, .headers = "Icy-MetaData: 1\r\n", .on_header = radio_header,
                             .sink = radio_sink, .ctx = r, .idle_ms = 8000, .cancel = &r->cancel};
        int st = kg_http_request(&req);
        if (r->cancel) break;
        failures++;
        char e[48];
        if (st == -6) snprintf(e, sizeof e, "Only http:// streams");
        else if (st > 0 && st != 200) snprintf(e, sizeof e, "HTTP error %d", st);
        else if (st == 200) snprintf(e, sizeof e, "Stream ended, reconnecting");
        else snprintf(e, sizeof e, "Cannot reach the station (%d)", st);
        radio_set(r, failures >= 5 ? KG_RADIO_ERROR : KG_RADIO_CONNECTING, e);
        if (failures >= 5 || st == -6) break;
        r->env->sleep_ms(2000);
    }
    r->net_done = true;
}

void kg_radio_play_loop(kg_radio_t *r)
{
    /* Decoder state and buffers (~15 KB) from the platform allocator, for the session only. */
    struct work {
        mp3dec_t dec;
        uint8_t in[4096];
        int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    } *w = r->env->alloc(sizeof *w);
    if (!w) {
        radio_set(r, KG_RADIO_ERROR, "Out of memory");
        return;
    }
    mp3dec_t *dec = &w->dec;
    uint8_t *in = w->in;
    int16_t *pcm = w->pcm;
    const size_t in_cap = sizeof w->in;
    size_t in_len = 0;
    uint32_t rate = 0;
    bool playing = false;
    mp3dec_init(dec);
    while (!r->cancel) {
        uint32_t used = kg_ring_used(&r->ring);
        r->env->lock();
        r->st.buffer_pct = (uint8_t)(used * 100 / RING_BYTES);
        r->env->unlock();
        if (!playing) { /* (re)fill before playing: Wi-Fi delivers in bursts */
            if (used < (rate ? REBUFFER : PREBUFFER) && !r->net_done) {
                if (r->st.state != KG_RADIO_CONNECTING || used) radio_set(r, KG_RADIO_BUFFERING, NULL);
                r->env->sleep_ms(50);
                continue;
            }
            if (r->net_done && !used) break;
            playing = true;
        }
        in_len += kg_ring_read(&r->ring, in + in_len, (uint32_t)(in_cap - in_len));
        mp3dec_frame_info_t info;
        int samples = mp3dec_decode_frame(dec, in, (int)in_len, pcm, &info);
        if (info.frame_bytes) {
            memmove(in, in + info.frame_bytes, in_len - (size_t)info.frame_bytes);
            in_len -= (size_t)info.frame_bytes;
        }
        if (samples > 0) {
            if ((uint32_t)info.hz != rate) {
                if (rate) r->env->pcm_close();
                rate = (uint32_t)info.hz;
                if (!r->env->pcm_open(rate)) {
                    radio_set(r, KG_RADIO_ERROR, "No audio output");
                    break;
                }
                r->env->lock();
                r->st.rate = rate;
                if (!r->st.kbps) r->st.kbps = (uint16_t)info.bitrate_kbps;
                r->env->unlock();
            }
            if (r->st.state != KG_RADIO_PLAYING) radio_set(r, KG_RADIO_PLAYING, NULL);
            /* Mono mix and volume: a quadratic curve feels even to the ear. */
            int32_t v = r->st.volume, gain = v * v * 32768 / (KG_RADIO_VOL_MAX * KG_RADIO_VOL_MAX);
            for (int i = 0; i < samples; i++) {
                int32_t s = info.channels == 2 ? ((int32_t)pcm[2 * i] + pcm[2 * i + 1]) / 2 : pcm[i];
                pcm[i] = (int16_t)(s * gain / 32768);
            }
            r->env->pcm_write(pcm, (size_t)samples);
        } else if (!info.frame_bytes && kg_ring_used(&r->ring) == 0) { /* starved */
            if (r->net_done) break;
            r->env->lock();
            r->st.underruns++;
            r->env->unlock();
            playing = false;
        }
    }
    if (rate) r->env->pcm_close();
    r->env->release(w);
    if (!r->cancel && r->st.state != KG_RADIO_ERROR) radio_set(r, KG_RADIO_ERROR, r->st.error[0] ? r->st.error : "Stream ended");
}

/* ------------------------------------------------------------------ video */

bool kg_mjpeg_feed(kg_mjpeg_t *m, const uint8_t *d, size_t n, size_t *used)
{
    for (size_t i = 0; i < n; i++) {
        uint8_t c = d[i];
        if (!m->in_frame) {
            if (m->prev == 0xFF && c == 0xD8) { /* start of image */
                m->in_frame = true;
                m->overflow = false;
                m->buf[0] = 0xFF, m->buf[1] = 0xD8;
                m->len = 2;
            }
        } else {
            if (m->len < m->cap) m->buf[m->len++] = c;
            else m->overflow = true;
            if (m->prev == 0xFF && c == 0xD9) { /* end of image */
                m->in_frame = false;
                m->prev = 0;
                if (m->overflow) continue; /* too large: dropped */
                *used = i + 1;
                return true;
            }
        }
        m->prev = c;
    }
    *used = n;
    return false;
}

typedef struct {
    const uint8_t *jpg;
    size_t len, pos;
    uint16_t *out;
    int ox, oy; /* where the scaled image's (0, 0) lands in out (negative: cropped) */
    bool swap;
} jpeg_io_t;

static size_t jpeg_in(JDEC *jd, uint8_t *buf, size_t n)
{
    jpeg_io_t *io = jd->device;
    if (n > io->len - io->pos) n = io->len - io->pos;
    if (buf) memcpy(buf, io->jpg + io->pos, n);
    io->pos += n;
    return n;
}

static int jpeg_out(JDEC *jd, void *bitmap, JRECT *rect)
{
    jpeg_io_t *io = jd->device;
    const uint16_t *px = bitmap;
    int w = rect->right - rect->left + 1;
    for (int y = rect->top; y <= rect->bottom; y++, px += w) {
        int dy = y + io->oy;
        if (dy < 0 || dy >= KG_VIDEO_H) continue;
        for (int x = 0; x < w; x++) {
            int dx = rect->left + x + io->ox;
            if (dx < 0 || dx >= KG_VIDEO_W) continue;
            uint16_t c = px[x];
            io->out[dy * KG_VIDEO_W + dx] = io->swap ? (uint16_t)(c >> 8 | c << 8) : c;
        }
    }
    return 1;
}

bool kg_jpeg_decode(const uint8_t *jpg, size_t len, uint16_t *out, bool swap, void *work, uint16_t *w, uint16_t *h)
{
    JDEC jd;
    jpeg_io_t io = {jpg, len, 0, out, 0, 0, swap};
    if (jd_prepare(&jd, jpeg_in, work, KG_JPEG_WORK, &io) != JDR_OK) return false;
    *w = jd.width, *h = jd.height;
    /* Largest image that is at most 4/3 of the screen: a slight crop beats a thumbnail. */
    uint8_t scale = 0;
    while (scale < 3 && ((jd.width >> scale) > KG_VIDEO_W * 4 / 3 || (jd.height >> scale) > KG_VIDEO_H * 4 / 3)) scale++;
    int sw = jd.width >> scale, sh = jd.height >> scale;
    io.ox = (KG_VIDEO_W - sw) / 2;
    io.oy = (KG_VIDEO_H - sh) / 2;
    if (sw < KG_VIDEO_W || sh < KG_VIDEO_H) memset(out, 0, KG_VIDEO_W * KG_VIDEO_H * 2); /* letterbox */
    return jd_decomp(&jd, jpeg_out, scale) == JDR_OK;
}

static void video_set(kg_video_t *v, kg_video_state_t s, const char *error)
{
    v->env->lock();
    v->st.state = s;
    snprintf(v->st.error, sizeof v->st.error, "%s", error ? error : "");
    v->env->unlock();
}

bool kg_video_init(kg_video_t *v, const kg_media_env_t *env, const char *url, bool swap)
{
    memset(v, 0, sizeof *v);
    v->env = env;
    v->swap = swap;
    snprintf(v->url, sizeof v->url, "%s", url);
    v->st.state = KG_VIDEO_CONNECTING;
    v->mj.cap = 160 * 1024;
    v->mj.buf = env->alloc(v->mj.cap);
    v->work = env->alloc(KG_JPEG_WORK);
    for (int i = 0; i < 3; i++) v->fb[i] = env->alloc(KG_VIDEO_W * KG_VIDEO_H * 2);
    v->front = v->busy = -1;
    return v->mj.buf && v->work && v->fb[0] && v->fb[1] && v->fb[2];
}

void kg_video_free(kg_video_t *v)
{
    v->env->release(v->mj.buf);
    v->env->release(v->work);
    for (int i = 0; i < 3; i++) v->env->release(v->fb[i]);
    memset(v->fb, 0, sizeof v->fb);
}

static bool video_sink(const uint8_t *d, size_t n, void *ctx)
{
    kg_video_t *v = ctx;
    v->bytes_window += (uint32_t)n;
    while (n && !v->cancel) {
        size_t used;
        if (kg_mjpeg_feed(&v->mj, d, n, &used)) {
            /* Decode into the buffer that is neither shown nor held by the UI. */
            v->env->lock();
            int back = 0;
            while (back == v->front || back == v->busy) back++;
            v->env->unlock();
            uint16_t w, h;
            if (kg_jpeg_decode(v->mj.buf, v->mj.len, v->fb[back], v->swap, v->work, &w, &h)) {
                int64_t now = kg_net_us();
                v->frames_window++;
                v->env->lock();
                v->front = back;
                v->seq++;
                v->st.frames++;
                v->st.src_w = w, v->st.src_h = h;
                v->st.state = KG_VIDEO_PLAYING;
                if (now - v->t_window >= 1000000) {
                    float s = (float)(now - v->t_window) / 1e6f;
                    v->st.fps = v->frames_window / s;
                    v->st.kbps = (uint32_t)(v->bytes_window * 8 / s / 1000);
                    v->t_window = now;
                    v->frames_window = v->bytes_window = 0;
                }
                v->env->unlock();
            }
        }
        d += used;
        n -= used;
    }
    return !v->cancel;
}

void kg_video_loop(kg_video_t *v)
{
    int failures = 0;
    v->t_window = kg_net_us();
    while (!v->cancel) {
        if (!v->env->link_up()) {
            video_set(v, KG_VIDEO_CONNECTING, "Waiting for Wi-Fi");
            v->env->sleep_ms(500);
            continue;
        }
        uint32_t frames = v->st.frames;
        kg_http_req_t req = {.url = v->url, .sink = video_sink, .ctx = v, .idle_ms = 8000, .cancel = &v->cancel};
        int st = kg_http_request(&req);
        if (v->cancel) break;
        if (st == 200 && v->st.frames > frames) { /* a single JPEG (snapshot URL): poll it */
            failures = 0;
            v->env->sleep_ms(1000);
            continue;
        }
        char e[48];
        if (st == -6) snprintf(e, sizeof e, "Only http:// streams");
        else if (st > 0 && st != 200) snprintf(e, sizeof e, "HTTP error %d", st);
        else if (st == 200) snprintf(e, sizeof e, "No JPEG in the stream");
        else snprintf(e, sizeof e, "Cannot reach the camera (%d)", st);
        video_set(v, ++failures >= 5 || st == -6 ? KG_VIDEO_ERROR : KG_VIDEO_CONNECTING, e);
        if (failures >= 5 || st == -6) break;
        v->env->sleep_ms(2000);
    }
}

const uint16_t *kg_video_take(kg_video_t *v, uint32_t *seq)
{
    v->env->lock();
    v->busy = v->front;
    *seq = v->seq;
    const uint16_t *f = v->front >= 0 ? v->fb[v->front] : NULL;
    v->env->unlock();
    return f;
}
