#!/usr/bin/env python3
# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Genera include/logo_splash_color.h: el logo de la app (tools/assets/welcome_logo.jpg) para la bienvenida a color
# de la Heltec Wireless Tracker (160x80): el pin con las ondas y el texto "APRS LoRa+", en RGB565, con el fondo
# del JPG fundido al #131A22 de la paleta. Uso: python3 tools/make_tft_logo.py
import os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "tools", "assets", "welcome_logo.jpg")
OUT = os.path.join(ROOT, "include", "logo_splash_color.h")
BG = (0x13, 0x1A, 0x22)


def blend(im):
    """Funde el fondo oscuro del JPG con BG: cuanto mas se parece un pixel al fondo, mas se acerca a BG."""
    px = im.load()
    w, h = im.size
    edge = [px[x, 0] for x in range(w)] + [px[x, h - 1] for x in range(w)] + [px[0, y] for y in range(h)] + [px[w - 1, y] for y in range(h)]
    ref = [sorted(e[k] for e in edge)[len(edge) // 2] for k in range(3)]
    for y in range(h):
        for x in range(w):
            p = px[x, y]
            d = max(abs(p[k] - ref[k]) for k in range(3))
            t = min(1.0, max(0.0, (d - 8) / 40.0))
            px[x, y] = tuple(round(BG[k] * (1 - t) + p[k] * t) for k in range(3))
    return im


def rgb565(p):
    r, g, b = p
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def emit(name, im):
    w, h = im.size
    px = im.load()
    vals = [rgb565(px[x, y]) for y in range(h) for x in range(w)]
    lines = [f"#define {name}_W   {w}", f"#define {name}_H   {h}", f"static const uint16_t {name}[] PROGMEM = {{"]
    for i in range(0, len(vals), 12):
        lines.append("    " + ", ".join(f"0x{v:04X}" for v in vals[i:i + 12]) + ",")
    lines.append("};")
    return "\n".join(lines)


def main():
    logo = Image.open(SRC).convert("RGB")
    icon = logo.crop((235, 125, 850, 815))
    icon = blend(icon.resize((round(icon.width * 66 / icon.height), 66), Image.LANCZOS))
    text = logo.crop((165, 860, 925, 1290))
    text = blend(text.resize((88, round(text.height * 88 / text.width)), Image.LANCZOS))
    head = """/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (based on CA2RXU LoRa APRS Tracker).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Logo de la app para la bienvenida a color de la Heltec Wireless Tracker: pin con ondas y texto "APRS LoRa+",
 * RGB565 (un uint16_t por pixel, fila a fila). Generado por tools/make_tft_logo.py; no editar a mano.
 */
#ifndef LOGO_SPLASH_COLOR_H_
#define LOGO_SPLASH_COLOR_H_

#include <Arduino.h>
"""
    with open(OUT, "w") as f:
        f.write(head + "\n" + emit("LOGO_APP_ICON", icon) + "\n\n" + emit("LOGO_APP_TEXT", text) + "\n\n#endif\n")
    print(OUT, icon.size, text.size)


if __name__ == "__main__":
    main()
