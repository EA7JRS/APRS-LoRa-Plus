#!/usr/bin/env python3
# Genera include/fst4w_table.h a partir de lib/fst4/ldpc_240_74_generator.f90 de WSJT-X 3.3.0 (GPL-3.0).
# Uso: python3 tools/gen_fst4w_table.py <ldpc_240_74_generator.f90> > include/fst4w_table.h  (cabecera GPL: anadirla a mano)
import re, sys
g = re.findall(r'"([0-9a-f]{19})"', open(sys.argv[1]).read())
assert len(g) == 166
for h in g:
    bits = bin(int(h, 16))[2:].zfill(76)[:74].ljust(80, '0')
    print('        { ' + ', '.join('0x%02x' % int(bits[i:i + 8], 2) for i in range(0, 80, 8)) + ' },')
