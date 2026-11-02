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

"""Genera include/aprs_devices.h (identificacion de equipos APRS) a partir de tocalls.yaml de
https://github.com/aprsorg/aprs-deviceid (web: https://aprsorg.github.io/aprs-deviceid-web/).
Uso: gen_aprs_devices.py tocalls.yaml include/aprs_devices.h
La lista la mantiene Hessu, OH7LZB, para aprs.fi bajo licencia CC BY-SA 2.0 (http://creativecommons.org/licenses/by-sa/2.0/):
hay que citar la fuente (ver docs)."""
import re, sys
import yaml

COPYRIGHT_HEADER = '/* Copyright (C) 2026 Manuel - EA7JRS\n *\n * This file is part of LoRa APRS Tracker.\n *\n * LoRa APRS Tracker is free software: you can redistribute it and/or modify\n * it under the terms of the GNU General Public License as published by\n * the Free Software Foundation, either version 3 of the License, or\n * (at your option) any later version.\n *\n * LoRa APRS Tracker is distributed in the hope that it will be useful,\n * but WITHOUT ANY WARRANTY; without even the implied warranty of\n * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the\n * GNU General Public License for more details.\n *\n * You should have received a copy of the GNU General Public License\n * along with LoRa APRS Tracker. If not, see <https://www.gnu.org/licenses/>.\n */\n\n'
MAXLEN = 17          # Station.device[18]
CALLSIGN = re.compile(r'^[A-Z0-9]{1,2}[0-9][A-Z]{1,4}$')

def brand(vendor):
    """Marca de una sola palabra (Yaesu, Kenwood...); None si es una persona, un indicativo o un texto largo."""
    if not vendor: return None
    v = re.sub(r'\b(LLC|Ltd\.?|Srl|Inc\.?)\b', '', vendor).strip(' ,')
    v = {'Argent Data Systems': 'Argent', 'Altus Metrum': 'Altus', 'Painter Engineering': 'Painter', 'Open Source': ''}.get(v, v)
    if not v or ',' in v or '/' in v or ' ' in v or CALLSIGN.match(v): return None
    return v

def short(entry):
    vendor, model = entry.get('vendor', ''), entry.get('model', '')
    if model.lower() == 'unknown': model = ''
    b = brand(vendor)
    if model:
        if b and b.lower() not in model.lower() and len(b) + 1 + len(model) <= MAXLEN: return b + ' ' + model
        return model[:MAXLEN]
    if vendor:
        return re.sub(r'\s+', ' ', vendor.split(',')[0]).strip()[:MAXLEN]
    return ''

def literals(p):
    return sum(1 for c in p if c not in '?n*')

def cstr(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'

def main(src, dst):
    d = yaml.safe_load(open(src, encoding='utf-8'))
    tocalls = [(t['tocall'], short(t)) for t in d['tocalls']]
    tocalls = [t for t in tocalls if t[1]]
    tocalls = [t for _, t in sorted(enumerate(tocalls), key=lambda it: (-literals(it[1][0]), it[0]))]
    mice = [(m['suffix'], short(m)) for m in d['mice']]
    legacy = [(m['prefix'], m.get('suffix', ''), short(m)) for m in d['micelegacy']]
    with open(dst, 'w', encoding='utf-8') as f:
        f.write(COPYRIGHT_HEADER)
        f.write('#pragma once\n// Generado por tools/gen_aprs_devices.py a partir de tocalls.yaml (https://aprsorg.github.io/aprs-deviceid-web/).\n'
                '// Lista de identificacion de equipos y software APRS mantenida por Hessu, OH7LZB, para aprs.fi; licencia CC BY-SA 2.0\n'
                '// (http://creativecommons.org/licenses/by-sa/2.0/). Nombres abreviados a 17 caracteres.\n'
                '// Patrones: ? = cualquier caracter, n = digito, * = el resto. Ordenados de mas a menos especifico.\n'
                '#include <stdint.h>\n')
        f.write('struct AprsTocall { const char* pattern; const char* name; };\n')
        f.write('struct AprsMiceSuffix { char suffix[3]; const char* name; };\n')
        f.write('struct AprsMiceLegacy { char prefix; char suffix; const char* name; };\n')
        f.write('static const AprsTocall APRS_TOCALLS[] = {\n')
        for p, n in tocalls: f.write('    { %s, %s },\n' % (cstr(p), cstr(n)))
        f.write('};\nstatic const int APRS_TOCALL_COUNT = %d;\n' % len(tocalls))
        f.write('static const AprsMiceSuffix APRS_MICE_SUFFIXES[] = {\n')
        for s, n in mice: f.write('    { {%s, %s, 0}, %s },\n' % (("'\\\\'" if s[0]=='\\' else "'%s'" % s[0]), ("'\\\\'" if s[1]=='\\' else ("'\\\"'" if s[1]=='"' else "'%s'" % s[1])), cstr(n)))
        f.write('};\nstatic const int APRS_MICE_SUFFIX_COUNT = %d;\n' % len(mice))
        f.write('static const AprsMiceLegacy APRS_MICE_LEGACY[] = {\n')
        for p, s, n in legacy: f.write("    { '%s', %s, %s },\n" % (p, ("'%s'" % s) if s else "0", cstr(n)))
        f.write('};\nstatic const int APRS_MICE_LEGACY_COUNT = %d;\n' % len(legacy))
    print(len(tocalls), 'tocalls,', len(mice), 'sufijos Mic-E,', len(legacy), 'Mic-E clasicos')

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
