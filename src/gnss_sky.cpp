/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+.
 * Licensed under the GNU General Public License, version 3 or later.
 */

#include "gnss_sky.h"
#include <math.h>

namespace GNSS_Sky {

    static const uint8_t  MAX_SATS = 48;
    static const uint32_t SAT_TTL  = 12000;          // el u-blox M10 envia GSV cada 5 s por defecto
    static const uint32_t DOP_TTL  = 10000;

    Sat      sats[MAX_SATS];
    uint8_t  satCount = 0;
    float    dopP = NAN, dopH = NAN, dopV = NAN, errLat = NAN, errLon = NAN;
    uint32_t dopMs = 0, errMs = 0;

    char     line[100];
    uint8_t  len = 0;
    bool     inLine = false;

    // Sistema del talker ("GP" GPS, "GL" GLONASS, "GA" Galileo, "GB"/"BD" BeiDou, "GQ"/"QZ" QZSS, "GI" NavIC)
    char talkerSys(const char* t) {
        if (t[0] == 'G' && t[1] == 'P') return 'G';
        if (t[0] == 'G' && t[1] == 'L') return 'R';
        if (t[0] == 'G' && t[1] == 'A') return 'E';
        if ((t[0] == 'G' && t[1] == 'B') || (t[0] == 'B' && t[1] == 'D')) return 'C';
        if ((t[0] == 'G' && t[1] == 'Q') || (t[0] == 'Q' && t[1] == 'Z')) return 'J';
        if ((t[0] == 'G' && t[1] == 'I') || (t[0] == 'I' && t[1] == 'R')) return 'I';   // NavIC (UC6580 del Heltec Wireless Tracker)
        return 'N';
    }

    // Sistema de GSA en NMEA 4.10/4.11 (ultimo campo): 1 GPS, 2 GLONASS, 3 Galileo, 4 BeiDou, 5 QZSS, 6 NavIC
    char gsaSys(int id) {
        switch (id) { case 1: return 'G'; case 2: return 'R'; case 3: return 'E'; case 4: return 'C'; case 5: return 'J'; case 6: return 'I'; default: return 0; }
    }

    // SBAS dentro de GP (numeracion NMEA de u-blox: 33-64) y QZSS 193-202 en algunos receptores
    char refine(char sys, uint16_t svid) {
        if (sys == 'G' && svid >= 33 && svid <= 64) return 'S';
        if (sys == 'G' && svid >= 193 && svid <= 202) return 'J';
        return sys;
    }

    // Divide la frase (sin '$' ni '*xx') en campos; devuelve cuantos hay
    uint8_t split(char* s, char** f, uint8_t maxFields) {
        uint8_t n = 0;
        f[n++] = s;
        for (char* p = s; *p && n < maxFields; p++) {
            if (*p == ',') { *p = 0; f[n++] = p + 1; }
        }
        return n;
    }

    int toInt(const char* s, int def) { return (s && *s) ? atoi(s) : def; }

    Sat* find(char sys, uint16_t svid, bool create) {
        const uint32_t now = millis();
        int8_t oldest = -1;
        for (uint8_t i = 0; i < satCount; i++) {
            if (sats[i].sys == sys && sats[i].svid == svid) return &sats[i];
            if (now - sats[i].seenMs > SAT_TTL && oldest < 0) oldest = i;
        }
        if (!create) return nullptr;
        if (satCount < MAX_SATS) return &sats[satCount++];
        if (oldest >= 0) return &sats[oldest];
        return nullptr;
    }

    // $GPGSV,3,1,11,03,03,111,00,04,15,270,00,06,01,010,00,13,06,292,00*74 (en 4.10 un campo mas: senal)
    void onGsv(char sys, char** f, uint8_t n) {
        const uint32_t now = millis();
        const bool firstMsg = toInt(f[2], 0) == 1;
        // grupos de 4 campos desde el 4.o; en NMEA 4.10 queda un campo de senal suelto al final
        for (uint8_t i = 4; i + 3 < n; i += 4) {
            const int svid = toInt(f[i], -1);
            if (svid <= 0) continue;
            const char s = refine(sys, (uint16_t)svid);
            Sat* p = find(s, (uint16_t)svid, true);
            if (!p) continue;
            const int snr = toInt(f[i + 3], -1);
            const bool fresh = p->sys != s || p->svid != svid || now - p->seenMs > 3000 || firstMsg;
            if (p->sys != s || p->svid != svid) p->used = false;
            p->sys  = s;
            p->svid = (uint16_t)svid;
            p->elev = (int8_t)toInt(f[i + 1], -1);
            p->azim = (int16_t)toInt(f[i + 2], -1);
            // con varias senales (L1 / L5) se queda la mejor de la misma tanda
            p->snr  = fresh ? (int8_t)snr : (int8_t)max((int)p->snr, snr);
            p->seenMs = now;
        }
    }

    // $GNGSA,A,3,80,71,73,79,69,,,,,,,,1.83,1.09,1.47,2*0B (4.10: sistema al final)
    void onGsa(char talker, char** f, uint8_t n) {
        if (n < 18) return;
        char sys = talker;
        if (n >= 19) { const char s = gsaSys(toInt(f[18], 0)); if (s) sys = s; }
        if (sys == 'N') return;
        uint16_t ids[12];
        uint8_t k = 0;
        for (uint8_t i = 3; i <= 14; i++) { const int v = toInt(f[i], -1); if (v > 0) ids[k++] = (uint16_t)v; }
        for (uint8_t i = 0; i < satCount; i++) {
            Sat& s = sats[i];
            const bool sameSys = s.sys == sys || (sys == 'G' && s.sys == 'S') || (sys == 'G' && s.sys == 'J');
            if (!sameSys) continue;
            bool u = false;
            for (uint8_t j = 0; j < k; j++) if (ids[j] == s.svid) { u = true; break; }
            s.used = u;
        }
        if (*f[15]) dopP = atof(f[15]);
        if (*f[16]) dopH = atof(f[16]);
        if (*f[17]) dopV = atof(f[17]);
        dopMs = millis();
    }

    // $GPGST,hhmmss.ss,rms,smaj,smin,orient,sigLat,sigLon,sigAlt*cs
    void onGst(char** f, uint8_t n) {
        if (n < 9 || !*f[6] || !*f[7]) return;
        errLat = atof(f[6]);
        errLon = atof(f[7]);
        errMs  = millis();
    }

    uint8_t hexVal(char c) { return (c >= '0' && c <= '9') ? c - '0' : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : 0xFF; }

    void onLine() {
        line[len] = 0;
        char* star = strchr(line, '*');
        if (!star || star - line < 6) return;
        uint8_t cs = 0;
        for (char* p = line; p < star; p++) cs ^= (uint8_t)*p;
        const uint8_t h = hexVal(star[1]), l = hexVal(star[2]);
        if (h == 0xFF || l == 0xFF || cs != (uint8_t)((h << 4) | l)) return;
        *star = 0;
        const char* type = line + 2;
        char* f[24];
        if (strncmp(type, "GSV", 3) == 0) {
            const char sys = talkerSys(line);
            if (sys == 'N') return;
            const uint8_t n = split(line, f, 24);
            onGsv(sys, f, n);
        } else if (strncmp(type, "GSA", 3) == 0) {
            const char sys = talkerSys(line);
            const uint8_t n = split(line, f, 24);
            onGsa(sys, f, n);
        } else if (strncmp(type, "GST", 3) == 0) {
            const uint8_t n = split(line, f, 24);
            onGst(f, n);
        }
    }

    void feed(char c) {
        if (c == '$') { inLine = true; len = 0; return; }
        if (!inLine) return;
        if (c == '\r' || c == '\n') { inLine = false; onLine(); return; }
        if (c < 0x20 || c > 0x7E || len >= sizeof(line) - 1) { inLine = false; return; }   // UBX binario o frase demasiado larga
        line[len++] = c;
    }

    uint8_t count() {
        const uint32_t now = millis();
        uint8_t n = 0;
        for (uint8_t i = 0; i < satCount; i++) if (now - sats[i].seenMs <= SAT_TTL) n++;
        return n;
    }

    const Sat* sat(uint8_t index) {
        const uint32_t now = millis();
        uint8_t n = 0;
        for (uint8_t i = 0; i < satCount; i++) {
            if (now - sats[i].seenMs > SAT_TTL) continue;
            if (n++ == index) return &sats[i];
        }
        return nullptr;
    }

    float pdop() { return millis() - dopMs <= DOP_TTL ? dopP : NAN; }
    float hdop() { return millis() - dopMs <= DOP_TTL ? dopH : NAN; }
    float vdop() { return millis() - dopMs <= DOP_TTL ? dopV : NAN; }
    float errorM() { return (millis() - errMs <= DOP_TTL && !isnan(errLat) && !isnan(errLon)) ? sqrtf(errLat * errLat + errLon * errLon) : NAN; }

}
