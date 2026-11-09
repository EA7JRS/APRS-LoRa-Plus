/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Receptor AIS. Ver include/ais_core.h
 */

#include "ais_core.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

namespace AisCore {

    static constexpr float PI_F = 3.14159265358979f;

    // ------------------------------------------------------------------------------------------------ bits

    uint32_t bitsU(const uint8_t* d, size_t nbits, size_t start, size_t len) {
        uint32_t v = 0;
        for (size_t i = 0; i < len; i++) {
            const size_t b = start + i;
            const uint32_t bit = b < nbits ? (d[b >> 3] >> (7 - (b & 7))) & 1u : 0u;
            v = (v << 1) | bit;
        }
        return v;
    }

    int32_t bitsS(const uint8_t* d, size_t nbits, size_t start, size_t len) {
        uint32_t v = bitsU(d, nbits, start, len);
        if (len < 32 && (v & (1u << (len - 1)))) v |= ~((1u << len) - 1u);
        return (int32_t)v;
    }

    void bitsText(const uint8_t* d, size_t nbits, size_t start, size_t nchars, char* out, size_t outMax) {
        size_t o = 0;
        for (size_t i = 0; i < nchars && o + 1 < outMax; i++) {
            if (start + i * 6 + 6 > nbits) break;
            const uint32_t c = bitsU(d, nbits, start + i * 6, 6);
            out[o++] = (char)(c < 32 ? c + 64 : c);
        }
        out[o] = 0;
        char* at = strchr(out, '@');                // '@' = fin / relleno
        if (at) *at = 0;
        size_t l = strlen(out);
        while (l && out[l - 1] == ' ') out[--l] = 0;
        size_t s = 0;
        while (out[s] == ' ') s++;
        if (s) memmove(out, out + s, strlen(out + s) + 1);
    }

    uint16_t crcX25(const uint8_t* d, size_t n) {
        uint16_t c = 0xFFFF;
        for (size_t i = 0; i < n; i++) {
            c ^= d[i];
            for (int k = 0; k < 8; k++) c = (c & 1) ? (uint16_t)((c >> 1) ^ 0x8408) : (uint16_t)(c >> 1);
        }
        return (uint16_t)~c;
    }

    // ------------------------------------------------------------------------------------------------ mensajes

    bool isDistress(uint32_t m) { const uint32_t p = m / 1000000u; return p == 970 || p == 972 || p == 974; }
    const char* distressName(uint32_t m) {
        switch (m / 1000000u) { case 970: return "AIS-SART"; case 972: return "MOB"; case 974: return "EPIRB"; default: return ""; }
    }

    // posicion: 1/10000 de minuto; 181 / 91 grados = sin dato
    static bool pos(int32_t lonRaw, int32_t latRaw, double& lat, double& lon) {
        lon = lonRaw / 600000.0; lat = latRaw / 600000.0;
        return fabs(lon) <= 180.0 && fabs(lat) <= 90.0 && !(lonRaw == 0 && latRaw == 0);
    }

    static void sogCog(Report& r, uint32_t sog, uint32_t cog, uint32_t hdg) {
        r.hasSog = sog != 1023; r.sogKn = sog / 10.0f;
        r.hasCog = cog < 3600;  r.cogDeg = cog / 10.0f;
        r.hasHdg = hdg < 360;   r.hdg = (int16_t)(r.hasHdg ? hdg : 511);
    }

    static void dims(Report& r, const uint8_t* d, size_t n, size_t at) {
        r.toBow = (uint16_t)bitsU(d, n, at, 9); r.toStern = (uint16_t)bitsU(d, n, at + 9, 9);
        r.toPort = (uint8_t)bitsU(d, n, at + 18, 6); r.toStar = (uint8_t)bitsU(d, n, at + 24, 6);
        r.hasDim = (r.toBow + r.toStern) > 0;
    }

    bool decode(const uint8_t* d, size_t n, Report& r) {
        memset(&r, 0, sizeof(r));
        r.rot = -128; r.hdg = 511; r.status = 15; r.cls = CL_UNKNOWN;
        if (n < 38) return false;
        r.type = (uint8_t)bitsU(d, n, 0, 6);
        r.mmsi = bitsU(d, n, 8, 30);
        switch (r.type) {
            case 1: case 2: case 3: {
                if (n < 168) return false;
                r.cls = CL_A;
                r.status = (uint8_t)bitsU(d, n, 38, 4); r.hasStatus = true;
                const int32_t rot = bitsS(d, n, 42, 8);
                r.rot = (int8_t)rot;
                sogCog(r, bitsU(d, n, 50, 10), bitsU(d, n, 116, 12), bitsU(d, n, 128, 9));
                r.hasPos = pos(bitsS(d, n, 61, 28), bitsS(d, n, 89, 27), r.lat, r.lon);
                break;
            }
            case 4: {
                if (n < 168) return false;
                r.cls = CL_BASE;
                r.hasPos = pos(bitsS(d, n, 79, 28), bitsS(d, n, 107, 27), r.lat, r.lon);
                break;
            }
            case 5: {
                if (n < 420) return false;
                r.cls = CL_A;
                r.imo = bitsU(d, n, 40, 30); r.hasImo = r.imo != 0;
                bitsText(d, n, 70, 7, r.call, sizeof(r.call)); r.hasCall = r.call[0] != 0;
                bitsText(d, n, 112, 20, r.name, sizeof(r.name)); r.hasName = r.name[0] != 0;
                r.shipType = (uint8_t)bitsU(d, n, 232, 8); r.hasShip = true;
                dims(r, d, n, 240);
                r.etaMonth = (uint8_t)bitsU(d, n, 274, 4); r.etaDay = (uint8_t)bitsU(d, n, 278, 5);
                r.etaHour = (uint8_t)bitsU(d, n, 283, 5); r.etaMinute = (uint8_t)bitsU(d, n, 288, 6);
                r.hasEta = r.etaMonth != 0 && r.etaDay != 0;
                r.draughtM = bitsU(d, n, 294, 8) / 10.0f; r.hasDraught = r.draughtM > 0;
                bitsText(d, n, 302, 20, r.dest, sizeof(r.dest)); r.hasDest = r.dest[0] != 0;
                break;
            }
            case 9: {
                if (n < 168) return false;
                r.cls = CL_SAR;
                const uint32_t alt = bitsU(d, n, 38, 12);
                r.hasAlt = alt != 4095; r.altM = (int16_t)alt;
                const uint32_t sog = bitsU(d, n, 50, 10);
                r.hasSog = sog != 1023; r.sogKn = (float)sog;
                const uint32_t cog = bitsU(d, n, 116, 12);
                r.hasCog = cog < 3600; r.cogDeg = cog / 10.0f;
                r.hasPos = pos(bitsS(d, n, 61, 28), bitsS(d, n, 89, 27), r.lat, r.lon);
                break;
            }
            case 14: {
                if (n < 46) return false;
                r.cls = isDistress(r.mmsi) ? CL_SART : CL_UNKNOWN;
                bitsText(d, n, 40, (n - 40) / 6, r.text, sizeof(r.text)); r.hasText = true;
                break;
            }
            case 18: case 19: {
                if (n < 168 || (r.type == 19 && n < 306)) return false;
                r.cls = CL_B;
                sogCog(r, bitsU(d, n, 46, 10), bitsU(d, n, 112, 12), bitsU(d, n, 124, 9));
                r.hasPos = pos(bitsS(d, n, 57, 28), bitsS(d, n, 85, 27), r.lat, r.lon);
                if (r.type == 19) {
                    bitsText(d, n, 143, 20, r.name, sizeof(r.name)); r.hasName = r.name[0] != 0;
                    r.shipType = (uint8_t)bitsU(d, n, 263, 8); r.hasShip = true;
                    dims(r, d, n, 271);
                }
                break;
            }
            case 21: {
                if (n < 268) return false;
                r.cls = CL_ATON;
                r.aidType = (uint8_t)bitsU(d, n, 38, 5);
                bitsText(d, n, 43, 20, r.name, sizeof(r.name));
                if (n >= 278) {                                   // extension del nombre (hasta 14 caracteres mas)
                    char ext[16];
                    bitsText(d, n, 272, (n - 272) / 6 > 14 ? 14 : (n - 272) / 6, ext, sizeof(ext));
                    if (ext[0] && strlen(r.name) + strlen(ext) < sizeof(r.name)) strcat(r.name, ext);
                }
                r.hasName = r.name[0] != 0;
                r.hasPos = pos(bitsS(d, n, 164, 28), bitsS(d, n, 192, 27), r.lat, r.lon);
                dims(r, d, n, 219);
                break;
            }
            case 24: {
                if (n < 160) return false;
                r.cls = CL_B;
                r.part = (uint8_t)bitsU(d, n, 38, 2);
                if (r.part == 0) { bitsText(d, n, 40, 20, r.name, sizeof(r.name)); r.hasName = r.name[0] != 0; }
                else if (r.part == 1 && n >= 162) {
                    r.shipType = (uint8_t)bitsU(d, n, 40, 8); r.hasShip = true;
                    bitsText(d, n, 90, 7, r.call, sizeof(r.call)); r.hasCall = r.call[0] != 0;
                    if (r.mmsi / 10000000u != 98) dims(r, d, n, 132);   // 98xxxxxxx: embarcacion auxiliar (lleva el MMSI de la nodriza)
                } else return false;
                break;
            }
            default:
                return false;
        }
        if (isDistress(r.mmsi)) r.cls = CL_SART;
        return true;
    }

    const char* shipTypeName(uint8_t t) {
        if (t == 0) return "";
        if (t >= 20 && t <= 29) return "WIG";
        if (t == 30) return "Fishing";
        if (t == 31 || t == 32) return "Towing";
        if (t == 33) return "Dredging";
        if (t == 34) return "Diving";
        if (t == 35) return "Military";
        if (t == 36) return "Sailing";
        if (t == 37) return "Pleasure";
        if (t >= 40 && t <= 49) return "High speed";
        if (t == 50) return "Pilot";
        if (t == 51) return "SAR";
        if (t == 52) return "Tug";
        if (t == 53) return "Port tender";
        if (t == 54) return "Anti-pollution";
        if (t == 55) return "Law enforcement";
        if (t == 58) return "Medical";
        if (t >= 60 && t <= 69) return "Passenger";
        if (t >= 70 && t <= 79) return "Cargo";
        if (t >= 80 && t <= 89) return "Tanker";
        return "Other";
    }

    const char* statusName(uint8_t s) {
        static const char* const N[16] = { "Under way", "At anchor", "Not under command", "Restricted", "Constrained", "Moored", "Aground",
                                           "Fishing", "Sailing", "", "", "Towing", "Pushing", "", "AIS-SART", "" };
        return s < 16 ? N[s] : "";
    }

    // ------------------------------------------------------------------------------------------------ NMEA

    uint8_t nmeaChecksum(const char* s, size_t n) {
        uint8_t x = 0;
        for (size_t i = 0; i < n; i++) x ^= (uint8_t)s[i];
        return x;
    }

    static char armor(uint32_t v) { return (char)(v < 40 ? v + 48 : v + 56); }
    static int  unarmor(char c) {
        int v = (unsigned char)c - 48;
        if (v > 40) v -= 8;
        return (v >= 0 && v < 64) ? v : -1;
    }

    int toNmea(const uint8_t* d, size_t nbits, char channel, int seqId, char* out, size_t outMax, bool own) {
        const size_t nchars = (nbits + 5) / 6;
        const int fill = (int)(nchars * 6 - nbits);
        const size_t PER = 60;                                  // caracteres de datos por frase (como los receptores comerciales)
        const int total = (int)((nchars + PER - 1) / PER);
        if (total < 1 || total > 9) return 0;
        size_t o = 0;
        for (int k = 0; k < total; k++) {
            char line[100];
            char sq[2] = { (char)('0' + (seqId < 0 ? 0 : seqId) % 10), 0 };
            int l = snprintf(line, sizeof(line), "!%s,%d,%d,%s,%c,", own ? "AIVDO" : "AIVDM", total, k + 1, total > 1 ? sq : "", channel);
            const size_t from = k * PER, to = (from + PER < nchars) ? from + PER : nchars;
            for (size_t c = from; c < to && l + 1 < (int)sizeof(line); c++) line[l++] = armor(bitsU(d, nbits, c * 6, 6));
            l += snprintf(line + l, sizeof(line) - l, ",%d", k == total - 1 ? fill : 0);
            const uint8_t cs = nmeaChecksum(line + 1, (size_t)l - 1);
            l += snprintf(line + l, sizeof(line) - l, "*%02X\r\n", cs);
            if (o + (size_t)l + 1 > outMax) return 0;
            memcpy(out + o, line, (size_t)l);
            o += (size_t)l;
        }
        out[o] = 0;
        return total;
    }

    bool NmeaReader::add(const char* p, size_t len, int fill) {
        for (size_t i = 0; i < len; i++) {
            const int v = unarmor(p[i]);
            if (v < 0) return false;
            for (int b = 5; b >= 0; b--) {
                if (accBits >= sizeof(acc) * 8) return false;
                const size_t byte = accBits >> 3;
                if ((accBits & 7) == 0) acc[byte] = 0;
                if ((v >> b) & 1) acc[byte] |= (uint8_t)(0x80 >> (accBits & 7));
                accBits++;
            }
        }
        if (fill > 0 && accBits >= (size_t)fill) accBits -= (size_t)fill;
        return true;
    }

    bool NmeaReader::feed(const char* line) {
        while (*line == ' ' || *line == '\r' || *line == '\n') line++;
        if (line[0] != '!' && line[0] != '$') return false;
        if (strncmp(line + 3, "VDM", 3) != 0 && strncmp(line + 3, "VDO", 3) != 0) return false;   // !AIVDM, !BSVDM, !ABVDM...
        const char* star = strchr(line, '*');
        if (!star || !star[1] || !star[2]) { bad++; return false; }
        const uint8_t cs = (uint8_t)strtol(star + 1, nullptr, 16);
        if (nmeaChecksum(line + 1, (size_t)(star - line - 1)) != cs) { bad++; return false; }
        // campos: 0 cabecera, 1 total, 2 numero, 3 secuencia, 4 canal, 5 datos, 6 relleno
        const char* f[8] = { nullptr }; size_t fl[8] = { 0 };
        int nf = 0;
        const char* p = line;
        f[0] = p;
        for (; p < star; p++) {
            if (*p == ',') { if (nf + 1 < 8) { f[++nf] = p + 1; } continue; }
            if (nf < 8) fl[nf]++;
        }
        if (nf < 6) { bad++; return false; }
        const int total = atoi(f[1]), num = atoi(f[2]);
        const int fill = atoi(f[6]);
        if (total < 1 || total > 9 || num < 1 || num > total) { bad++; return false; }
        if (num == 1) { accBits = 0; expectTotal = total; expectNext = 1; seq = fl[3] ? f[3][0] : 0; }
        if (num != expectNext || total != expectTotal || (fl[3] ? f[3][0] : 0) != seq) { accBits = 0; expectTotal = 0; return false; }
        if (!add(f[5], fl[5], num == total ? fill : 0)) { bad++; accBits = 0; expectTotal = 0; return false; }
        channel = fl[4] ? (f[4][0] == '1' ? 'A' : (f[4][0] == '2' ? 'B' : f[4][0])) : 'A';
        if (num < total) { expectNext++; return false; }
        memcpy(data, acc, (accBits + 7) / 8);
        nbits = accBits;
        accBits = 0; expectTotal = 0;
        return true;
    }

    // ------------------------------------------------------------------------------------------------ demodulador

    bool Demod::begin(float sampleRate, float hpfHz) {
        if (sampleRate < 38400 || sampleRate > 96000) return false;
        fs = sampleRate;
        // paso bajo (GMSK BT 0,4 ya viene limitado): corte en 0,6 x baudios, 1,5 simbolos, ventana de coseno
        int len = (int)lrintf(1.5f * fs / BAUD) | 1;
        if (len > MAX_TAPS - 1) len = MAX_TAPS - 1;
        lpLen = len;
        const float fc = 0.6f * BAUD, mid = (len - 1) * 0.5f;
        float sum = 0;
        for (int i = 0; i < len; i++) {
            const float k = i - mid;
            const float h = (fabsf(k) < 1e-6f) ? 2.0f * fc / fs : sinf(2.0f * PI_F * fc * k / fs) / (PI_F * k);
            lp[i] = h * sinf(PI_F * (i + 0.5f) / len);
            sum += lp[i];
        }
        for (int i = 0; i < len; i++) lp[i] /= sum;
        pllStep = (uint32_t)(((uint64_t)BAUD << 32) / (uint32_t)fs);
        hpfW = hpfHz > 0 ? 2.0f * PI_F * hpfHz / fs : 0.0f;
        agcSlow = 0.0001f * 48000.0f / fs;
        reset();
        return true;
    }

    void Demod::reset() {
        memset(hist, 0, sizeof(hist)); hpos = 0;
        integ = 0; dcAvg = 0; peak = 0; valley = 0; prevDemod = 0;
        pll = 0; prevData = 0; prevBit = 0; lockedBits = 0; shift = 0;
        inFrame = false; ones = 0; nbuf = 0; cur = 0; curBits = 0;
    }

    void Demod::endFrame(FrameCallback cb, void* ctx) {
        // octetos completos: datos + FCS (2); en cur quedan los 7 primeros bits de la bandera de cierre
        if (curBits == 7 && nbuf >= 4) {
            const size_t n = nbuf - 2;
            const uint16_t fcs = (uint16_t)(buf[n] | (buf[n + 1] << 8));
            if (crcX25(buf, n) == fcs) { frames++; if (cb) cb(buf, n, ctx); }
            else crcErrors++;
        }
        nbuf = 0; cur = 0; curBits = 0;
    }

    // bit ya sin NRZI: banderas 0x7E, relleno tras cinco unos, octetos con el primer bit como el de menor peso
    void Demod::bit(uint8_t b, FrameCallback cb, void* ctx) {
        shift = (shift << 1) | b;
        if ((shift & 0xFF) == 0x7E) {                          // bandera
            if (inFrame && nbuf > 0) endFrame(cb, ctx);
            inFrame = true; ones = 0; nbuf = 0; cur = 0; curBits = 0;
            return;
        }
        if ((shift & 0x7F) == 0x7F) { inFrame = false; ones = 0; return; }    // siete unos: abortar
        if (!inFrame) return;
        if (b) ones++;
        else {
            if (ones == 5) { ones = 0; return; }               // cero de relleno
            ones = 0;
        }
        cur = (uint8_t)((cur >> 1) | (b ? 0x80 : 0));
        if (++curBits == 8) {
            if (nbuf < sizeof(buf)) buf[nbuf++] = cur;
            else inFrame = false;
            cur = 0; curBits = 0;
        }
    }

    void Demod::feed(float x, FrameCallback cb, void* ctx) {
        if (hpfW > 0) { integ = integ * leak + x; x += hpfW * integ; }     // inversa del acoplo de alterna de la entrada
        dcAvg += (x - dcAvg) * 0.0005f;
        x -= dcAvg;
        hpos = (hpos == 0) ? lpLen - 1 : hpos - 1;
        hist[hpos] = x; hist[hpos + lpLen] = x;
        float y = 0;
        for (int i = 0; i < lpLen; i++) y += lp[i] * hist[hpos + 1 + i];
        peak   = (y >= peak)   ? y * agcFast + peak * (1 - agcFast)   : y * agcSlow + peak * (1 - agcSlow);
        valley = (y <= valley) ? y * agcFast + valley * (1 - agcFast) : y * agcSlow + valley * (1 - agcSlow);
        const float demod = (peak > valley) ? (y - 0.5f * (peak + valley)) / (peak - valley) : 0.0f;
        const uint8_t data = demod > 0 ? 1 : 0;

        const int32_t prev = pll;
        pll = (int32_t)((uint32_t)pll + pllStep);
        if (pll < 0 && prev > 0) {
            const float after = (float)((uint32_t)pll - 0x80000000u) / (float)pllStep;
            const float atCross = demod - (demod - prevDemod) * after;
            const uint8_t lvl = atCross > 0 ? 1 : 0;
            const uint8_t b = (lvl == prevBit) ? 1 : 0;        // NRZI: sin cambio = 1, cambio = 0
            prevBit = lvl;
            bit(b, cb, ctx);
            // portadora de datos: banderas o entrenamiento recientes
            if ((shift & 0xFF) == 0x7E || (shift & 0xFFFF) == 0x5555 || (shift & 0xFFFF) == 0xAAAA) lockedBits = 300;
            else if (lockedBits > 0) lockedBits--;
        }
        if (data != prevData) {
            float t0 = (prevDemod != demod) ? prevDemod / (prevDemod - demod) : 1.0f;
            if (t0 < 0) t0 = 0;
            if (t0 > 1) t0 = 1;
            const double back = (1.0 - t0) * (double)pllStep;
            double nudged = ((double)pll - back) * (lockedBits > 0 ? 0.85 : 0.65) + back;
            if (nudged > 2147483647.0) nudged -= 4294967296.0;
            if (nudged < -2147483648.0) nudged += 4294967296.0;
            pll = (int32_t)nudged;
            prevData = data;
        }
        prevDemod = demod;
    }

    size_t frameBits(const uint8_t* msg, size_t nbytes, uint8_t* out, size_t maxBits) {
        uint8_t raw[MAX_MSG_BYTES * 10 + 128];
        size_t n = 0;
        auto put = [&](uint8_t b) { if (n < sizeof(raw)) raw[n++] = b; };
        for (int i = 0; i < 24; i++) put((uint8_t)(i & 1));      // entrenamiento 0101...
        for (int i = 0; i < 8; i++) put((uint8_t)((0x7E >> i) & 1));
        uint8_t fr[MAX_MSG_BYTES + 2];
        if (nbytes > MAX_MSG_BYTES) return 0;
        memcpy(fr, msg, nbytes);
        const uint16_t fcs = crcX25(msg, nbytes);
        fr[nbytes] = (uint8_t)(fcs & 0xFF); fr[nbytes + 1] = (uint8_t)(fcs >> 8);
        int ones = 0;
        for (size_t i = 0; i < nbytes + 2; i++) {
            for (int k = 0; k < 8; k++) {                         // el de menor peso primero
                const uint8_t b = (fr[i] >> k) & 1;
                put(b);
                if (b) { if (++ones == 5) { put(0); ones = 0; } } else ones = 0;
            }
        }
        for (int i = 0; i < 8; i++) put((uint8_t)((0x7E >> i) & 1));
        for (int i = 0; i < 8; i++) put(0);                         // cola
        // NRZI: 0 = cambio de nivel, 1 = sin cambio
        uint8_t lvl = 0;
        size_t o = 0;
        for (size_t i = 0; i < n && o < maxBits; i++) {
            if (raw[i] == 0) lvl ^= 1;
            out[o++] = lvl;
        }
        return o;
    }

    // ------------------------------------------------------------------------------------------------ barcos

    void rangeBearing(const Own& o, double lat, double lon, double& nm, double& brg) {
        const double D2R = 3.14159265358979323846 / 180.0;
        const double p1 = o.lat * D2R, p2 = lat * D2R, dl = (lon - o.lon) * D2R;
        const double a = sin((p2 - p1) / 2) * sin((p2 - p1) / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
        nm = 2.0 * atan2(sqrt(a), sqrt(1 - a)) * 3440.065;
        brg = fmod(atan2(sin(dl) * cos(p2), cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl)) / D2R + 360.0, 360.0);
    }

    bool cpa(const Own& o, const Vessel& v, double& cpaNm, double& tcpaMin) {
        if (!o.valid || !v.hasPos) return false;
        const double D2R = 3.14159265358979323846 / 180.0;
        // plano local (millas nauticas): x este, y norte
        const double rx = (v.lon - o.lon) * 60.0 * cos(o.lat * D2R), ry = (v.lat - o.lat) * 60.0;
        const double vs = v.hasSog && v.hasCog ? v.sogKn : 0.0, vc = v.hasCog ? v.cogDeg * D2R : 0.0;
        const double os = o.sogKn, oc = o.cogDeg * D2R;
        const double vx = (vs * sin(vc) - os * sin(oc)) / 60.0, vy = (vs * cos(vc) - os * cos(oc)) / 60.0;     // millas por minuto
        const double vv = vx * vx + vy * vy;
        if (vv < 1e-9) { tcpaMin = 0; cpaNm = sqrt(rx * rx + ry * ry); return true; }
        tcpaMin = -(rx * vx + ry * vy) / vv;
        const double t = tcpaMin > 0 ? tcpaMin : 0;
        const double cx = rx + vx * t, cy = ry + vy * t;
        cpaNm = sqrt(cx * cx + cy * cy);
        return true;
    }

    Vessel* Table::find(uint32_t mmsi) {
        for (int i = 0; i < n; i++) if (v[i].mmsi == mmsi) return &v[i];
        return nullptr;
    }

    Vessel* Table::update(const Report& r, uint32_t nowMs, char channel) {
        if (r.mmsi == 0) return nullptr;
        Vessel* x = find(r.mmsi);
        if (!x) {
            if (n < CAP) x = &v[n++];
            else { int old = 0; for (int i = 1; i < n; i++) if ((int32_t)(v[i].lastMs - v[old].lastMs) < 0) old = i; x = &v[old]; }
            memset(x, 0, sizeof(*x));
            x->mmsi = r.mmsi; x->hdg = 511; x->status = 15; x->cls = r.cls;
        }
        if (r.cls != CL_UNKNOWN && (x->cls == CL_UNKNOWN || r.cls == CL_SART)) x->cls = r.cls;
        if (r.cls == CL_A || r.cls == CL_B || r.cls == CL_BASE || r.cls == CL_ATON || r.cls == CL_SAR) { if (x->cls != CL_SART) x->cls = r.cls; }
        x->lastMs = nowMs; x->msgs++; x->channel = channel;
        if (r.hasPos)    { x->hasPos = true; x->lat = r.lat; x->lon = r.lon; x->posMs = nowMs; }
        if (r.hasSog)    { x->hasSog = true; x->sogKn = r.sogKn; }
        if (r.hasCog)    { x->hasCog = true; x->cogDeg = r.cogDeg; }
        if (r.type == 1 || r.type == 2 || r.type == 3 || r.type == 18 || r.type == 19) x->hdg = r.hdg;
        if (r.hasStatus) x->status = r.status;
        if (r.hasName)   snprintf(x->name, sizeof(x->name), "%s", r.name);
        if (r.hasCall)   snprintf(x->call, sizeof(x->call), "%s", r.call);
        if (r.hasDest)   snprintf(x->dest, sizeof(x->dest), "%s", r.dest);
        if (r.hasShip)   x->shipType = r.shipType;
        if (r.hasDim)    { x->length = (uint16_t)(r.toBow + r.toStern); x->beam = (uint8_t)((r.toPort + r.toStar) > 255 ? 255 : r.toPort + r.toStar); }
        if (r.hasDraught) x->draughtM = r.draughtM;
        if (r.hasEta)    { x->etaMonth = r.etaMonth; x->etaDay = r.etaDay; x->etaHour = r.etaHour; x->etaMinute = r.etaMinute; }
        if (r.hasImo)    x->imo = r.imo;
        if (r.hasAlt)    x->altM = r.altM;
        if (r.hasText)   snprintf(x->lastText, sizeof(x->lastText), "%.39s", r.text);
        return x;
    }

    void Table::purge(uint32_t nowMs, uint32_t maxAgeMs) {
        int k = 0;
        for (int i = 0; i < n; i++) if (nowMs - v[i].lastMs <= maxAgeMs) { if (k != i) v[k] = v[i]; k++; }
        n = k;
    }

    int Table::sorted(const Own& o, int* idx, int max) {
        double dist[CAP];
        int m = 0;
        for (int i = 0; i < n && m < max; i++) {
            double nm = 1e9, b = 0;
            if (v[i].hasPos && o.valid) rangeBearing(o, v[i].lat, v[i].lon, nm, b);
            else if (v[i].hasPos) nm = 1e8;
            dist[m] = nm; idx[m] = i; m++;
        }
        for (int a = 1; a < m; a++) {                              // insercion (pocos barcos)
            const int ii = idx[a]; const double dd = dist[a];
            int b = a - 1;
            while (b >= 0 && dist[b] > dd) { idx[b + 1] = idx[b]; dist[b + 1] = dist[b]; b--; }
            idx[b + 1] = ii; dist[b + 1] = dd;
        }
        return m;
    }
}
