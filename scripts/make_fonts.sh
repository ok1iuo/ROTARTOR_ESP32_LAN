#!/bin/sh
# Regenerates the LVGL fonts in src/fonts/ (built-in Montserrat has no Czech letters).
# Montserrat (SIL OFL) and FontAwesome 5 come with LVGL, see
# managed_components/lvgl__lvgl/scripts/built_in_font/font_license. Needs node (npx) and one
# build first so managed_components/ exists. Run from the project root: sh scripts/make_fonts.sh
set -e
cd managed_components/lvgl__lvgl/scripts/built_in_font
OUT=../../../../src/fonts
CZ="áčďéěíňóřšťúůýžÁČĎÉĚÍŇÓŘŠŤÚŮÝŽ°"
SYMBOLS=0xF071,0xF054   # LV_SYMBOL_WARNING, LV_SYMBOL_RIGHT
for SIZE in 14 20 28; do
    npx -y lv_font_conv@1.5.3 --no-compress --no-prefilter --bpp 4 --size $SIZE --format lvgl \
        --lv-include lvgl.h --force-fast-kern-format \
        --font Montserrat-Medium.ttf -r 0x20-0x7E --symbols "$CZ" \
        --font FontAwesome5-Solid+Brands+Regular.woff -r $SYMBOLS \
        -o $OUT/font_cz_$SIZE.c
done
# Big angle readouts: digits, minus and the degree sign only
npx -y lv_font_conv@1.5.3 --no-compress --no-prefilter --bpp 4 --size 48 --format lvgl \
    --lv-include lvgl.h --force-fast-kern-format \
    --font Montserrat-Medium.ttf -r 0x2D,0x30-0x39 --symbols "°" \
    -o $OUT/font_num_48.c
