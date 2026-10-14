#!/bin/bash
# Generates the LVGL fonts (Latin-1 subset + typographic punctuation) from the OFL TTFs.
set -euo pipefail
cd "$(dirname "$0")/.."
R='0x20-0x7E,0xA0-0xFF,0x152,0x153,0x2013,0x2014,0x2018,0x2019,0x201C,0x201D,0x2022,0x2026,0x20AC,0x2212'
gen() { # weight size name
  npx -y lv_font_conv@1.5.2 --font "assets/fonts/BarlowCondensed-$1.ttf" -r "$R" \
    --size "$2" --bpp 4 --format lvgl --no-compress --lv-include lvgl.h -o "src/ui/fonts/$3.c"
}
mkdir -p src/ui/fonts
gen Medium   16 kg_font_text_16
gen SemiBold 20 kg_font_label_20
gen SemiBold 34 kg_font_title_34
# Digits only (bezel, sub-dials, applied numerals): minimal subsets.
gen_digits() { # weight size name
  npx -y lv_font_conv@1.5.2 --font "assets/fonts/BarlowCondensed-$1.ttf" -r '0x20,0x25,0x2B-0x3A,0x4B,0x6B' \
    --size "$2" --bpp 4 --format lvgl --no-compress --lv-include lvgl.h -o "src/ui/fonts/$3.c"
}
# Small font: digits, capitals, % and k (bezel, sub-dials).
npx -y lv_font_conv@1.5.2 --font assets/fonts/BarlowCondensed-SemiBold.ttf -r '0x20,0x25,0x2B-0x3A,0x41-0x5A,0x6B' \
  --size 11 --bpp 4 --format lvgl --no-compress --lv-include lvgl.h -o src/ui/fonts/kg_font_digits_11.c
gen_digits SemiBold 30 kg_font_digits_30
# Stencil for labels (Time circuits face, titles): capitals, digits, a few accented capitals.
npx -y lv_font_conv@1.5.2 --font assets/fonts/AllertaStencil-Regular.ttf -r '0x20-0x5A,0xC0,0xC7,0xC8,0xC9,0xCA,0xCE,0xD4' \
  --size 11 --bpp 4 --format lvgl --no-compress --lv-include lvgl.h -o src/ui/fonts/kg_font_stencil_11.c
# Large digits of the Night face.
npx -y lv_font_conv@1.5.2 --font assets/fonts/BarlowCondensed-Medium.ttf -r '0x30-0x3A' \
  --size 104 --bpp 4 --format lvgl --no-compress --lv-include lvgl.h -o src/ui/fonts/kg_font_night_104.c
# Medium digits for the apps (stopwatch, timer, alarms, steps).
npx -y lv_font_conv@1.5.2 --font assets/fonts/BarlowCondensed-SemiBold.ttf -r '0x20,0x2C-0x3A,0xB0,0x2212' \
  --size 56 --bpp 4 --format lvgl --no-compress --lv-include lvgl.h -o src/ui/fonts/kg_font_digits_56.c
