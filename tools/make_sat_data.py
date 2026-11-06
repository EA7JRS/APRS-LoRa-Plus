#!/usr/bin/env python3
"""Genera sats.txt para el modo SAT del tracker: TLE (CelesTrak) + frecuencias de 70 cm (SatNOGS DB).

Solo incluye satelites con TLE y con un transmisor activo cuya subida o bajada cae en 420-450 MHz.
Formato (ver include/sat_core.h):
    S|<norad>|<nombre>
    1 ...
    2 ...
    T|<subida_min>|<subida_max>|<bajada_min>|<bajada_max>|<modo>|<baudios>|<inversor>|<descripcion>

Uso:  make_sat_data.py salida.txt                     (descarga de internet)
      make_sat_data.py salida.txt --tle f1.txt [f2.txt ...] --transmitters t.json   (sin red, para pruebas)

Copyright (c) 2026 Manuel - EA7JRS. GPL-3.0-or-later. Datos: CelesTrak (https://celestrak.org) y SatNOGS DB (https://db.satnogs.org, CC BY-SA 4.0).
"""
import argparse
import datetime
import json
import sys
import time
import urllib.request
import urllib.error

CELESTRAK_GROUPS = ["amateur", "cubesat", "satnogs", "weather", "stations", "science", "engineering"]
CELESTRAK_URL = "https://celestrak.org/NORAD/elements/gp.php?GROUP=%s&FORMAT=tle"
CELESTRAK_CAT_URL = "https://celestrak.org/NORAD/elements/gp.php?CATNR=%d&FORMAT=tle"
SATNOGS_URL = "https://db.satnogs.org/api/transmitters/?format=json"
BAND_LO, BAND_HI = 420_000_000, 450_000_000
MAX_CATNR_FALLBACK = 60
UA = "APRS-LoRa-Plus-satdata/1.0 (+https://github.com/EA7JRS/APRS-LoRa-Plus)"


def http_get(url, tries=3, timeout=60):
    last = None
    for i in range(tries):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": UA})
            with urllib.request.urlopen(req, timeout=timeout) as r:
                return r.read().decode("utf-8", "replace")
        except (urllib.error.URLError, TimeoutError, OSError) as e:
            last = e
            time.sleep(2 + 3 * i)
    raise RuntimeError("%s: %s" % (url, last))


def checksum_ok(line):
    if len(line) < 69:
        return False
    s = 0
    for c in line[:68]:
        if c.isdigit():
            s += int(c)
        elif c == "-":
            s += 1
    return line[68].isdigit() and s % 10 == int(line[68])


def parse_tles(text):
    """Texto de TLE de 3 lineas (nombre, 1, 2) -> {norad: (nombre, l1, l2)}"""
    out = {}
    lines = [l.rstrip("\r\n") for l in text.split("\n")]
    i = 0
    while i + 2 < len(lines) + 0:
        name, l1, l2 = lines[i].strip(), lines[i + 1], lines[i + 2]
        if l1.startswith("1 ") and l2.startswith("2 ") and checksum_ok(l1) and checksum_ok(l2):
            try:
                norad = int(l1[2:7])
            except ValueError:
                i += 1
                continue
            out[norad] = (name[:24], l1[:69], l2[:69])
            i += 3
        else:
            i += 1
    return out


def num(v):
    try:
        return int(float(v))
    except (TypeError, ValueError):
        return 0


def pick(d, *keys):
    for k in keys:
        if k in d and d[k] is not None:
            return d[k]
    return None


def in_band(hz):
    return BAND_LO <= hz <= BAND_HI


def transmitters_by_sat(data):
    """Lista de transmisores de SatNOGS -> {norad: [linea T|...]} solo activos y de 70 cm"""
    if isinstance(data, dict):
        data = data.get("results", [])
    res = {}
    for t in data:
        if not isinstance(t, dict):
            continue
        if pick(t, "alive") is False:
            continue
        status = pick(t, "status")
        if status is not None and str(status).lower() != "active":
            continue
        norad = num(pick(t, "norad_cat_id", "norad_id"))
        if norad <= 0:
            continue
        ul, uh = num(pick(t, "uplink_low", "uplink_low_hz")), num(pick(t, "uplink_high", "uplink_high_hz"))
        dl, dh = num(pick(t, "downlink_low", "downlink_low_hz")), num(pick(t, "downlink_high", "downlink_high_hz"))
        if ul and not uh:
            uh = ul
        if dl and not dh:
            dh = dl
        if not (in_band(ul) or in_band(dl)):
            continue
        mode = str(pick(t, "mode") or "").replace("|", "/")[:9]
        baud = num(pick(t, "baud"))
        inv = 1 if pick(t, "invert") else 0
        desc = str(pick(t, "description") or "").replace("|", "/").replace("\n", " ")[:25]
        res.setdefault(norad, []).append("T|%d|%d|%d|%d|%s|%d|%d|%s" % (ul, uh, dl, dh, mode, baud, inv, desc))
    return res


def build(tles, tx, stamp):
    lines = ["# APRS LoRa+ satellites (70 cm) %s" % stamp,
             "# TLE: CelesTrak. Transmitters: SatNOGS DB (CC BY-SA 4.0)."]
    count = 0
    for norad in sorted(tx, key=lambda n: tles[n][0].upper() if n in tles else ""):
        if norad not in tles:
            continue
        name, l1, l2 = tles[norad]
        name = name.replace("|", "/") or str(norad)
        lines.append("S|%d|%s" % (norad, name))
        lines.append(l1)
        lines.append(l2)
        # los de mas banda ancha primero: maximo 6 por satelite
        for t in tx[norad][:6]:
            lines.append(t)
        count += 1
    return "\n".join(lines) + "\n", count


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("output")
    ap.add_argument("--tle", nargs="*", help="archivos TLE (sin red)")
    ap.add_argument("--transmitters", help="JSON de SatNOGS (sin red)")
    a = ap.parse_args()

    if a.transmitters:
        with open(a.transmitters, encoding="utf-8") as f:
            data = json.load(f)
    else:
        data = json.loads(http_get(SATNOGS_URL, timeout=180))
    tx = transmitters_by_sat(data)
    print("SatNOGS: %d satelites con transmisores activos en 70 cm" % len(tx), file=sys.stderr)

    tles = {}
    if a.tle is not None:
        for p in a.tle:
            with open(p, encoding="utf-8") as f:
                tles.update(parse_tles(f.read()))
    else:
        for g in CELESTRAK_GROUPS:
            try:
                got = parse_tles(http_get(CELESTRAK_URL % g))
                print("CelesTrak %-12s %4d TLE" % (g, len(got)), file=sys.stderr)
                tles.update(got)
            except RuntimeError as e:
                print("aviso: grupo %s no disponible (%s)" % (g, e), file=sys.stderr)
        missing = [n for n in tx if n not in tles][:MAX_CATNR_FALLBACK]
        for n in missing:
            try:
                tles.update(parse_tles(http_get(CELESTRAK_CAT_URL % n, tries=2, timeout=30)))
            except RuntimeError:
                pass
            time.sleep(0.4)
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%MZ")
    text, count = build(tles, tx, stamp)
    if count < 3:
        print("ERROR: solo %d satelites; no se publica" % count, file=sys.stderr)
        return 1
    with open(a.output, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("%s: %d satelites, %d bytes" % (a.output, count, len(text)), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
