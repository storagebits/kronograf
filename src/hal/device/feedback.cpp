/*
 * Retours haptiques et sonores, non bloquants.
 *
 *   Vibreur : motifs marche/arrêt cadencés par un esp_timer (aucun delay dans la boucle).
 *   Sons    : tâche dédiée ; séquences de notes en sinus avec attaque et relâche
 *             douces (pas de clic), I²S 16 kHz vers le MAX98357A. L'ampli (LDO3)
 *             n'est alimenté que pendant la lecture.
 */
#include "../hal.h"
#include "board.h"
#include "hal_device.h"
#include <Arduino.h>
#include <driver/i2s.h>
#include <esp_timer.h>
#include <math.h>

/* ------------------------------------------------------------------ vibreur */

#define MAX_PATTERN 16
static uint16_t s_pattern[MAX_PATTERN];
static uint8_t s_pattern_n, s_pattern_i;
static esp_timer_handle_t s_vib_timer;

static void vib_step(void *)
{
#ifdef PIN_MOTOR
    if (s_pattern_i >= s_pattern_n) {
        digitalWrite(PIN_MOTOR, LOW);
        return;
    }
    digitalWrite(PIN_MOTOR, s_pattern_i % 2 == 0 ? HIGH : LOW);
    esp_timer_start_once(s_vib_timer, 1000ULL * s_pattern[s_pattern_i++]);
#endif
}

void hal_vibrate_pattern(const uint16_t *ms, uint8_t n)
{
#ifdef PIN_MOTOR
    esp_timer_stop(s_vib_timer);
    s_pattern_n = n > MAX_PATTERN ? MAX_PATTERN : n;
    memcpy(s_pattern, ms, s_pattern_n * sizeof(uint16_t));
    s_pattern_i = 0;
    vib_step(nullptr);
#else
    (void)ms;
    (void)n;
#endif
}

void hal_vibrate(uint16_t ms) { hal_vibrate_pattern(&ms, 1); }

/* -------------------------------------------------------------------- sons */

#define MAX_TONES 24
#define SAMPLE_RATE 16000
struct tone_job {
    kg_tone_t tones[MAX_TONES];
    uint8_t n;
};
static QueueHandle_t s_audio_q;
static bool s_i2s_ready;

bool hal_sound_available(void)
{
#ifdef PIN_I2S_DOUT
    return hal_inventory()->speaker;
#else
    return false;
#endif
}

#ifdef PIN_I2S_DOUT
static void i2s_start(void)
{
    if (s_i2s_ready) return;
    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate = SAMPLE_RATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.dma_buf_count = 4;
    cfg.dma_buf_len = 256;
    cfg.tx_desc_auto_clear = true; /* silence si la tâche prend du retard */
    i2s_pin_config_t pins = {};
    pins.bck_io_num = PIN_I2S_BCK;
    pins.ws_io_num = PIN_I2S_WS;
    pins.data_out_num = PIN_I2S_DOUT;
    pins.data_in_num = I2S_PIN_NO_CHANGE;
    i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
    i2s_set_pin(I2S_NUM_0, &pins);
    s_i2s_ready = true;
}

static void i2s_stop(void)
{
    if (!s_i2s_ready) return;
    i2s_driver_uninstall(I2S_NUM_0);
    s_i2s_ready = false;
}

static void play_tone(const kg_tone_t &t)
{
    static int16_t buf[256];
    uint32_t total = (uint32_t)SAMPLE_RATE * t.ms / 1000;
    uint32_t ramp = SAMPLE_RATE * 6 / 1000; /* 6 ms d'attaque et de relâche */
    float phase = 0, step = 2.0f * (float)M_PI * t.hz / SAMPLE_RATE;
    for (uint32_t done = 0; done < total;) {
        uint32_t n = total - done > 256 ? 256 : total - done;
        for (uint32_t i = 0; i < n; i++, done++) {
            float env = 1.0f;
            if (done < ramp) env = (float)done / ramp;
            else if (total - done < ramp) env = (float)(total - done) / ramp;
            buf[i] = t.hz ? (int16_t)(sinf(phase) * env * 9000) : 0;
            phase += step;
            if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
        }
        size_t written;
        i2s_write(I2S_NUM_0, buf, n * sizeof(int16_t), &written, portMAX_DELAY);
    }
}

static void audio_task(void *)
{
    tone_job job;
    for (;;) {
        if (xQueueReceive(s_audio_q, &job, portMAX_DELAY) != pdTRUE) continue;
        pmu_audio_power(true);
        vTaskDelay(pdMS_TO_TICKS(20)); /* stabilisation de l'ampli */
        i2s_start();
        do {
            for (uint8_t i = 0; i < job.n; i++) play_tone(job.tones[i]);
        } while (xQueueReceive(s_audio_q, &job, pdMS_TO_TICKS(300)) == pdTRUE);
        play_tone({0, 40}); /* vide les tampons DMA avant de couper */
        i2s_stop();
        pmu_audio_power(false);
    }
}
#endif

void hal_tones(const kg_tone_t *tones, uint8_t n)
{
#ifdef PIN_I2S_DOUT
    if (!s_audio_q || !hal_sound_available()) return;
    tone_job job;
    job.n = n > MAX_TONES ? MAX_TONES : n;
    memcpy(job.tones, tones, job.n * sizeof(kg_tone_t));
    xQueueSend(s_audio_q, &job, 0);
#else
    (void)tones;
    (void)n;
#endif
}

void feedback_init(void)
{
#ifdef PIN_MOTOR
    pinMode(PIN_MOTOR, OUTPUT);
    digitalWrite(PIN_MOTOR, LOW);
    esp_timer_create_args_t args = {};
    args.callback = vib_step;
    args.name = "kg_vib";
    esp_timer_create(&args, &s_vib_timer);
#endif
#ifdef PIN_I2S_DOUT
    s_audio_q = xQueueCreate(2, sizeof(tone_job));
    /* Cœur 0, priorité moyenne : le son ne doit pas hacher, l'UI reste sur le cœur 1. */
    xTaskCreatePinnedToCore(audio_task, "kg_audio", 3072, nullptr, 5, nullptr, 0);
#endif
}
