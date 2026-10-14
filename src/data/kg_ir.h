/*
 * Infrared TV codes: pulse trains for the NEC, Samsung, Sony SIRC and Philips RC5
 * protocols, and the common keys of a few TV brands. The watch turns the train into RMT
 * items; the host tests check the timings.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { KG_IR_NEC, KG_IR_SAMSUNG, KG_IR_SONY12, KG_IR_RC5 } kg_ir_proto_t;

typedef struct {
    uint32_t carrier_hz;
    uint16_t frame_ms;   /* start-to-start period when the frame is repeated */
    uint8_t repeats;     /* frames to send (Sony expects 3) */
    uint16_t n;          /* durations used */
    uint16_t us[72];     /* mark, space, mark… in microseconds, starting with a mark */
} kg_ir_frame_t;

/* NEC / Samsung: 32 bits, MSB first as commonly written (LG power = 0x20DF10EF).
 * Sony: 12 bits (power = 0xA90). RC5: address << 6 | command, bit 11 = toggle. */
bool kg_ir_encode(kg_ir_proto_t proto, uint32_t code, kg_ir_frame_t *out);

typedef enum { KG_KEY_POWER, KG_KEY_VOL_UP, KG_KEY_VOL_DOWN, KG_KEY_MUTE, KG_KEY_CH_UP, KG_KEY_CH_DOWN, KG_KEY_INPUT, KG_N_KEYS } kg_ir_key_t;

typedef struct {
    const char *name;
    kg_ir_proto_t proto;
    uint32_t keys[KG_N_KEYS];
} kg_ir_brand_t;

extern const kg_ir_brand_t KG_IR_BRANDS[];
extern const int KG_IR_N_BRANDS;

#ifdef __cplusplus
}
#endif
