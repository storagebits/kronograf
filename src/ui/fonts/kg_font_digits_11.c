/*******************************************************************************
 * Size: 11 px
 * Bpp: 4
 * Opts: --font assets/fonts/BarlowCondensed-SemiBold.ttf -r 0x20,0x25,0x2B-0x3A,0x41-0x5A,0x6B --size 11 --bpp 4 --format lvgl --no-compress --lv-include lvgl.h -o src/ui/fonts/kg_font_digits_11.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef KG_FONT_DIGITS_11
#define KG_FONT_DIGITS_11 1
#endif

#if KG_FONT_DIGITS_11

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */

    /* U+0025 "%" */
    0x1b, 0xb6, 0x6, 0x70, 0x56, 0xb, 0xd, 0x0,
    0xa, 0xb4, 0x77, 0x0, 0x0, 0x0, 0xd0, 0x0,
    0x0, 0x8, 0x68, 0xb3, 0x0, 0x1d, 0x47, 0xb,
    0x0, 0x86, 0x46, 0xa, 0x1, 0xc0, 0x9, 0xa4,

    /* U+002B "+" */
    0x0, 0xe0, 0x0, 0x1e, 0x0, 0xae, 0xfe, 0x70,
    0x2e, 0x10, 0x0, 0x60, 0x0,

    /* U+002C "," */
    0x59, 0x78, 0x94,

    /* U+002D "-" */
    0xaf, 0xf4, 0x12, 0x20,

    /* U+002E "." */
    0x36, 0x6c,

    /* U+002F "/" */
    0x0, 0x9a, 0x0, 0xe5, 0x3, 0xf1, 0x8, 0xc0,
    0xc, 0x70, 0x1f, 0x30, 0x6e, 0x0, 0xa9, 0x0,

    /* U+0030 "0" */
    0xb, 0xfb, 0x7, 0xe3, 0xe6, 0x9b, 0xc, 0x89,
    0xb0, 0xc8, 0x9b, 0xc, 0x89, 0xb0, 0xc8, 0x7e,
    0x4e, 0x60, 0xbf, 0xb0,

    /* U+0031 "1" */
    0x9f, 0x86, 0xd8, 0xc, 0x80, 0xc8, 0xc, 0x80,
    0xc8, 0xc, 0x80, 0xb8,

    /* U+0032 "2" */
    0x1c, 0xf9, 0x9, 0xd5, 0xf2, 0x66, 0x1f, 0x30,
    0x7, 0xe0, 0x1, 0xe6, 0x0, 0xbc, 0x0, 0x6f,
    0x42, 0xa, 0xff, 0xf6,

    /* U+0033 "3" */
    0x9f, 0xff, 0x31, 0x2a, 0xe1, 0x4, 0xf4, 0x0,
    0x7d, 0x90, 0x0, 0x1f, 0x26, 0x50, 0xf3, 0x9c,
    0x5f, 0x22, 0xdf, 0x80,

    /* U+0034 "4" */
    0x6, 0xf0, 0x0, 0xbb, 0x0, 0xf, 0x60, 0x4,
    0xf1, 0xd5, 0x9d, 0xe, 0x5c, 0xff, 0xfd, 0x23,
    0x3f, 0x70, 0x0, 0xe5,

    /* U+0035 "5" */
    0x9f, 0xff, 0x49, 0xc2, 0x20, 0x9b, 0x0, 0x9,
    0xee, 0xc0, 0x7a, 0x4f, 0x41, 0x20, 0xe6, 0x7e,
    0x4f, 0x41, 0xcf, 0xa0,

    /* U+0036 "6" */
    0x1c, 0xf9, 0x7, 0xe5, 0xf3, 0x9b, 0x7, 0x29,
    0xee, 0xa0, 0x9e, 0x4f, 0x48, 0xb0, 0xe6, 0x6e,
    0x4f, 0x51, 0xcf, 0xb0,

    /* U+0037 "7" */
    0xcf, 0xff, 0x2a, 0x46, 0xf1, 0x0, 0x8c, 0x0,
    0xd, 0x80, 0x1, 0xf4, 0x0, 0x5f, 0x0, 0xa,
    0xb0, 0x0, 0xe6, 0x0,

    /* U+0038 "8" */
    0x1c, 0xf9, 0x7, 0xd4, 0xf3, 0x7c, 0xf, 0x41,
    0xff, 0xd0, 0x5e, 0x4f, 0x18, 0xb0, 0xe4, 0x6e,
    0x5f, 0x30, 0xbf, 0x90,

    /* U+0039 "9" */
    0x3d, 0xf8, 0xa, 0xc6, 0xf1, 0xb8, 0x1f, 0x3a,
    0xa3, 0xf3, 0x3f, 0xff, 0x33, 0x41, 0xf3, 0x8d,
    0x6f, 0x22, 0xdf, 0x80,

    /* U+003A ":" */
    0x18, 0x3, 0xe1, 0x0, 0x0, 0x0, 0x18, 0x3,
    0xe1,

    /* U+0041 "A" */
    0x4, 0xf4, 0x0, 0x8f, 0x80, 0xb, 0xab, 0x0,
    0xe4, 0xf0, 0x2e, 0xf, 0x25, 0xfe, 0xf5, 0x9a,
    0x1a, 0x9c, 0x70, 0x7c,

    /* U+0042 "B" */
    0x6f, 0xfc, 0x26, 0xe3, 0xd9, 0x6e, 0xb, 0x96,
    0xff, 0xf1, 0x6e, 0xb, 0x86, 0xe0, 0x8b, 0x6e,
    0x3c, 0x96, 0xff, 0xc1,

    /* U+0043 "C" */
    0xb, 0xfb, 0x16, 0xe4, 0xd8, 0x8b, 0x4, 0x38,
    0xb0, 0x0, 0x8b, 0x0, 0x8, 0xb0, 0x44, 0x6e,
    0x4d, 0x80, 0xbf, 0xb1,

    /* U+0044 "D" */
    0x6f, 0xfc, 0x16, 0xe2, 0xd8, 0x6e, 0x9, 0xa6,
    0xe0, 0x9b, 0x6e, 0x9, 0xb6, 0xe0, 0x9a, 0x6e,
    0x2d, 0x86, 0xff, 0xc1,

    /* U+0045 "E" */
    0x6f, 0xff, 0x66, 0xe2, 0x20, 0x6e, 0x0, 0x6,
    0xff, 0x70, 0x6e, 0x21, 0x6, 0xe0, 0x0, 0x6e,
    0x22, 0x6, 0xff, 0xf6,

    /* U+0046 "F" */
    0x6f, 0xff, 0x66, 0xe2, 0x20, 0x6e, 0x0, 0x6,
    0xff, 0x70, 0x6e, 0x21, 0x6, 0xe0, 0x0, 0x6e,
    0x0, 0x6, 0xd0, 0x0,

    /* U+0047 "G" */
    0xb, 0xfb, 0x16, 0xe3, 0xe8, 0x8b, 0x7, 0x78,
    0xb0, 0x0, 0x8b, 0x7f, 0x98, 0xb1, 0xba, 0x6e,
    0x4d, 0x80, 0xbf, 0xc1,

    /* U+0048 "H" */
    0x6d, 0x9, 0xa6, 0xe0, 0xaa, 0x6e, 0xa, 0xa6,
    0xff, 0xfa, 0x6e, 0x2b, 0xa6, 0xe0, 0xaa, 0x6e,
    0xa, 0xa6, 0xd0, 0x9a,

    /* U+0049 "I" */
    0x6d, 0x6e, 0x6e, 0x6e, 0x6e, 0x6e, 0x6e, 0x6d,

    /* U+004A "J" */
    0x0, 0xf, 0x40, 0x0, 0xf4, 0x0, 0xf, 0x40,
    0x0, 0xf4, 0x0, 0xf, 0x4c, 0x70, 0xf4, 0xbb,
    0x5f, 0x23, 0xdf, 0x80,

    /* U+004B "K" */
    0x6d, 0x9, 0xb0, 0x6e, 0x2f, 0x30, 0x6e, 0xab,
    0x0, 0x6f, 0xf5, 0x0, 0x6f, 0xeb, 0x0, 0x6e,
    0x3f, 0x10, 0x6e, 0xd, 0x80, 0x6d, 0x7, 0xe0,

    /* U+004C "L" */
    0x6d, 0x0, 0x6, 0xe0, 0x0, 0x6e, 0x0, 0x6,
    0xe0, 0x0, 0x6e, 0x0, 0x6, 0xe0, 0x0, 0x6e,
    0x22, 0x6, 0xff, 0xf6,

    /* U+004D "M" */
    0x6f, 0x22, 0xf5, 0x7f, 0xab, 0xf5, 0x7e, 0xfe,
    0xf5, 0x7d, 0x87, 0xe5, 0x7d, 0x10, 0xe5, 0x7d,
    0x0, 0xe5, 0x7d, 0x0, 0xe5, 0x6d, 0x0, 0xe5,

    /* U+004E "N" */
    0x6f, 0x24, 0xf6, 0xf7, 0x4f, 0x6f, 0xd4, 0xf6,
    0xde, 0x7f, 0x6d, 0x9c, 0xf6, 0xd4, 0xff, 0x6d,
    0xe, 0xf6, 0xd0, 0x9f,

    /* U+004F "O" */
    0xb, 0xfc, 0x16, 0xe4, 0xd8, 0x8b, 0x9, 0xa8,
    0xb0, 0x9a, 0x8b, 0x9, 0xa8, 0xb0, 0x9a, 0x6e,
    0x4d, 0x80, 0xbf, 0xc1,

    /* U+0050 "P" */
    0x6f, 0xfd, 0x26, 0xe2, 0xc9, 0x6d, 0xb, 0xa6,
    0xff, 0xf3, 0x6e, 0x10, 0x6, 0xd0, 0x0, 0x6d,
    0x0, 0x6, 0xd0, 0x0,

    /* U+0051 "Q" */
    0xb, 0xfb, 0x6, 0xe3, 0xe6, 0x8b, 0xb, 0x88,
    0xb0, 0xb8, 0x8b, 0xb, 0x88, 0xb0, 0xb8, 0x7d,
    0x1e, 0x71, 0xdf, 0xd1, 0x2, 0xf2, 0x0, 0x4,
    0x0,

    /* U+0052 "R" */
    0x6f, 0xfd, 0x26, 0xe2, 0xca, 0x6e, 0xa, 0xb6,
    0xff, 0xf3, 0x6e, 0x6e, 0x6, 0xe1, 0xf2, 0x6e,
    0xd, 0x76, 0xd0, 0x9b,

    /* U+0053 "S" */
    0x1c, 0xf9, 0x8, 0xd4, 0xf4, 0x8c, 0x7, 0x32,
    0xea, 0x0, 0x2, 0xcc, 0x4, 0x40, 0xe5, 0x8d,
    0x4f, 0x51, 0xcf, 0xa0,

    /* U+0054 "T" */
    0xaf, 0xff, 0xa1, 0x5f, 0x31, 0x3, 0xf1, 0x0,
    0x3f, 0x10, 0x3, 0xf1, 0x0, 0x3f, 0x10, 0x3,
    0xf1, 0x0, 0x2f, 0x10,

    /* U+0055 "U" */
    0x7d, 0x8, 0xb7, 0xd0, 0x9b, 0x7d, 0x9, 0xb7,
    0xd0, 0x9b, 0x7d, 0x9, 0xb7, 0xd0, 0x9b, 0x4f,
    0x3d, 0x80, 0xaf, 0xc1,

    /* U+0056 "V" */
    0x9b, 0x8, 0xc6, 0xe0, 0xa9, 0x3f, 0x1d, 0x60,
    0xf3, 0xf2, 0xc, 0x8f, 0x0, 0x9e, 0xc0, 0x6,
    0xf9, 0x0, 0x3f, 0x50,

    /* U+0057 "W" */
    0xaa, 0xf, 0x54, 0xf0, 0x7c, 0x1f, 0x76, 0xd0,
    0x4f, 0x3f, 0xa9, 0xa0, 0x1f, 0x8c, 0xcb, 0x70,
    0xe, 0xd7, 0xfd, 0x40, 0xb, 0xf4, 0xef, 0x10,
    0x8, 0xf2, 0xce, 0x0, 0x5, 0xf0, 0x9b, 0x0,

    /* U+0058 "X" */
    0x8c, 0xb, 0x92, 0xf3, 0xf3, 0xb, 0xdc, 0x0,
    0x5f, 0x60, 0x5, 0xf6, 0x0, 0xbd, 0xc0, 0x2f,
    0x3f, 0x38, 0xc0, 0xb9,

    /* U+0059 "Y" */
    0x8c, 0xb, 0x83, 0xf1, 0xf3, 0xe, 0x9e, 0x0,
    0x8f, 0x80, 0x3, 0xf3, 0x0, 0x2f, 0x20, 0x2,
    0xf2, 0x0, 0x1f, 0x20,

    /* U+005A "Z" */
    0xbf, 0xff, 0x21, 0x27, 0xf0, 0x0, 0xc8, 0x0,
    0x3f, 0x10, 0xb, 0xa0, 0x2, 0xf3, 0x0, 0x9d,
    0x22, 0xb, 0xff, 0xf2,

    /* U+006B "k" */
    0x7c, 0x0, 0x8, 0xc0, 0x0, 0x8c, 0x3f, 0x28,
    0xcc, 0x80, 0x8f, 0xf5, 0x8, 0xfb, 0xa0, 0x8c,
    0x3f, 0x7, 0xc0, 0xe5
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 35, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 0, .adv_w = 136, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 32, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 45, .adv_w = 35, .box_w = 2, .box_h = 3, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 48, .adv_w = 57, .box_w = 4, .box_h = 2, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 52, .adv_w = 37, .box_w = 2, .box_h = 2, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 54, .adv_w = 68, .box_w = 4, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 70, .adv_w = 79, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 90, .adv_w = 48, .box_w = 3, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 102, .adv_w = 75, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 122, .adv_w = 75, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 142, .adv_w = 81, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 162, .adv_w = 75, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 182, .adv_w = 76, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 202, .adv_w = 69, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 222, .adv_w = 76, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 242, .adv_w = 74, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 262, .adv_w = 44, .box_w = 3, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 271, .adv_w = 80, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 291, .adv_w = 81, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 311, .adv_w = 80, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 331, .adv_w = 83, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 351, .adv_w = 77, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 371, .adv_w = 73, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 391, .adv_w = 81, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 411, .adv_w = 84, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 431, .adv_w = 39, .box_w = 2, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 439, .adv_w = 78, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 459, .adv_w = 84, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 483, .adv_w = 73, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 503, .adv_w = 95, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 527, .adv_w = 89, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 547, .adv_w = 82, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 567, .adv_w = 80, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 587, .adv_w = 80, .box_w = 5, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 612, .adv_w = 81, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 632, .adv_w = 76, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 652, .adv_w = 80, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 672, .adv_w = 84, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 692, .adv_w = 83, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 712, .adv_w = 117, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 744, .adv_w = 81, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 764, .adv_w = 80, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 784, .adv_w = 71, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 804, .adv_w = 76, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_0[] = {
    0x0, 0x5
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 32, .range_length = 6, .glyph_id_start = 1,
        .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL, .list_length = 2, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    },
    {
        .range_start = 43, .range_length = 16, .glyph_id_start = 3,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 65, .range_length = 26, .glyph_id_start = 19,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 107, .range_length = 1, .glyph_id_start = 45,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};

/*-----------------
 *    KERNING
 *----------------*/


/*Map glyph_ids to kern left classes*/
static const uint8_t kern_left_class_mapping[] =
{
    0, 0, 0, 1, 2, 3, 2, 4,
    0, 5, 6, 7, 8, 9, 10, 11,
    12, 12, 0, 13, 14, 15, 14, 16,
    17, 15, 18, 0, 19, 20, 21, 0,
    0, 15, 22, 23, 24, 25, 26, 19,
    27, 27, 28, 29, 0, 30
};

/*Map glyph_ids to kern right classes*/
static const uint8_t kern_right_class_mapping[] =
{
    0, 0, 0, 0, 1, 2, 1, 3,
    4, 5, 6, 0, 0, 0, 0, 7,
    0, 0, 0, 8, 9, 10, 9, 9,
    9, 10, 0, 0, 11, 12, 13, 0,
    9, 10, 9, 10, 14, 15, 16, 0,
    17, 17, 18, 19, 0, 0
};

/*Kern values between classes*/
static const int8_t kern_class_values[] =
{
    0, 0, 0, 0, 0, -2, -5, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -7, 0,
    0, -5, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -1, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -10, 0, 0, 0, 0, 0,
    -13, -2, -5, 0, -2, -3, -2, -2,
    0, 0, 0, 0, 0, 0, 0, 0,
    -2, 1, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -4, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -4, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -9, 0, -9, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -2, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -3,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -9, 0,
    -14, 0, -1, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, -4, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 1, 0,
    0, 0, 0, 4, 0, -3, 1, 0,
    0, 0, -4, -10, -12, 1, -8, -2,
    0, -1, 0, 0, 0, 0, -4, -1,
    0, 0, -1, 0, 0, 0, 0, -4,
    -4, -5, -1, -1, 0, 0, 0, 0,
    0, -1, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -2, -2, 0, -1, 0,
    0, 0, 0, 0, 3, 0, 0, 2,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -5, -8, 0, 0, 0, 0, -6,
    0, 0, -2, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -1, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -2, 0,
    -2, 0, 0, 0, 0, -2, -2, -1,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 2, 0, 2, 0, 0, 0, 0,
    2, 0, -3, 0, 0, 0, 0, -3,
    0, 0, 0, 0, 2, 0, 2, 0,
    0, 0, 0, 5, 0, -1, 0, 0,
    0, 0, -1, -13, 0, 0, -5, -5,
    0, -10, 0, 0, 0, 0, -5, 0,
    0, -8, 0, 0, 0, 0, 0, -2,
    -3, -3, -1, -1, 0, 0, 0, 0,
    0, -1, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -2, -2, 0, 0, 2,
    0, 0, 0, 0, 1, 0, 0, 0,
    0, 0, 0, 1, 0, 0, 0, -4,
    -1, 0, 0, 0, 0, 0, 0, 0,
    -1, 0, 0, 0, 0, 0, 0, -2,
    -2, -5, -4, -8, -11, -2, 0, 0,
    0, 0, -8, 0, -4, 0, 0, -1,
    0, 0, 0, 0, 0, 0, -5, -8,
    -8, 0, 0, 0, 0, -12, 0, -2,
    -5, 0, 0, -1, -2, 0, 0, 0,
    0, 1, -2, 0, 0, 0, 0, 0,
    1, 0, -2, -1, 0, 0, 0, -5,
    0, 0, 0, 0, -12, -9, -8, 0,
    0, 0, 0, -11, -2, -3, -8, 0,
    -1, -1, -4, 0, 0, 0, 0, 0,
    -3, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0
};


/*Collect the kern class' data in one place*/
static const lv_font_fmt_txt_kern_classes_t kern_classes =
{
    .class_pair_values   = kern_class_values,
    .left_class_mapping  = kern_left_class_mapping,
    .right_class_mapping = kern_right_class_mapping,
    .left_class_cnt      = 30,
    .right_class_cnt     = 19,
};

/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LV_VERSION_CHECK(8, 0, 0)
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = &kern_classes,
    .kern_scale = 16,
    .cmap_num = 4,
    .bpp = 4,
    .kern_classes = 1,
    .bitmap_format = 0,
#if LV_VERSION_CHECK(8, 0, 0)
    .cache = &cache
#endif
};


/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LV_VERSION_CHECK(8, 0, 0)
const lv_font_t kg_font_digits_11 = {
#else
lv_font_t kg_font_digits_11 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 10,          /*The maximum line height required by the font*/
    .base_line = 2,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc           /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
};



#endif /*#if KG_FONT_DIGITS_11*/

