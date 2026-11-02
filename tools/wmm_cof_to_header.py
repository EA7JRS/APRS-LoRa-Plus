#!/usr/bin/env python3
"""Convierte el fichero oficial WMM.COF (NOAA / BGS) en include/wmm_coeffs.h.

Uso:  python3 tools/wmm_cof_to_header.py WMM2025.COF > include/wmm_coeffs.h
Primera linea del .COF: epoca, nombre y fecha; despues una fila por coeficiente: n m g h gDot hDot;
termina con una linea de 9999...
"""
import sys

def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    rows = []
    epoch = None
    with open(sys.argv[1]) as f:
        for line in f:
            parts = line.split()
            if not parts:
                continue
            if epoch is None:
                epoch = float(parts[0])
                continue
            if parts[0].startswith('9999'):
                break
            if len(parts) < 6:
                continue
            n, m = int(parts[0]), int(parts[1])
            g, h, gd, hd = (float(x) for x in parts[2:6])
            rows.append((n, m, g, h, gd, hd))
    if epoch is None or not rows:
        sys.exit("fichero .COF no valido")
    print("// Generado por tools/wmm_cof_to_header.py -- no editar a mano")
    print("#ifndef WMM_COEFFS_H_")
    print("#define WMM_COEFFS_H_")
    print("#define WMM_EPOCH   %.1f" % epoch)
    print("#define WMM_COUNT   %d" % len(rows))
    print("static const WMM::Coeff WMM_TABLE[] = {")
    for n, m, g, h, gd, hd in rows:
        print("    {%d, %d, %.4ff, %.4ff, %.4ff, %.4ff}," % (n, m, g, h, gd, hd))
    print("};")
    print("#endif")

main()
