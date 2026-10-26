#!/usr/bin/env bash
# Regenerates main/fonts/font_*.c with lv_font_conv (needs Node.js; npx downloads the tool).
# Montserrat Medium with ASCII + Latin-1 (accents such as "Recebíveis") + a few typographic
# characters, merged with the LVGL symbol icons used by the UI (same FontAwesome file LVGL uses
# for its built-in fonts). Run from the project root after changing sizes or ranges.
set -euo pipefail
cd "$(dirname "$0")/.."
FONT_DIR=managed_components/lvgl__lvgl/scripts/built_in_font
TTF="$FONT_DIR/Montserrat-Medium.ttf"
ICONS="$FONT_DIR/FontAwesome5-Solid+Brands+Regular.woff"
[ -f "$TTF" ] || { echo "Run 'idf.py reconfigure' first so managed_components/ exists"; exit 1; }

TEXT_RANGE="0x20-0x7E,0xA0-0xFF,0x2013,0x2014,0x2018,0x2019,0x201C,0x201D,0x2022,0x2026,0x20AC"
# LV_SYMBOL_ OK, CLOSE, REFRESH, PLUS, LEFT, RIGHT, TRASH, WIFI, WARNING, KEYBOARD, BACKSPACE, NEW_LINE, UP, DOWN,
# PLAY, PAUSE, SETTINGS (gear)
ICON_RANGE="0xF00C,0xF00D,0xF021,0xF067,0xF053,0xF054,0xF2ED,0xF1EB,0xF071,0xF11C,0xF55A,0xF8A2,0xF077,0xF078,0xF04B,0xF04C,0xF013"

for size in 12 14 16 18 20 24 40; do
  npx -y lv_font_conv@1.5.3 --no-compress --no-prefilter --force-fast-kern-format --bpp 4 --size "$size" \
    --font "$TTF" --range "$TEXT_RANGE" --font "$ICONS" --range "$ICON_RANGE" \
    --format lvgl --lv-include lvgl.h --lv-font-name "font_$size" -o "main/fonts/font_$size.c"
  echo "main/fonts/font_$size.c"
done
