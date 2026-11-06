#!/usr/bin/env python3
"""Genera sats.txt para el modo SAT del tracker: TLE (CelesTrak) + frecuencias de 70 cm (SatNOGS DB).

Categorias (3er campo de la linea S|):
    U  70 cm: transmisor activo cuya subida o bajada cae en 420-450 MHz (valor por defecto)
    L  LoRa / LR-FHSS / TinyGS: transmisor activo con modo LoRa o LR-FHSS en cualquier banda (433, 868, 915 MHz, 2,4 GHz...), y los LACUNASAT
    G  geoestacionarios (Inmarsat): seguimiento y frecuencias nominales de la banda L (para SDR u otros equipos); el tracker no los recibe
Formato (ver include/sat_core.h):
    S|<norad>|<nombre>|<categoria>
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

CELESTRAK_GROUPS = ["amateur", "cubesat", "satnogs", "weather", "stations", "science", "engineering", "active"]       # "active" es grande (~10 000 TLE) pero evita cientos de consultas por satelite
CELESTRAK_URL = "https://celestrak.org/NORAD/elements/gp.php?GROUP=%s&FORMAT=tle"
CELESTRAK_CAT_URL = "https://celestrak.org/NORAD/elements/gp.php?CATNR=%d&FORMAT=tle"
SATNOGS_URL = "https://db.satnogs.org/api/transmitters/?format=json"
BAND_LO, BAND_HI = 420_000_000, 450_000_000
MAX_CATNR_FALLBACK = 120
MAX_TX_PER_SAT = 4
TIME_BUDGET_S = 480                # tiempo maximo de las descargas de respaldo (por satelite)
START = time.time()
MAX_BYTES = 62000                  # la particion de datos del tracker es de 192 kB (archivo viejo + nuevo durante la descarga)
# Frecuencias nominales de la banda L movil de Inmarsat (UIT / planes publicos de frecuencias); el canal concreto depende del satelite y de la region: verificar
INMARSAT_TX = [
    "T|1626500000|1660500000|1525000000|1559000000|L-band|0|0|Inmarsat L-band up/dn",
    "T|0|0|1537100000|1537100000|STD-C|600|0|STD-C TDM (verify)",
    "T|0|0|1541450000|1541450000|STD-C|600|0|STD-C TDM (verify)",
]
NAME_QUERIES = {"INMARSAT": "G"}
LORA_NAMES = ("LACUNA",)           # satelites LR-FHSS / LoRa que SatNOGS puede no marcar con modo LoRa
NAME_URL = "https://celestrak.org/NORAD/elements/gp.php?NAME=%s&FORMAT=tle"
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


def is_lora(mode):
    m = (mode or "").lower().replace("_", "").replace(" ", "")
    return "lora" in m or "lrfhss" in m or "lr-fhss" in m or "fhss" in m


def mean_motion(l2):
    try:
        return float(l2[52:63])
    except ValueError:
        return 0.0


def is_geo_tle(l2):
    return 0.99 < mean_motion(l2) < 1.01


def transmitters_by_sat(data):
    """Lista de transmisores de SatNOGS -> {norad: (categoria, [linea T|...])}: solo activos, de 70 cm o con modo LoRa / LR-FHSS"""
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
        mode = str(pick(t, "mode") or "").replace("|", "/")[:9]
        lora = is_lora(mode)
        if not (in_band(ul) or in_band(dl) or lora):
            continue
        baud = num(pick(t, "baud"))
        inv = 1 if pick(t, "invert") else 0
        desc = str(pick(t, "description") or "").replace("|", "/").replace("\n", " ")[:25]
        cat, lines = res.get(norad, ("U", []))
        if lora:
            cat = "L"
        lines.append("T|%d|%d|%d|%d|%s|%d|%d|%s" % (ul, uh, dl, dh, mode, baud, inv, desc))
        res[norad] = (cat, lines)
    return res


def build(tles, tx, geo, stamp):
    """tx: {norad: (cat, [T|...])}; geo: {norad} de satelites de solo seguimiento. Prioridad ante el limite de tamano: G, L, U"""
    head = ["# APRS LoRa+ satellites (70 cm, LoRa, GEO) %s" % stamp,
            "# TLE: CelesTrak. Transmitters: SatNOGS DB (CC BY-SA 4.0)."]
    entries = []
    for norad, (cat, tl) in tx.items():
        if norad in tles and norad not in geo:
            entries.append((cat, norad, tl[:MAX_TX_PER_SAT if cat == "L" else 3]))
    for norad in geo:
        if norad in tles:
            entries.append(("G", norad, list(INMARSAT_TX)))
    prio = {"G": 0, "L": 1, "U": 2}
    entries.sort(key=lambda e: (prio[e[0]], tles[e[1]][0].upper()))
    out, size, count, dropped = [], sum(len(x) + 1 for x in head), 0, 0
    for cat, norad, tl in entries:
        name, l1, l2 = tles[norad]
        name = name.replace("|", "/") or str(norad)
        block = ["S|%d|%s|%s" % (norad, name, cat), l1, l2] + tl
        n = sum(len(x) + 1 for x in block)
        if size + n > MAX_BYTES:
            dropped += 1
            continue
        out.append((cat, name.upper(), block))
        size += n
        count += 1
    if dropped:
        print("aviso: %d satelites sin sitio (limite %d bytes)" % (dropped, MAX_BYTES), file=sys.stderr)
    out.sort(key=lambda e: (prio[e[0]], e[1]))
    lines = list(head)
    for _, _, block in out:
        lines.extend(block)
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
        data = json.loads(http_get(SATNOGS_URL, tries=2, timeout=120))
        print("SatNOGS descargado (%.0f s)" % (time.time() - START), file=sys.stderr)
    tx = transmitters_by_sat(data)
    print("SatNOGS: %d satelites con transmisores activos (70 cm o LoRa)" % len(tx), file=sys.stderr)

    tles = {}
    if a.tle is not None:
        for p in a.tle:
            with open(p, encoding="utf-8") as f:
                tles.update(parse_tles(f.read()))
    else:
        for g in CELESTRAK_GROUPS:
            try:
                got = parse_tles(http_get(CELESTRAK_URL % g, timeout=120))
                print("CelesTrak %-12s %4d TLE" % (g, len(got)), file=sys.stderr)
                tles.update(got)
            except RuntimeError as e:
                print("aviso: grupo %s no disponible (%s)" % (g, e), file=sys.stderr)
        extra = {}
        for q, c in NAME_QUERIES.items():
            try:
                got = parse_tles(http_get(NAME_URL % q, tries=2, timeout=30))
                print("CelesTrak NAME=%-10s %4d TLE" % (q, len(got)), file=sys.stderr)
                for n, v in got.items():
                    extra[n] = c
                tles.update(got)
            except RuntimeError as e:
                print("aviso: busqueda %s no disponible (%s)" % (q, e), file=sys.stderr)
        for n, v in tles.items():
            if any(k in v[0].upper() for k in LORA_NAMES) and n not in tx:
                tx[n] = ("L", [])
        missing = [n for n in tx if n not in tles][:MAX_CATNR_FALLBACK]
        for n in missing:
            if time.time() - START > TIME_BUDGET_S:
                print("aviso: presupuesto de tiempo agotado; faltan TLE", file=sys.stderr)
                break
            try:
                tles.update(parse_tles(http_get(CELESTRAK_CAT_URL % n, tries=2, timeout=30)))
            except RuntimeError:
                pass
            time.sleep(0.4)
    # Inmarsat y demas geoestacionarios de la busqueda por nombre (periodo ~ 1 dia sideral): solo seguimiento
    geo = {n for n, c in (extra if a.tle is None else {}).items() if c == "G" and n in tles and is_geo_tle(tles[n][2])}
    if a.tle is not None:
        geo = {n for n, v in tles.items() if "INMARSAT" in v[0].upper() and is_geo_tle(v[2])}
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%MZ")
    text, count = build(tles, tx, geo, stamp)
    if count < 3:
        print("ERROR: solo %d satelites; no se publica" % count, file=sys.stderr)
        return 1
    with open(a.output, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("%s: %d satelites, %d bytes" % (a.output, count, len(text)), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
