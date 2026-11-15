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

"""Genera include/aprs_icons.h (iconos APRS de 1 bit para las pantallas OLED; 28x28 desde la 5.4.7, antes 24x24) a partir de las
hojas de simbolos aprs-symbols-<N>-0.png (tabla primaria '/') y aprs-symbols-<N>-1.png (tabla secundaria '\\'): 16 columnas x 6 filas
de N px (cualquier tamano: se reescala al del icono), orden por codigo ASCII desde '!' (0x21) hasta '~' (0x7E).
Uso: gen_aprs_icons.py primaria.png secundaria.png salida.h [tamano=28]
Las hojas de 48 px estan en tools/assets/aprs-symbols.
Los simbolos son los de aprs-symbols (hessu/aprs-symbols), los mismos del iGate. Pixel encendido = tinta oscura (luminosidad < ~65 %)."""
import sys
from PIL import Image

SIZE, COLS, FIRST, COUNT, THR = 28, 16, 0x21, 94, 0.35

COPYRIGHT_HEADER = '/* Copyright (C) 2026 Manuel - EA7JRS\n *\n * This file is part of LoRa APRS Tracker.\n *\n * LoRa APRS Tracker is free software: you can redistribute it and/or modify\n * it under the terms of the GNU General Public License as published by\n * the Free Software Foundation, either version 3 of the License, or\n * (at your option) any later version.\n *\n * LoRa APRS Tracker is distributed in the hope that it will be useful,\n * but WITHOUT ANY WARRANTY; without even the implied warranty of\n * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the\n * GNU General Public License for more details.\n *\n * You should have received a copy of the GNU General Public License\n * along with LoRa APRS Tracker. If not, see <https://www.gnu.org/licenses/>.\n */\n\n'

def table(path):
    im = Image.open(path).convert('RGBA')
    tw = im.width // COLS                                   # lado de cada simbolo en la hoja
    out = []
    for idx in range(COUNT):
        c, r = idx % COLS, idx // COLS
        tile = im.crop((c * tw, r * tw, (c + 1) * tw, (r + 1) * tw))
        if tw != SIZE: tile = tile.resize((SIZE, SIZE), Image.LANCZOS)
        bg = Image.new('RGBA', tile.size, (255, 255, 255, 255))
        bg.alpha_composite(tile)
        g = bg.convert('L')
        rows = []
        for y in range(SIZE):
            bits = 0
            for x in range(SIZE):
                ink = (255 - g.getpixel((x, y))) / 255.0 > THR
                bits = (bits << 1) | (1 if ink else 0)
            bits <<= BPR * 8 - SIZE                          # alineado a la izquierda: el MSB del primer byte es el pixel 0
            rows += [(bits >> (8 * (BPR - 1 - k))) & 255 for k in range(BPR)]
        out.append(rows)
    return out

def emit(name, data, f):
    f.write('static const uint8_t %s[%d][%d] = {\n' % (name, COUNT, SIZE * BPR))
    for i, rows in enumerate(data):
        f.write('    /* %s */ {%s},\n' % ('0x%02X' % (FIRST + i), ','.join(str(v) for v in rows)))
    f.write('};\n')

if __name__ == '__main__':
    p0, p1, out = sys.argv[1:4]
    if len(sys.argv) > 4: SIZE = int(sys.argv[4])
    BPR = (SIZE + 7) // 8
    with open(out, 'w') as f:
        f.write(COPYRIGHT_HEADER)
        f.write('#pragma once\n// Generado por tools/gen_aprs_icons.py: iconos APRS de %dx%d px, 1 bit, %d bytes por fila (MSB = pixel izquierdo).\n'
                '// Indice = codigo del simbolo - 0x21. Imagenes: aprs-symbols (hessu/aprs-symbols), usadas tambien en la WebUI del iGate.\n'
                '#include <stdint.h>\n#define APRS_ICON_SIZE %d\n#define APRS_ICON_BPR %d\n#define APRS_ICON_FIRST 0x21\n#define APRS_ICON_COUNT 94\n'
                % (SIZE, SIZE, BPR, SIZE, BPR))
        emit('APRS_ICONS_PRIMARY', table(p0), f)
        emit('APRS_ICONS_SECONDARY', table(p1), f)
