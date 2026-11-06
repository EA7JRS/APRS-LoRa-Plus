#!/usr/bin/env python3
# Comprueba que las tablas de canales IARU del firmware (src/iaru_plan.cpp) coinciden con las de la WebUI (data_embed/script.js).
# Copyright (C) 2026 Manuel - EA7JRS   (GPL-3.0-or-later)
#   python3 tools/check_iaru_tables.py        (necesita g++ y node)
import json, os, re, subprocess, sys, tempfile
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
tmp = tempfile.mkdtemp()
exe = os.path.join(tmp, "t")
subprocess.check_call(["g++", "-std=c++11", "-I" + root + "/include", "-I" + root + "/src", root + "/tools/test_iaru_plan.cpp", root + "/src/iaru_plan.cpp", "-o", exe])
fw = json.loads(subprocess.check_output([exe, "--tables"]))
js = open(root + "/data_embed/script.js", encoding="utf-8").read()
start = js.index("const IARU_REGIONS")
end = js.index("function iaruList")
code = js[start:end] + """
const out = {};
const T = {lora: IARU_CHANNELS, fsk: IARU_CHANNELS_FSK, cw: IARU_CHANNELS_CW, cwb: IARU_CHANNELS_CWB};
for (const k of Object.keys(T)) for (const r of [1, 2, 3]) out[k + r] = (T[k][r] || []).map(c => Math.round(c[1] * 1e6));
out.bands = IARU_REGIONS;
console.log(JSON.stringify(out));
"""
ws = json.loads(subprocess.check_output(["node", "-e", code]))
bad = 0
for k, v in fw.items():
    if ws.get(k) != v:
        bad += 1
        print("DIFERENTE", k, "\n  firmware:", v, "\n  WebUI   :", ws.get(k))
# bandas de aficionados (firmware: R1/R3 430-440, R2 420-450 y 902-928)
exp = {"1": [[430.0, 440.0]], "2": [[420.0, 450.0], [902.0, 928.0]], "3": [[430.0, 440.0]]}
for r, b in exp.items():
    if ws["bands"][r]["bands"] != b:
        bad += 1
        print("DIFERENTE bandas R" + r, ws["bands"][r]["bands"], "!=", b)
print("tablas IARU: %s" % ("OK" if not bad else "%d diferencias" % bad))
sys.exit(1 if bad else 0)
