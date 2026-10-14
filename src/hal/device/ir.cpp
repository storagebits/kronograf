/*
 * Infrared transmitter (IR LED on GPIO 13, V1/V3). The carrier (36–40 kHz, 33 % duty) is
 * an LEDC PWM channel; marks and spaces switch its duty on and off against an absolute
 * µs schedule (no drift). The RMT driver would be the textbook choice, but its ISR does not
 * fit in the remaining IRAM. A send blocks the UI for the frame (≈70 ms NEC, 3 × 45 ms Sony),
 * short enough for a key press; the Wi-Fi and BLE tasks run on the other core.
 */
#include "../hal.h"
#include "board.h"
#include "hal_device.h"
#include <Arduino.h>
#include <esp_timer.h>

#ifdef PIN_IR
static constexpr int IR_CHANNEL = 4; /* high-speed timer 2; the backlight uses channel 0 (timer 0) */
static constexpr int IR_BITS = 8;
static bool s_ready;
static uint32_t s_freq, s_sent;

bool hal_ir_available(void) { return hal_inventory()->ir_tx; }

void hal_ir_send(kg_ir_proto_t proto, uint32_t code)
{
    kg_ir_frame_t f;
    if (!hal_ir_available() || !kg_ir_encode(proto, code, &f)) return;
    if (!s_ready || s_freq != f.carrier_hz) {
        ledcSetup(IR_CHANNEL, f.carrier_hz, IR_BITS);
        if (!s_ready) ledcAttachPin(PIN_IR, IR_CHANNEL);
        s_ready = true;
        s_freq = f.carrier_hz;
    }
    const uint32_t on = (1u << IR_BITS) / 3; /* 33 % duty */
    for (int r = 0; r < f.repeats; r++) {
        int64_t t = esp_timer_get_time(), start = t;
        for (int i = 0; i < f.n; i++) {
            ledcWrite(IR_CHANNEL, (i & 1) ? 0 : on);
            t += f.us[i];
            while (esp_timer_get_time() < t) {
            }
        }
        ledcWrite(IR_CHANNEL, 0);
        if (r + 1 < f.repeats) { /* next frame f.frame_ms after this one started */
            int64_t next = start + (int64_t)f.frame_ms * 1000;
            while (esp_timer_get_time() < next) {
            }
        }
    }
    s_sent++;
}

void ir_print(Print &out)
{
    out.printf("ir: GPIO %d, LEDC channel %d, %s, %lu codes sent\n", PIN_IR, IR_CHANNEL, s_ready ? "ready" : "idle",
               (unsigned long)s_sent);
}
#else
bool hal_ir_available(void) { return false; }
void hal_ir_send(kg_ir_proto_t, uint32_t) {}
void ir_print(Print &out) { out.println("ir: no IR LED on this board"); }
#endif
