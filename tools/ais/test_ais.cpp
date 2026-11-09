// Pruebas en el PC del receptor AIS (include/ais_core.h)
//   g++ -std=c++17 -O2 -Wall -Wextra -Iinclude tools/ais/test_ais.cpp src/ais_core.cpp -o /tmp/test_ais && /tmp/test_ais
// Copyright (C) 2026 Manuel - EA7JRS. GPL-3.0-or-later.
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <vector>
#include <random>
#include "ais_core.h"

using namespace AisCore;
static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FALLO: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

// GMSK BT 0,4 a 48 kHz desde los niveles NRZI: filtro gaussiano del NRZ (salida del discriminador de FM), acoplo de alterna, ruido
static std::vector<float> gmsk(const uint8_t* lv, size_t nb, float fs, float hpfHz, float noise, unsigned seed, float offset) {
    const float sps = fs / BAUD, bt = 0.4f;
    const float sigma = sqrtf(logf(2.0f)) / (2.0f * (float)M_PI * bt) * sps;
    const int half = (int)(3 * sps);
    std::vector<float> g(2 * half + 1); float gs = 0;
    for (int i = -half; i <= half; i++) { g[i + half] = expf(-0.5f * i * i / (sigma * sigma)); gs += g[i + half]; }
    for (float& x : g) x /= gs;
    const size_t lead = (size_t)(fs * 0.004f), n = (size_t)(nb * sps) + 2 * lead;
    std::vector<float> nrz(n, 0.0f), out(n, 0.0f);
    for (size_t i = 0; i < n; i++) {
        const long k = (long)floorf((float)((long)i - (long)lead) / sps + offset);
        nrz[i] = (k >= 0 && (size_t)k < nb) ? (lv[k] ? 0.5f : -0.5f) : 0.0f;
    }
    for (size_t i = 0; i < n; i++) { float a = 0; for (int j = -half; j <= half; j++) { const long t = (long)i + j; if (t >= 0 && (size_t)t < n) a += g[j + half] * nrz[t]; } out[i] = a; }
    if (hpfHz > 0) {                                          // acoplo de alterna (primer orden)
        const float rc = 1.0f / (2.0f * (float)M_PI * hpfHz), dt = 1.0f / fs, al = rc / (rc + dt);
        float py = 0, px = 0;
        for (size_t i = 0; i < n; i++) { const float y = al * (py + out[i] - px); px = out[i]; py = y; out[i] = y; }
    }
    std::mt19937 rng(seed); std::normal_distribution<float> nd(0.0f, 1.0f);
    for (float& x : out) x += noise * nd(rng);
    return out;
}

struct Got { int n = 0; uint8_t last[MAX_MSG_BYTES]; size_t len = 0; };
static void onFrame(const uint8_t* m, size_t n, void* ctx) { Got* g = (Got*)ctx; g->n++; memcpy(g->last, m, n); g->len = n; }

int main() {
    // ---- NMEA: vectores conocidos (gpsd / pyais) ----
    NmeaReader rd;
    CHECK(rd.feed("!AIVDM,1,1,,B,15NG6V0P01G?cFhE`R2IU?wn28R>,0*05\r\n") && rd.nbits == 168, "type 1 nmea");
    Report r;
    CHECK(decode(rd.data, rd.nbits, r), "type 1 decode");
    CHECK(r.type == 1 && r.mmsi == 367380120 && r.status == 0 && fabs(r.sogKn - 0.1f) < 1e-4 && r.hasPos, "type 1 fields: %u %u %.1f", r.mmsi, r.status, r.sogKn);
    CHECK(fabs(r.lon - -122.404333) < 1e-5 && fabs(r.lat - 37.806948) < 1e-5, "type 1 pos %.6f %.6f", r.lat, r.lon);
    CHECK(fabs(r.cogDeg - 245.2f) < 1e-3 && r.hasCog && r.hdg == 511 && !r.hasHdg, "type 1 course 245.2, heading n/a");
    CHECK(rd.feed("!AIVDM,1,1,,B,177KQJ5000G?tO`K>RA1wUbN0TKH,0*5C") && decode(rd.data, rd.nbits, r) && r.mmsi == 477553000 && r.status == 5, "type 1 moored %u %u", r.mmsi, r.status);
    CHECK(fabs(r.lat - 47.582833) < 1e-5 && fabs(r.lon - -122.345832) < 1e-5 && fabs(r.cogDeg - 51.0f) < 1e-3 && r.hdg == 181, "type 1 b: %.6f %.6f %.1f %d", r.lat, r.lon, r.cogDeg, r.hdg);
    CHECK(!rd.feed("!AIVDM,2,1,1,A,55?MbV02;H;s<HtKR20EHE:0@T4@Dn2222222216L961O5Gf0NSQEp6ClRp8,0*1C"), "type 5 part 1 waits");
    CHECK(rd.feed("!AIVDM,2,2,1,A,88888888880,2*25") && rd.nbits == 424, "type 5 assembled %zu", rd.nbits);
    CHECK(decode(rd.data, rd.nbits, r) && r.type == 5 && r.mmsi == 351759000 && r.imo == 9134270, "type 5 ids %u %u", r.mmsi, r.imo);
    CHECK(!strcmp(r.call, "3FOF8") && !strcmp(r.name, "EVER DIADEM") && !strcmp(r.dest, "NEW YORK"), "type 5 texts '%s' '%s' '%s'", r.call, r.name, r.dest);
    CHECK(r.shipType == 70 && r.toBow == 225 && r.toStern == 70 && r.toPort == 1 && r.toStar == 31, "type 5 dims");
    CHECK(r.etaMonth == 5 && r.etaDay == 15 && r.etaHour == 14 && r.etaMinute == 0 && fabs(r.draughtM - 12.2f) < 1e-4, "type 5 eta / draught");
    CHECK(!strcmp(shipTypeName(70), "Cargo") && !strcmp(statusName(5), "Moored"), "names");
    CHECK(!rd.feed("!AIVDM,1,1,,B,177KQJ5000G?tO`K>RA1wUbN0TKH,0*5D") && rd.bad > 0, "bad checksum refused");

    // ---- ida y vuelta NMEA ----
    {
        char out[400];
        const int k = toNmea(rd.data, rd.nbits, 'A', 3, out, sizeof(out));
        CHECK(k == 2, "type 5 -> 2 sentences (%d)", k);
        CHECK(strncmp(out, "!AIVDM,2,1,3,A,55?MbV02;H;s<HtKR20EHE:0@T4@Dn2222222216L961O5Gf0NSQEp6ClRp8", 74) == 0, "armored payload: %.80s", out);
        NmeaReader r2;
        char* l2 = strstr(out, "\r\n") + 2;
        char l1[120]; memcpy(l1, out, l2 - out); l1[l2 - out] = 0;
        r2.feed(l1);
        CHECK(r2.feed(l2) && r2.nbits == 424 && memcmp(r2.data, rd.data, 53) == 0, "round trip type 5");
    }

    // ---- CRC (HDLC) ----
    { const uint8_t t[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' }; CHECK(crcX25(t, 9) == 0x906E, "CRC-16/X-25 check value %04X", crcX25(t, 9)); }

    // ---- modem: tramas generadas con ruido y acoplo de alterna ----
    NmeaReader src; src.feed("!AIVDM,1,1,,B,15NG6V0P01G?cFhE`R2IU?wn28R>,0*05");
    const uint8_t* msg = src.data; const size_t nbytes = 21;
    uint8_t lv[2000];
    const size_t nb = frameBits(msg, nbytes, lv, sizeof(lv));
    CHECK(nb > 200, "frame bits %zu", nb);
    const float noises[] = { 0.0f, 0.05f, 0.10f, 0.15f };
    for (float nz : noises) {
        int ok = 0, okNoComp = 0;
        const int N = 20;
        for (int s = 0; s < N; s++) {
            auto x = gmsk(lv, nb, 48000.0f, 320.0f, nz, (unsigned)(s + 1), (s % 7) / 7.0f);
            Demod d; d.begin(48000.0f, 320.0f);
            Got g; for (float v : x) d.feed(v, onFrame, &g);
            if (g.n == 1 && g.len == nbytes && memcmp(g.last, msg, nbytes) == 0) ok++;
            Demod d2; d2.begin(48000.0f, 0.0f);
            Got g2; for (float v : x) d2.feed(v, onFrame, &g2);
            if (g2.n == 1) okNoComp++;
        }
        printf("ruido %.2f: %d/%d (sin compensar el acoplo %d/%d)\n", nz, ok, N, okNoComp, N);
        if (nz <= 0.10f) CHECK(ok >= N - 1, "modem noise %.2f: %d/%d", nz, ok, N);
    }
    {   // tipo 5 (dos ranuras, 53 octetos) sin ruido
        size_t n5 = frameBits(rd.data, 53, lv, sizeof(lv));
        auto x = gmsk(lv, n5, 48000.0f, 320.0f, 0.03f, 99, 0.3f);
        Demod d; d.begin(48000.0f, 320.0f); Got g; for (float v : x) d.feed(v, onFrame, &g);
        CHECK(g.n == 1 && g.len == 53 && memcmp(g.last, rd.data, 53) == 0, "modem type 5");
    }
    {   // sin senal: ningun falso
        auto x = gmsk(lv, 0, 48000.0f, 320.0f, 0.3f, 5, 0); x.resize(48000 * 5);
        std::mt19937 rng(7); std::normal_distribution<float> nd(0, 0.3f); for (float& v : x) v = nd(rng);
        Demod d; d.begin(48000.0f, 320.0f); Got g; for (float v : x) d.feed(v, onFrame, &g);
        CHECK(g.n == 0, "noise only: %d frames", g.n);
    }

    // ---- tabla, distancia y CPA ----
    {
        Table t;
        Own o{ 37.80, -122.40, 10.0f, 0.0f, true };            // rumbo norte a 10 nudos
        Report a; NmeaReader q; q.feed("!AIVDM,1,1,,B,15NG6V0P01G?cFhE`R2IU?wn28R>,0*05"); decode(q.data, q.nbits, a);
        Vessel* v = t.update(a, 1000, 'B');
        CHECK(v && t.count() == 1 && v->hasPos && v->cls == CL_A, "table add");
        double nm, br; rangeBearing(o, v->lat, v->lon, nm, br);
        CHECK(nm > 0.45 && nm < 0.48 && br > 330 && br < 338, "range %.3f nm bearing %.1f", nm, br);
        double c, tc;
        CHECK(cpa(o, *v, c, tc) && tc > 2.3 && tc < 2.7 && c > 0.18 && c < 0.24, "cpa %.3f nm tcpa %.2f min", c, tc);
        q.feed("!AIVDM,2,1,1,A,55?MbV02;H;s<HtKR20EHE:0@T4@Dn2222222216L961O5Gf0NSQEp6ClRp8,0*1C"); q.feed("!AIVDM,2,2,1,A,88888888880,2*25");
        Report b; decode(q.data, q.nbits, b); t.update(b, 2000, 'A');
        Vessel* w = t.find(351759000);
        CHECK(w && !strcmp(w->name, "EVER DIADEM") && w->length == 295 && w->beam == 32 && !w->hasPos, "table static");
        int idx[Table::CAP]; const int m = t.sorted(o, idx, Table::CAP);
        CHECK(m == 2 && t.at(idx[0])->mmsi == 367380120, "sorted by distance");
        t.purge(601500, 600000);
        CHECK(t.count() == 1 && t.find(351759000), "purge old");
        CHECK(isDistress(970123456) && !strcmp(distressName(972000001), "MOB") && !isDistress(224000000), "SART / MOB / EPIRB");
    }

    printf(fails ? "%d FALLOS de %d\n" : "OK: %d comprobaciones\n", fails ? fails : checks, checks);
    return fails ? 1 : 0;
}
