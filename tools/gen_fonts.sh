#!/bin/bash
# Génère les polices LVGL (sous-ensemble Latin-1 + ponctuation typographique) depuis les TTF OFL.
set -euo pipefail
cd "$(dirname "$0")/.."
R='0x20-0x7E,0xA0-0xFF,0x2013,0x2014,0x2019,0x2022,0x2026'
gen() { # poids taille nom
  npx -y lv_font_conv@1.5.2 --font "assets/fonts/BarlowCondensed-$1.ttf" -r "$R" \
    --size "$2" --bpp 4 --format lvgl --no-compress --lv-include lvgl.h -o "src/ui/fonts/$3.c"
}
mkdir -p src/ui/fonts
gen Medium   16 kg_font_text_16
gen SemiBold 20 kg_font_label_20
gen SemiBold 34 kg_font_title_34
