#!/usr/bin/env python3
# Copyright (C) 2026 Manuel - EA7JRS
#
# This file is part of LoRa APRS Tracker.
#
# LoRa APRS Tracker is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# LoRa APRS Tracker is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with LoRa APRS Tracker. If not, see <https://www.gnu.org/licenses/>.

"""Genera include/aprs_icons.h (iconos APRS 24x24 de 1 bit) a partir de las hojas de simbolos aprs-symbols-24-0.png (tabla
primaria '/') y aprs-symbols-24-1.png (tabla secundaria '\\'): 16 columnas x 6 filas de 24 px, orden por codigo ASCII desde '!' (0x21)
hasta '~' (0x7E). Uso: gen_aprs_icons.py primaria.png secundaria.png salida.h
Los simbolos son los de aprs-symbols (hessu/aprs-symbols), los mismos del iGate. Pixel encendido = tinta oscura (luminosidad < ~65 %)."""
import sys
from PIL import Image

SIZE, COLS, FIRST, COUNT, THR = 24, 16, 0x21, 94, 0.35

COPYRIGHT_HEADER = '/* Copyright (C) 2026 Manuel - EA7JRS\n *\n * This file is part of LoRa APRS Tracker.\n *\n * LoRa APRS Tracker is free software: you can redistribute it and/or modify\n * it under the terms of the GNU General Public License as published by\n * the Free Software Foundation, either version 3 of the License, or\n * (at your option) any later version.\n *\n * LoRa APRS Tracker is distributed in the hope that it will be useful,\n * but WITHOUT ANY WARRANTY; without even the implied warranty of\n * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the\n * GNU General Public License for more details.\n *\n * You should have received a copy of the GNU General Public License\n * along with LoRa APRS Tracker. If not, see <https://www.gnu.org/licenses/>.\n */\n\n'

def table(path):
    im = Image.open(path).convert('RGBA')
    out = []
    for idx in range(COUNT):
        c, r = idx % COLS, idx // COLS
        tile = im.crop((c * SIZE, r * SIZE, (c + 1) * SIZE, (r + 1) * SIZE))
        bg = Image.new('RGBA', tile.size, (255, 255, 255, 255))
        bg.alpha_composite(tile)
        g = bg.convert('L')
        rows = []
        for y in range(SIZE):
            bits = 0
            for x in range(SIZE):
                ink = (255 - g.getpixel((x, y))) / 255.0 > THR
                bits = (bits << 1) | (1 if ink else 0)
            rows += [(bits >> 16) & 255, (bits >> 8) & 255, bits & 255]
        out.append(rows)
    return out

def emit(name, data, f):
    f.write('static const uint8_t %s[%d][%d] = {\n' % (name, COUNT, SIZE * 3))
    for i, rows in enumerate(data):
        f.write('    /* %s */ {%s},\n' % ('0x%02X' % (FIRST + i), ','.join(str(v) for v in rows)))
    f.write('};\n')

if __name__ == '__main__':
    p0, p1, out = sys.argv[1:4]
    with open(out, 'w') as f:
        f.write(COPYRIGHT_HEADER)
        f.write('#pragma once\n// Generado por tools/gen_aprs_icons.py: iconos APRS de 24x24 px, 1 bit, 3 bytes por fila (MSB = pixel izquierdo).\n'
                '// Indice = codigo del simbolo - 0x21. Imagenes: aprs-symbols (hessu/aprs-symbols), usadas tambien en la WebUI del iGate.\n'
                '#include <stdint.h>\n#define APRS_ICON_SIZE 24\n#define APRS_ICON_FIRST 0x21\n#define APRS_ICON_COUNT 94\n')
        emit('APRS_ICONS_PRIMARY', table(p0), f)
        emit('APRS_ICONS_SECONDARY', table(p1), f)
