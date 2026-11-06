#!/usr/bin/env python3
# APRS LoRa+ - generador de las tablas de limites de las regiones IARU (UIT RR art. 5.2) y prueba con ciudades de referencia.
# Copyright (C) 2026 Manuel - EA7JRS   (GPL-3.0-or-later)
#
#   python3 tools/gen_iaru_region.py            -> escribe src/iaru_region_data.inc y comprueba las ciudades
#   python3 tools/gen_iaru_region.py --check    -> solo comprueba
#
# R2 = entre la linea A (Atlantico) y la linea C (Pacifico). R1 = al este de la linea A y al oeste de la frontera R1/R3
# (frontera terrestre de Iran, Afganistan, China...). El resto es R3. Los arcos de circulo maximo se densifican en tramos de ~2 grados.
import math, sys, os

def gc(p, q, step=2.0):
    """Puntos (lon, lat) del arco de circulo maximo p->q (sin incluir q), p y q = (lon, lat)."""
    def vec(lon, lat):
        lo, la = math.radians(lon), math.radians(lat)
        return (math.cos(la) * math.cos(lo), math.cos(la) * math.sin(lo), math.sin(la))
    a, b = vec(*p), vec(*q)
    dot = max(-1.0, min(1.0, sum(x * y for x, y in zip(a, b))))
    om = math.acos(dot)
    n = max(1, int(math.degrees(om) / step + 0.5))
    out = []
    for i in range(n):
        t = i / n
        if om < 1e-9:
            v = a
        else:
            s = math.sin(om)
            v = tuple((math.sin((1 - t) * om) * x + math.sin(t * om) * y) / s for x, y in zip(a, b))
        lon = math.degrees(math.atan2(v[1], v[0]))
        lat = math.degrees(math.asin(max(-1.0, min(1.0, v[2]))))
        # mantener la longitud continua con p (el arco no salta de +180 a -180)
        while lon - p[0] > 180: lon -= 360
        while lon - p[0] < -180: lon += 360
        out.append((round(lon, 2), round(lat, 2)))
    return out

def chain(*pts, great=True):
    r = []
    for i in range(len(pts) - 1):
        r += gc(pts[i], pts[i + 1]) if great else [pts[i]]
    r.append(pts[-1])
    return r

# --- Region 2 (lon' en [-195, -10]; las longitudes este > 165 se pasan a lon-360) ---
R2 = []
R2 += [(-10, 90), (-10, 72)]
R2 += gc((-10, 72), (-50, 40)) [1:] + gc((-50, 40), (-20, -10))
R2 += [(-20, -10), (-20, -90), (-120, -90), (-120, 10)]
R2 += [(-170, 10)]                              # paralelo 10 N
R2 += gc((-170, 10), (-180, 28))
R2 += gc((-180, 28), (-195, 50))
R2 += gc((-195, 50), (-169, 65.5))
R2 += [(-169, 65.5), (-169, 90)]

# --- Region 1 (lon en [-50, 191]) ---
BORDER = [(60, 24), (57.6, 25.8), (56.4, 26.85), (54.5, 26.6), (52.5, 27.2), (50.5, 28.3), (48.5, 29.9), (48.05, 30.45), (47.7, 31.0),
          (46.2, 32.9), (45.6, 34.4), (45.2, 35.6), (44.9, 36.6), (44.4, 37.2), (44.3, 38.4), (44.6, 39.6), (45.4, 39.1),
          (46.5, 38.85), (48.0, 38.9), (48.9, 38.4), (51.5, 38.0), (53.9, 37.35), (55.5, 38.0), (57.4, 37.65), (59.2, 37.5),
          (60.0, 36.65), (61.25, 35.62), (61.3, 36.5), (62.5, 37.4), (64.5, 37.1), (67.0, 37.35), (68.5, 37.2), (70.0, 37.5),
          (71.5, 37.9), (72.5, 37.3), (74.0, 37.2), (74.9, 37.2), (75.0, 38.4), (74.0, 39.5), (75.5, 40.5), (78.0, 41.0),
          (80.2, 42.2), (80.4, 44.8), (82.5, 45.2), (83.0, 47.2), (85.5, 47.0), (87.3, 49.1), (88.7, 48.2), (90.5, 47.7),
          (91.0, 46.0), (90.7, 45.5), (93.0, 44.9), (95.5, 44.2), (96.8, 42.8), (100.5, 42.6), (105.0, 41.6), (107.5, 42.4),
          (110.2, 43.0), (111.9, 43.7), (113.6, 44.7), (116.0, 45.7), (117.4, 46.6), (119.9, 46.7), (119.6, 47.6),
          (118.5, 48.0), (117.0, 49.5), (117.8, 49.5), (119.5, 50.3), (120.3, 52.0), (122.0, 53.4), (124.0, 53.3),
          (126.0, 52.8), (127.5, 50.3), (130.0, 48.9), (132.5, 47.7), (134.7, 48.3), (134.2, 47.5), (133.8, 46.2),
          (133.1, 45.2), (131.9, 43.5), (131.2, 42.9), (130.7, 42.3),
          (139.0, 45.3), (142.0, 45.7), (145.4, 43.7), (150.0, 43.0), (165.0, 50.0)]
R1 = [(-10, 90), (-10, 72)]
R1 += gc((-10, 72), (-50, 40))[1:] + gc((-50, 40), (-20, -10))
R1 += [(-20, -10), (-20, -90), (60, -90)]
R1 += BORDER
R1 += gc((165, 50), (191, 65.5))
R1 += [(191, 65.5), (191, 90)]

def inside(poly, x, y):
    c = False
    n = len(poly)
    j = n - 1
    for i in range(n):
        xi, yi = poly[i]; xj, yj = poly[j]
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
            c = not c
        j = i
    return c

def region(lat, lon):
    x = lon - 360 if lon > 165 else lon
    if inside(R2, x, lat): return 2
    x1 = lon + 360 if lon < -168.9 and lat >= 65.0 else lon
    return 1 if inside(R1, x1, lat) else 3

CITIES = [  # (nombre, lat, lon, region esperada)
    ("Madrid", 40.4, -3.7, 1), ("Sevilla", 37.4, -6.0, 1), ("Lisboa", 38.7, -9.1, 1), ("Azores Ponta Delgada", 37.7, -25.7, 1),
    ("Madeira", 32.7, -17.0, 1), ("Canarias", 28.1, -15.4, 1), ("Reykjavik", 64.1, -21.9, 1), ("Jan Mayen", 71.0, -8.5, 1),
    ("Londres", 51.5, -0.1, 1), ("Moscu", 55.8, 37.6, 1), ("Estambul", 41.0, 29.0, 1), ("Ankara", 39.9, 32.9, 1),
    ("El Cairo", 30.0, 31.2, 1), ("Bagdad", 33.3, 44.4, 1), ("Basora", 30.5, 47.8, 1), ("Riad", 24.7, 46.7, 1),
    ("Dubai", 25.2, 55.3, 1), ("Mascate", 23.6, 58.4, 1), ("Kuwait", 29.4, 48.0, 1), ("Tel Aviv", 32.1, 34.8, 1),
    ("Tiflis", 41.7, 44.8, 1), ("Ereván", 40.2, 44.5, 1), ("Bakú", 40.4, 49.9, 1), ("Asjabad", 37.95, 58.4, 1),
    ("Taskent", 41.3, 69.3, 1), ("Almaty", 43.2, 76.9, 1), ("Dusambé", 38.55, 68.8, 1), ("Ulán Bator", 47.9, 106.9, 1),
    ("Novosibirsk", 55.0, 82.9, 1), ("Irkutsk", 52.3, 104.3, 1), ("Jabárovsk", 48.5, 135.1, 1), ("Vladivostok", 43.1, 131.9, 1),
    ("Petropavlovsk", 53.0, 158.6, 1), ("Anádyr", 64.7, 177.5, 1), ("Yuzhno-Sajalinsk", 47.0, 142.7, 1),
    ("Ciudad del Cabo", -33.9, 18.4, 1), ("Nairobi", -1.3, 36.8, 1), ("Lagos", 6.5, 3.4, 1), ("Dakar", 14.7, -17.4, 1),
    ("Cabo Verde Praia", 14.9, -23.5, 1), ("Reunion", -21.1, 55.5, 1), ("Santa Elena", -15.9, -5.7, 1), ("Ascension", -7.9, -14.4, 1),
    ("Svalbard", 78.2, 15.6, 1), ("Tristan da Cunha", -37.1, -12.3, 1), ("Antartida R1", -75.0, 20.0, 1),
    ("Nuuk", 64.2, -51.7, 2), ("Ittoqqortoormiit", 70.5, -21.9, 2), ("Nueva York", 40.7, -74.0, 2), ("Bermudas", 32.3, -64.8, 2),
    ("Ciudad de Mexico", 19.4, -99.1, 2), ("Sao Paulo", -23.5, -46.6, 2), ("Natal", -5.8, -35.2, 2), ("Fernando de Noronha", -3.8, -32.4, 2),
    ("Buenos Aires", -34.6, -58.4, 2), ("Malvinas", -51.7, -57.9, 2), ("Georgias del Sur", -54.3, -36.5, 2), ("Anchorage", 61.2, -149.9, 2),
    ("Attu", 52.9, 173.2, 2), ("Adak", 51.9, -176.6, 2), ("Honolulu", 21.3, -157.9, 2), ("Midway", 28.2, -177.4, 2),
    ("Pascua", -27.1, -109.4, 2), ("Galapagos", -0.7, -90.4, 2), ("Los Angeles", 34.1, -118.2, 2), ("Santiago", -33.4, -70.7, 2),
    ("Antartida R2", -75.0, -60.0, 2), ("Alert (Canada)", 82.5, -62.3, 2), ("Cayena", 4.9, -52.3, 2), ("Johnston", 16.7, -169.5, 2),
    ("Tokio", 35.7, 139.7, 3), ("Sapporo", 43.1, 141.4, 3), ("Wakkanai", 45.4, 141.7, 3), ("Pekin", 39.9, 116.4, 3),
    ("Harbin", 45.8, 126.6, 3), ("Heihe", 50.2, 127.5, 3), ("Urumqi", 43.8, 87.6, 3), ("Kabul", 34.5, 69.2, 3),
    ("Herat", 34.35, 62.2, 3), ("Teheran", 35.7, 51.4, 3), ("Mashhad", 36.3, 59.6, 3), ("Bandar Abbas", 27.2, 56.3, 3),
    ("Tabriz", 38.1, 46.3, 3), ("Abadan", 30.3, 48.3, 3), ("Islamabad", 33.7, 73.0, 3), ("Delhi", 28.6, 77.2, 3),
    ("Male", 4.2, 73.5, 3), ("Bangkok", 13.8, 100.5, 3), ("Singapur", 1.35, 103.8, 3), ("Yakarta", -6.2, 106.8, 3),
    ("Manila", 14.6, 121.0, 3), ("Seul", 37.6, 127.0, 3), ("Pionyang", 39.0, 125.7, 3), ("Sidney", -33.9, 151.2, 3),
    ("Perth", -32.0, 115.9, 3), ("Auckland", -36.9, 174.8, 3), ("Guam", 13.5, 144.8, 3), ("Wake", 19.3, 166.6, 3),
    ("Tahiti", -17.5, -149.6, 3), ("Samoa Americana", -14.3, -170.7, 3), ("Kiritimati", 1.9, -157.4, 3),
    ("Fiyi", -18.1, 178.4, 3), ("Kwajalein", 9.0, 167.7, 3), ("Diego Garcia", -7.3, 72.4, 3), ("Kerguelen", -49.3, 70.2, 3),
    ("Antartida R3", -75.0, 120.0, 3), ("Pitcairn", -25.1, -130.1, 3), ("Hobart", -42.9, 147.3, 3),
]

def check():
    bad = 0
    for name, lat, lon, exp in CITIES:
        got = region(lat, lon)
        if got != exp:
            bad += 1
            print("FALLO  %-22s (%7.2f,%8.2f) esperada R%d, calculada R%d" % (name, lat, lon, exp, got))
    print("%d puntos de referencia, %d fallos" % (len(CITIES), bad))
    return bad

def emit(poly, name):
    s = "static const float %s[][2] = {  // {lon, lat}\n" % name
    for i in range(0, len(poly), 6):
        s += "    " + ", ".join("{%.2ff, %.2ff}" % p for p in poly[i:i + 6]) + ",\n"
    return s + "};\n"

if __name__ == "__main__":
    bad = check()
    if "--check" in sys.argv:
        sys.exit(1 if bad else 0)
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "iaru_region_data.inc")
    with open(out, "w") as f:
        f.write("// GENERADO por tools/gen_iaru_region.py (no editar a mano). Copyright (C) 2026 Manuel - EA7JRS (GPL-3.0-or-later)\n")
        f.write("// Limites de las regiones IARU / UIT (RR art. 5.2), simplificados: R2 entre las lineas A y C; R1 hasta la frontera R1/R3.\n")
        f.write(emit(R2, "IARU_R2") + "static const int IARU_R2_N = %d;\n\n" % len(R2))
        f.write(emit(R1, "IARU_R1") + "static const int IARU_R1_N = %d;\n" % len(R1))
    print("escrito", out, len(R2), len(R1))
    sys.exit(1 if bad else 0)
