#!/bin/sh
# Copyright (C) 2026 Manuel - EA7JRS. GPL-3.0-or-later.
# Simulador de las paginas del tracker en el PC: capturas PNG con el mismo codigo de dibujo que el firmware.
#   tools/pages_sim/build.sh <carpeta de Adafruit-GFX-Library> [carpeta de salida]
# Saca dos juegos: tft_*.png (160x80 a color) y oled_*.png (128x64). Necesita g++, zlib y python3.
set -e
GFX=${1:?uso: build.sh <Adafruit-GFX-Library> [salida]}
[ -f "$GFX/Adafruit_GFX.cpp" ] || GFX="$GFX/src"
OUT=${2:-docs/img/screens}
D=$(cd "$(dirname "$0")" && pwd)
T=$(cd "$D/../.." && pwd)
W=$(mktemp -d)
python3 "$D/extract.py" "$T/src/pages_utils.cpp" "$D/funcs.txt" "$W/extracted.inc"
for V in tft oled; do
    [ $V = oled ] && DEF=-DOLED || DEF=
    g++ $DEF -std=gnu++17 -DARDUINO=100 -O1 -w -I"$W" -I"$D/stub" -I"$GFX" -I"$T/include" -I"$T/lib/aprs_formulas/src" \
        "$D/harness.cpp" "$GFX/Adafruit_GFX.cpp" "$T/src/pages_core.cpp" "$T"/lib/aprs_formulas/src/*.cpp -lz -o "$W/sim_$V"
    mkdir -p "$W/$V"
    (cd "$W" && OUTDIR=$V ./sim_$V "${SIM_TIME:-2026-11-24T13:30:00}" all > /dev/null)
    for f in "$W/$V"/*.png; do cp "$f" "$OUT/${V}_$(basename "$f")"; done
done
rm -rf "$W"
echo "Capturas en $OUT"
