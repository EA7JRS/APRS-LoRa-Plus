// Pruebas en el PC del nucleo de satelites (include/sat_core.h)
//   g++ -O2 -std=c++17 -Iinclude -o /tmp/test_sat_core tools/test_sat_core.cpp src/sat_core.cpp src/sgp4/SGP4.cpp
//   /tmp/test_sat_core <SGP4-VER.TLE> <tcppver.out>          (verificacion de Vallado / python-sgp4)
//   /tmp/test_sat_core --look  <l1> <l2> <lat> <lon> <altM> <unix> [n step]    (azimut / elevacion / Doppler para comparar con la referencia en Python)
//   /tmp/test_sat_core --passes <l1> <l2> <lat> <lon> <altM> <unixDesde> <horizonteS> <elMin>
//
// Copyright (c) 2026 Manuel - EA7JRS. GPL-3.0-or-later.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string>
#include <vector>
#include "sat_core.h"

using namespace SatCore;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FALLO: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

struct Tle { std::string l1, l2; };

static std::vector<Tle> readTles(const char* path) {
    std::vector<Tle> v;
    FILE* f = fopen(path, "r");
    if (!f) { printf("no se abre %s\n", path); exit(2); }
    char line[256];
    std::string l1;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (s.size() > 1 && s[0] == '1' && s[1] == ' ') l1 = s;
        else if (s.size() > 1 && s[0] == '2' && s[1] == ' ' && !l1.empty()) { v.push_back(Tle{ l1, s }); l1.clear(); }
    }
    fclose(f);
    return v;
}

static int verify(const char* tlePath, const char* outPath) {
    const std::vector<Tle> tles = readTles(tlePath);
    FILE* f = fopen(outPath, "r");
    if (!f) { printf("no se abre %s\n", outPath); return 2; }
    char line[512];
    int idx = -1, rows = 0, satsOk = 0, satsSkipped = 0;
    Sat sat;
    bool have = false;
    double worst = 0;
    while (fgets(line, sizeof line, f)) {
        if (strstr(line, " xx")) {
            idx++;
            have = false;
            if (idx >= (int)tles.size()) { printf("mas bloques que TLE\n"); return 2; }
            if (parseTle("t", tles[idx].l1.c_str(), tles[idx].l2.c_str(), sat)) { have = true; satsOk++; }
            else { satsSkipped++; printf("  (TLE %d no aceptado por el parser: %s)\n", idx, tles[idx].l1.substr(2, 5).c_str()); }
            continue;
        }
        if (line[0] == '#' || !have) continue;
        double t, r[3], v[3];
        if (sscanf(line, "%lf %lf %lf %lf %lf %lf %lf", &t, &r[0], &r[1], &r[2], &v[0], &v[1], &v[2]) != 7) continue;
        double rr[3], vv[3];
        if (!SGP4Funcs::sgp4(sat.rec, t, rr, vv)) { CHECK(false, "sgp4 fallo en sat %d t=%.1f", idx, t); continue; }
        double d = 0, dv = 0;
        for (int i = 0; i < 3; i++) { d = fmax(d, fabs(rr[i] - r[i])); dv = fmax(dv, fabs(vv[i] - v[i])); }
        rows++;
        if (d > worst) worst = d;
        CHECK(d < 2e-5 && dv < 2e-8, "sat %d (%s) t=%.1f: dr=%.3g km dv=%.3g km/s", idx, tles[idx].l1.substr(2, 5).c_str(), t, d, dv);
    }
    fclose(f);
    printf("SGP4: %d satelites, %d omitidos, %d filas comparadas, peor error de posicion %.3g km\n", satsOk, satsSkipped, rows, worst);
    return 0;
}

static int lookMode(char** a, int n) {
    Sat s;
    if (!parseTle("x", a[0], a[1], s)) { printf("TLE no valido\n"); return 2; }
    Observer o{ atof(a[2]), atof(a[3]), atof(a[4]) };
    double t = atof(a[5]);
    int count = n > 6 ? atoi(a[6]) : 1;
    double step = n > 7 ? atof(a[7]) : 1;
    for (int i = 0; i < count; i++, t += step) {
        Look l;
        if (!look(s, o, t, l)) { printf("%.1f ERR\n", t); continue; }
        printf("%.1f %.6f %.6f %.6f %.9f %.6f %.6f %.6f\n", t, l.azDeg, l.elDeg, l.rangeKm, l.rangeRateKmS, l.subLatDeg, l.subLonDeg, l.altKm);
    }
    return 0;
}

static int passMode(char** a) {
    Sat s;
    if (!parseTle("x", a[0], a[1], s)) { printf("TLE no valido\n"); return 2; }
    Observer o{ atof(a[2]), atof(a[3]), atof(a[4]) };
    double from = atof(a[5]), horizon = atof(a[6]), minEl = atof(a[7]);
    double t = from;
    const double end = from + horizon;
    while (t < end) {
        PassSearch ps;
        passSearchStart(ps, t, end - t, minEl);
        int guard = 0;
        while (passSearchAdvance(ps, s, o, 50) < 2 && ++guard < 100000) {}
        if (ps.phase != 2) break;
        printf("PASS %.1f %.1f %.1f %.3f %.2f %.2f\n", ps.pass.aosUnix, ps.pass.tcaUnix, ps.pass.losUnix, ps.pass.maxElDeg, ps.pass.aosAzDeg, ps.pass.losAzDeg);
        t = ps.pass.losUnix + 60.0;
    }
    return 0;
}

static int catalogMode(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { printf("no se abre %s\n", path); return 2; }
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    fclose(f);
    size_t pos = 0;
    int blocks = 0, good = 0;
    while (pos < text.size()) {
        Block b;
        size_t used = parseBlock(text.c_str() + pos, text.size() - pos, b);
        if (used == 0) break;
        pos += used;
        blocks++;
        Sat s;
        const bool ok = parseTle(b.name, b.line1, b.line2, s) && s.noradId == b.noradId;
        if (ok) good++;
        printf("%6d %-24s TLE %s  tx=%d", b.noradId, b.name, ok ? "ok" : "MAL", b.txCount);
        for (int i = 0; i < b.txCount; i++) printf("  [%s up %ld dn %ld %s %d%s '%s' 70cm=%d]", b.tx[i].mode, txUpCenter(b.tx[i]), txDownCenter(b.tx[i]), b.tx[i].invert ? "inv" : "", b.tx[i].baud, "", b.tx[i].desc, (int)txIn70cm(b.tx[i]));
        printf("\n");
    }
    CHECK(blocks > 0 && good == blocks, "catalogo: %d bloques, %d TLE validos", blocks, good);
    return 0;
}

static void selfTests() {
    // fechas
    CHECK(unixFromCivil(1970, 1, 1, 0, 0, 0) == 0.0, "epoca unix");
    CHECK(fabs(unixFromCivil(2000, 1, 1, 12, 0, 0) - 946728000.0) < 1e-6, "J2000");
    CHECK(fabs(unixFromCivil(2026, 11, 6, 0, 0, 0) - 1793923200.0) < 1e-6, "2026-11-06 = %.0f", unixFromCivil(2026, 11, 6, 0, 0, 0));
    // Doppler: 435 MHz, satelite alejandose a 7 km/s
    CHECK(fabs(dopplerDown(435e6, 7.0) - (435e6 - 435e6 * 7.0 / 299792.458)) < 1.0, "doppler bajada");
    CHECK(fabs(dopplerDown(435e6, 7.0) - 435e6 + 10157.0) < 5.0, "Doppler 435 MHz a 7 km/s = -10,16 kHz (%.1f)", dopplerDown(435e6, 7.0) - 435e6);
    CHECK(fabs(dopplerDown(dopplerUpPre(145.9e6, 5.0), 5.0) - 145.9e6) < 1e-3, "subida compensada");
    CHECK(strcmp(cardinal(0), "N") == 0 && strcmp(cardinal(44), "NE") == 0 && strcmp(cardinal(359), "N") == 0 && strcmp(cardinal(181), "S") == 0, "puntos cardinales");
    // TLE incorrecto
    Sat s;
    CHECK(!parseTle("x", "1 00005U 58002B   00179.78495062  .00000023  00000-0  28098-4 0  4750", "2 00005  34.2682 348.7242 1859667 331.7664  19.3264 10.82419157413667", s), "checksum malo aceptado");
    CHECK(!parseTle("x", "corto", "corto", s), "linea corta aceptada");
}

int main(int argc, char** argv) {
    if (argc >= 3 && !strcmp(argv[1], "--look") && argc >= 8) return lookMode(argv + 2, argc - 2);
    if (argc >= 10 && !strcmp(argv[1], "--passes")) return passMode(argv + 2);
    if (argc >= 3 && !strcmp(argv[1], "--catalog")) { int r = catalogMode(argv[2]); printf(failures ? "%d FALLOS\n" : "OK: sin fallos\n", failures); return r ? r : (failures ? 1 : 0); }
    selfTests();
    if (argc >= 3) verify(argv[1], argv[2]);
    printf(failures ? "%d FALLOS\n" : "OK: sin fallos\n", failures);
    return failures ? 1 : 0;
}
