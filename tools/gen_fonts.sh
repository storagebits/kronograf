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
# Chiffres seuls (lunette, sous-cadrans, chiffres appliqués) : sous-ensembles minimaux.
gen_digits() { # poids taille nom
  npx -y lv_font_conv@1.5.2 --font "assets/fonts/BarlowCondensed-$1.ttf" -r '0x20,0x25,0x2B-0x3A,0x4B,0x6B' \
    --size "$2" --bpp 4 --format lvgl --no-compress --lv-include lvgl.h -o "src/ui/fonts/$3.c"
}
# Petite police : chiffres, majuscules, % et k (lunette, sous-cadrans).
npx -y lv_font_conv@1.5.2 --font assets/fonts/BarlowCondensed-SemiBold.ttf -r '0x20,0x25,0x2B-0x3A,0x41-0x5A,0x6B' \
  --size 11 --bpp 4 --format lvgl --no-compress --lv-include lvgl.h -o src/ui/fonts/kg_font_digits_11.c
gen_digits SemiBold 30 kg_font_digits_30
# Pochoir des bandeaux (cadran Circuits temporels) : majuscules, chiffres, É.
npx -y lv_font_conv@1.5.2 --font assets/fonts/AllertaStencil-Regular.ttf -r '0x20-0x5A,0xC8,0xC9' \
  --size 11 --bpp 4 --format lvgl --no-compress --lv-include lvgl.h -o src/ui/fonts/kg_font_stencil_11.c
# Grands chiffres du cadran Nuit.
npx -y lv_font_conv@1.5.2 --font assets/fonts/BarlowCondensed-Medium.ttf -r '0x30-0x3A' \
  --size 104 --bpp 4 --format lvgl --no-compress --lv-include lvgl.h -o src/ui/fonts/kg_font_night_104.c
