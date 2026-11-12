#!/usr/bin/env python3
# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
"""Genera include/aprs_icons_color.h: iconos APRS a color para las pantallas TFT (Heltec Wireless Tracker, T-Deck), de las
hojas de 48 px de aprs-symbols (hessu/aprs-symbols, tools/assets/aprs-symbols; las mismas de la app Android): tabla primaria '/',
secundaria '\\' y superposiciones (0-9, A-Z). Cada icono: paleta de hasta 15 colores (RGB565 + opacidad 0-255; el indice 0 es
transparente) y 4 bits por pixel, dos pixeles por byte (el de la izquierda en los 4 bits altos).
Tamanos: 24 px (LAST) y 8 px (lista STATIONS). Uso: python3 tools/gen_aprs_icons_color.py"""
import os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "tools", "assets", "aprs-symbols")
OUT = os.path.join(ROOT, "include", "aprs_icons_color.h")
COLS, FIRST, COUNT, TILE = 16, 0x21, 94, 48
OVERLAYS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"


def tiles(sheet):
    im = Image.open(os.path.join(SRC, sheet)).convert("RGBA")
    return [im.crop(((i % COLS) * TILE, (i // COLS) * TILE, (i % COLS + 1) * TILE, (i // COLS + 1) * TILE)) for i in range(COUNT)]


def encode(tile, size):
    t = tile.resize((size, size), Image.LANCZOS)
    px = list(t.getdata())
    solid = [p for p in px if p[3] >= 24]
    pal = []
    if solid:
        rgb = Image.new("RGB", (len(solid), 1))
        rgb.putdata([p[:3] for p in solid])
        q = rgb.quantize(colors=15, method=Image.Quantize.MEDIANCUT)
        qp = q.getpalette()[:45]
        idx = list(q.getdata())
        # opacidad media de cada color (con 4 niveles para que los bordes se fundan con el fondo)
        alphas = {}
        for i, p in zip(idx, solid):
            alphas.setdefault(i, []).append(p[3])
        used = sorted(alphas)
        remap = {}
        for i in used:
            r, g, b = qp[i * 3:i * 3 + 3]
            remap[i] = len(pal) + 1
            pal.append([r, g, b, 255])
        # pixeles semitransparentes: colores aparte por nivel de opacidad (si caben)
    out = []
    k = 0
    for p in px:
        if p[3] < 24:
            out.append(0)
        else:
            i = remap[idx[k]]
            k += 1
            if p[3] < 200:                                  # borde: busca o crea la variante semitransparente
                a = 96 if p[3] < 110 else 170
                base = pal[i - 1]
                key = [base[0], base[1], base[2], a]
                if key in pal:
                    i = pal.index(key) + 1
                elif len(pal) < 15:
                    pal.append(key)
                    i = len(pal)
            out.append(i)
    data = []
    for j in range(0, len(out), 2):
        data.append((out[j] << 4) | (out[j + 1] if j + 1 < len(out) else 0))
    c565 = [((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3) for r, g, b, a in pal]
    return len(pal), c565, [a for *_, a in pal], data


def emit(f, name, items, size):
    f.write(f"static const AprsIconColor {name}[{len(items)}] PROGMEM = {{\n")
    for label, (n, c, a, d) in items:
        c = c + [0] * (15 - len(c))
        a = a + [0] * (15 - len(a))
        f.write(f"    /* {label} */ {{ {n}, {{{','.join('0x%04X' % v for v in c)}}}, {{{','.join(str(v) for v in a)}}},\n")
        f.write(f"      {{{','.join(str(v) for v in d)}}} }},\n")
    f.write("};\n")


def main():
    prim, sec, ovl = tiles("aprs-symbols-48-0.png"), tiles("aprs-symbols-48-1.png"), tiles("aprs-symbols-48-2.png")
    with open(OUT, "w") as f:
        f.write("""/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (based on CA2RXU LoRa APRS Tracker).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Iconos APRS a color para las pantallas TFT. Generado por tools/gen_aprs_icons_color.py; no editar a mano.
 * Imagenes: aprs-symbols (hessu/aprs-symbols, tools/assets/aprs-symbols/COPYRIGHT.md), las mismas de la app Android.
 */
#pragma once
#include <stdint.h>
#include <Arduino.h>

// Paleta de hasta 15 colores (el indice 0 de los pixeles es transparente) y 4 bits por pixel, el de la izquierda en los bits altos
template <int S> struct AprsIconColorT { uint8_t n; uint16_t c[15]; uint8_t a[15]; uint8_t px[S * S / 2]; };
""")
        for size in (24, 8):
            f.write(f"typedef AprsIconColorT<{size}> AprsIconColor{size};\n")
        for size in (24, 8):
            f.write(f"\n#define AprsIconColor AprsIconColor{size}\n")
            emit(f, f"APRS_COLOR{size}_PRIMARY", [(chr(FIRST + i), encode(t, size)) for i, t in enumerate(prim)], size)
            emit(f, f"APRS_COLOR{size}_SECONDARY", [(chr(FIRST + i), encode(t, size)) for i, t in enumerate(sec)], size)
            if size == 24:
                emit(f, "APRS_COLOR24_OVERLAY", [(ch, encode(ovl[ord(ch) - FIRST], size)) for ch in OVERLAYS], size)
            f.write("#undef AprsIconColor\n")
        f.write('\n#define APRS_COLOR_OVERLAYS "%s"\n' % OVERLAYS)
    print(OUT, os.path.getsize(OUT))


if __name__ == "__main__":
    main()
