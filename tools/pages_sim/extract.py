# Saca funciones de pages_utils.cpp por su cabecera (texto exacto del principio de la linea, sin sangria)
import sys, re
src = open(sys.argv[1], encoding='utf-8', errors='replace').read().split('\n')
names = [l.strip() for l in open(sys.argv[2]) if l.strip() and not l.startswith('#')]
out = []
for nm in names:
    idx = None
    for i, l in enumerate(src):
        if l.strip().startswith(nm):
            idx = i; break
    if idx is None: sys.exit('no encontrado: ' + nm)
    depth = 0; started = False; j = idx
    while True:
        line = src[j]
        # quita cadenas y comentarios simples para contar llaves
        s = re.sub(r'"(\\.|[^"\\])*"', '""', line)
        s = re.sub(r"'(\\.|[^'\\])'", "''", s)
        s = s.split('//')[0]
        for ch in s:
            if ch == '{': depth += 1; started = True
            elif ch == '}': depth -= 1
        out.append(line)
        if started and depth == 0: break
        if not started and s.rstrip().endswith(';'): break
        j += 1
    out.append('')
open(sys.argv[3], 'w').write('\n'.join(out))
