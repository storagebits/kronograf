#include "kg_ir.h"
#include <string.h>

const kg_ir_brand_t KG_IR_BRANDS[] = {
    /*                         power       vol+        vol−        mute        ch+         ch−         input */
    {"Samsung", KG_IR_SAMSUNG, {0xE0E040BF, 0xE0E0E01F, 0xE0E0D02F, 0xE0E0F00F, 0xE0E048B7, 0xE0E008F7, 0xE0E0807F}},
    {"LG", KG_IR_NEC, {0x20DF10EF, 0x20DF40BF, 0x20DFC03F, 0x20DF906F, 0x20DF00FF, 0x20DF807F, 0x20DFD02F}},
    {"Sony", KG_IR_SONY12, {0xA90, 0x490, 0xC90, 0x290, 0x090, 0x890, 0xA50}},
    {"Philips", KG_IR_RC5, {12, 16, 17, 13, 32, 33, 56}},
};
const int KG_IR_N_BRANDS = sizeof KG_IR_BRANDS / sizeof KG_IR_BRANDS[0];

static void put(kg_ir_frame_t *f, uint16_t us)
{
    if (f->n < sizeof f->us / sizeof f->us[0]) f->us[f->n++] = us;
}

/* Pulse-distance coding (NEC family): constant mark, the space carries the bit. */
static void pulse_distance(kg_ir_frame_t *f, uint16_t hdr_mark, uint16_t hdr_space, uint32_t code)
{
    put(f, hdr_mark);
    put(f, hdr_space);
    for (int i = 31; i >= 0; i--) {
        put(f, 560);
        put(f, (code >> i & 1) ? 1690 : 560);
    }
    put(f, 560);
}

bool kg_ir_encode(kg_ir_proto_t proto, uint32_t code, kg_ir_frame_t *f)
{
    memset(f, 0, sizeof *f);
    f->repeats = 1;
    switch (proto) {
    case KG_IR_NEC:
        f->carrier_hz = 38000;
        f->frame_ms = 108;
        pulse_distance(f, 9000, 4500, code);
        return true;
    case KG_IR_SAMSUNG:
        f->carrier_hz = 38000;
        f->frame_ms = 108;
        pulse_distance(f, 4500, 4500, code);
        return true;
    case KG_IR_SONY12: /* pulse width: the mark carries the bit, MSB of the 12 first */
        f->carrier_hz = 40000;
        f->frame_ms = 45;
        f->repeats = 3;
        put(f, 2400);
        put(f, 600);
        for (int i = 11; i >= 0; i--) {
            put(f, (code >> i & 1) ? 1200 : 600);
            put(f, 600);
        }
        f->n--; /* no trailing space: the gap until the next frame follows */
        return true;
    case KG_IR_RC5: { /* Manchester, 889 µs half-bits: 1 = space then mark, 0 = mark then space */
        f->carrier_hz = 36000;
        f->frame_ms = 114;
        uint32_t cmd = code & 0x3F, addr = code >> 6 & 0x1F, toggle = code >> 11 & 1;
        uint32_t bits = 1u << 13 | 1u << 12 | toggle << 11 | addr << 6 | cmd; /* S1, S2 (field), T */
        int level = 0, run = 0; /* the line idles low: the leading space of the first 1 is dropped */
        for (int i = 13; i >= 0; i--) {
            int b = bits >> i & 1;
            int halves[2] = {b ? 0 : 1, b ? 1 : 0};
            for (int h = 0; h < 2; h++) {
                if (halves[h] == level) {
                    run++;
                } else {
                    if (run && (f->n || level == 1)) put(f, (uint16_t)(run * 889));
                    level = halves[h];
                    run = 1;
                }
            }
        }
        if (level == 1) put(f, (uint16_t)(run * 889)); /* last mark; a trailing space is idle */
        return true;
    }
    }
    return false;
}
