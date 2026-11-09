/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Modo FM de la T-TWR, parte sin hardware. Ver include/fm_core.h
 * Umbrales de deteccion CTCSS del AT1846S: tabla de OpenRTX (AT1846S.h, GPL-3.0), sacada del firmware de TYT MD-UV380.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fm_core.h"

namespace FmCore {

    const uint16_t CTCSS[CTCSS_COUNT] = {
        670, 693, 719, 744, 770, 797, 825, 854, 885, 915, 948, 974, 1000, 1035, 1072, 1109, 1148, 1188, 1230, 1273,
        1318, 1365, 1413, 1462, 1514, 1567, 1598, 1622, 1655, 1679, 1713, 1738, 1773, 1799, 1835, 1862, 1899, 1928, 1966, 1995,
        2035, 2065, 2107, 2181, 2257, 2291, 2336, 2418, 2503, 2541 };

    // los 38 del AT+DMOSETGROUP de NiceRF (codigos 0001..0038)
    static const uint16_t NICERF[38] = {
        670, 719, 744, 770, 797, 825, 854, 885, 915, 948, 974, 1000, 1035, 1072, 1109, 1148, 1188, 1230, 1273, 1318,
        1365, 1413, 1462, 1514, 1567, 1622, 1679, 1738, 1799, 1862, 1928, 2035, 2107, 2181, 2257, 2336, 2418, 2503 };

    const uint16_t DCS[DCS_COUNT] = {
        23, 25, 26, 31, 32, 36, 43, 47, 51, 53, 54, 65, 71, 72, 73, 74, 114, 115, 116, 122, 125, 131, 132, 134, 143, 145,
        152, 155, 156, 162, 165, 172, 174, 205, 212, 223, 225, 226, 243, 244, 245, 246, 251, 252, 255, 261, 263, 265, 266, 271,
        274, 306, 311, 315, 325, 331, 332, 343, 346, 351, 356, 364, 365, 371, 411, 412, 413, 423, 431, 432, 445, 446, 452, 454,
        455, 462, 464, 465, 466, 503, 506, 516, 523, 526, 532, 546, 565, 606, 612, 624, 627, 631, 632, 654, 662, 664, 703, 712,
        723, 731, 732, 734, 743, 754 };

    const long STEPS[STEP_COUNT] = { 5000, 6250, 10000, 12500, 15000, 20000, 25000, 50000, 100000 };

    int ctcssIndex(uint16_t t) { for (int i = 0; i < CTCSS_COUNT; i++) if (CTCSS[i] == t) return i; return -1; }
    int niceRfCtcss(uint16_t t) { for (int i = 0; i < 38; i++) if (NICERF[i] == t) return i + 1; return 0; }
    int dcsIndex(uint16_t c) { for (int i = 0; i < DCS_COUNT; i++) if (DCS[i] == c) return i; return -1; }
    bool validDcs(uint16_t c) { return dcsIndex(c) >= 0; }

    uint16_t ctcssThreshold(uint16_t t) {
        switch (t) {
            case 670:  return 0x0C0D;  case 693: return 0x0C0C;  case 719: return 0x0B0B;
            case 744:  case 770:  return 0x0A0A;
            case 797:  case 825:  return 0x0909;
            case 854:  case 885:  return 0x0808;
            case 915:  case 948:  return 0x0707;
            case 974:  return 0x0706;
            case 1000: case 1034: case 1035: return 0x0606;
            case 1072: case 1109: return 0x0605;
            case 1148: return 0x0505;
            case 1188: case 1230: return 0x0504;
            case 1273: case 1318: return 0x0404;
            case 1365: case 1413: case 1462: return 0x0403;
            case 1514: return 0x0504;
            case 1567: case 1622: case 1679: case 1713: case 1799: return 0x0403;
            case 1862: return 0x0400;
            case 1928: case 2035: case 2107: case 2181: case 2257: return 0x0302;
            case 2336: case 2418: case 2503: return 0x0300;
            default:   return 0x0505;
        }
    }

    uint16_t nextCtcss(uint16_t t, int dir) {
        int i = ctcssIndex(t);
        if (i < 0) { i = 0; while (i < CTCSS_COUNT - 1 && CTCSS[i] < t) i++; return CTCSS[i]; }
        return CTCSS[(i + (dir >= 0 ? 1 : CTCSS_COUNT - 1)) % CTCSS_COUNT];
    }

    uint16_t nextDcs(uint16_t c, int dir) {
        int i = dcsIndex(c);
        if (i < 0) return DCS[0];
        return DCS[(i + (dir >= 0 ? 1 : DCS_COUNT - 1)) % DCS_COUNT];
    }

    const char* toneModeName(uint8_t m) {
        static const char* const N[TONE_MODES] = { "OFF", "TONE", "TSQL", "DCS", "1750" };
        return m < TONE_MODES ? N[m] : "?";
    }

    // ---- Canal ------------------------------------------------------------------------------------------------------

    bool inVhf(long hz) { return hz >= 136000000L && hz <= 174000000L; }

    bool txAllowed(long hz, int region) {
        if (hz >= 144000000L && hz <= ((region == 2 || region == 3) ? 148000000L : 146000000L)) return true;
        if (region == 2) return hz >= 420000000L && hz <= 450000000L;
        return hz >= 430000000L && hz <= 440000000L;
    }
    bool inUhf(long hz) { return hz >= 400000000L && hz <= 480000000L; }

    void clearChannel(Channel& c, long rxHz) {
        memset(&c, 0, sizeof(c));
        c.rxHz = rxHz;
        c.offsetHz = standardOffset(rxHz, 1);
        c.tone = 885;
        c.dcs = 23;
        c.wide = false;
    }

    long txHz(const Channel& c) { return c.rxHz + (long)c.shift * c.offsetHz; }

    bool validChannel(const Channel& c, long lo, long hi) {
        const long tx = txHz(c);
        return c.rxHz >= lo && c.rxHz <= hi && tx >= lo && tx <= hi && c.toneMode < TONE_MODES;
    }

    long standardOffset(long hz, int region) {
        if (hz < 300000000L) return 600000L;
        return region == 1 ? 7600000L : 5000000L;
    }

    bool autoRepeater(long hz, int region, int8_t& shift, long& off) {
        struct Seg { int region; long lo, hi; int8_t shift; long off; };
        static const Seg SEGS[] = {
            { 1, 145593750L, 145793750L, -1,  600000L },      // R1: RV48..RV63 (145,600-145,7875)
            { 1, 438650000L, 439425000L, -1, 7600000L },      // R1: RU368..RU399 (438,650-439,425)
            { 2, 145100000L, 145500000L, -1,  600000L },      // R2 (EE. UU.)
            { 2, 146610000L, 146990000L, -1,  600000L },
            { 2, 147000000L, 147390000L, +1,  600000L },
            { 2, 147600000L, 147990000L, -1,  600000L },
            { 2, 442000000L, 445000000L, +1, 5000000L },
            { 2, 447000000L, 450000000L, -1, 5000000L },
            { 3, 146625000L, 146975000L, -1,  600000L },      // R3 (Australia)
            { 3, 147000000L, 147375000L, +1,  600000L },
            { 3, 438000000L, 438975000L, -1, 7000000L },
            { 3, 439000000L, 440000000L, -1, 5000000L },      // R3 (Japon)
        };
        for (const Seg& s : SEGS) if (s.region == region && hz >= s.lo && hz <= s.hi) { shift = s.shift; off = s.off; return true; }
        return false;
    }

    // ---- VFO --------------------------------------------------------------------------------------------------------

    long snap(long hz, long step) {
        if (step <= 0) return hz;
        return ((hz + step / 2) / step) * step;
    }

    long tune(long hz, int dir, long step, long lo, long hi) {
        if (step <= 0) step = 12500;
        long n = dir > 0 ? (hz / step) * step + step : ((hz + step - 1) / step) * step - step;
        if (n > hi) n = ((lo + step - 1) / step) * step;
        if (n < lo) n = (hi / step) * step;
        return n;
    }

    // ---- QSY --------------------------------------------------------------------------------------------------------

    // "FFF.FF" con 2 o 3 decimales; devuelve los caracteres leidos (0 si no es una frecuencia)
    static int readFreq(const char* p, long& hz) {
        if (!isdigit((unsigned char)p[0]) || !isdigit((unsigned char)p[1]) || !isdigit((unsigned char)p[2]) || p[3] != '.' ||
            !isdigit((unsigned char)p[4]) || !isdigit((unsigned char)p[5])) return 0;
        long mhz = (p[0] - '0') * 100 + (p[1] - '0') * 10 + (p[2] - '0');
        long khz = (p[4] - '0') * 100 + (p[5] - '0') * 10;
        int n = 6;
        if (isdigit((unsigned char)p[6])) { khz += p[6] - '0'; n = 7; }
        hz = mhz * 1000000L + khz * 1000L;
        return n;
    }

    static bool toneFromInt(int v, uint16_t& tone) {
        for (int i = 0; i < CTCSS_COUNT; i++) if (CTCSS[i] / 10 == v) { tone = CTCSS[i]; return true; }
        return false;
    }

    bool parseQsy(const char* name, const char* comment, Qsy& q) {
        memset(&q, 0, sizeof(q));
        const char* p = comment ? comment : "";
        if (name && readFreq(name, q.hz) == 0) q.hz = 0;
        if (q.hz == 0) {                                    // frecuencia en los 10 primeros bytes del comentario
            while (*p == ' ') p++;
            long hz = 0;
            const int n = readFreq(p, hz);
            if (n == 0) return false;
            const char* m = p + n;
            if (n == 6 && *m == ' ') m++;                   // "146.52 MHz"
            if (!(tolower((unsigned char)m[0]) == 'm' && tolower((unsigned char)m[1]) == 'h' && tolower((unsigned char)m[2]) == 'z')) return false;
            q.hz = hz;
            p = m + 3;
        }
        if (q.hz < 28000000L || q.hz > 1300000000L) { q.hz = 0; return false; }
        // campos opcionales separados por espacios: tono, desplazamiento, alcance
        for (int field = 0; field < 4; field++) {
            while (*p == ' ') p++;
            if (!*p) break;
            const char c = *p;
            const char lc = (char)tolower((unsigned char)c);
            const bool lower = c >= 'a' && c <= 'z';
            if ((lc == 't' || lc == 'c' || lc == 'd') && isdigit((unsigned char)p[1]) && isdigit((unsigned char)p[2]) && isdigit((unsigned char)p[3]) &&
                (p[4] == ' ' || p[4] == 0)) {
                const int v = (p[1] - '0') * 100 + (p[2] - '0') * 10 + (p[3] - '0');
                if (lc == 'd') {
                    if (!validDcs((uint16_t)v)) break;
                    q.toneMode = TONE_DCS; q.dcs = (uint16_t)v;
                } else {
                    if (!toneFromInt(v, q.tone)) break;
                    q.toneMode = lc == 't' ? TONE_ENC : TONE_TSQL;
                }
                q.narrow = lower;
                p += 4;
            } else if (lc == 't' && tolower((unsigned char)p[1]) == 'o' && tolower((unsigned char)p[2]) == 'f' && tolower((unsigned char)p[3]) == 'f' &&
                       (p[4] == ' ' || p[4] == 0)) {
                q.toneMode = TONE_OFF; q.hasOffset = true; q.offsetHz = 0; q.narrow = lower;     // sin tono y simplex forzado
                p += 4;
            } else if ((c == '1' || c == 'l') && p[1] == '7' && p[2] == '5' && p[3] == '0' && (p[4] == ' ' || p[4] == 0)) {
                q.toneMode = TONE_1750; q.narrow = c == 'l';
                p += 4;
            } else if ((c == '+' || c == '-') && isdigit((unsigned char)p[1]) && isdigit((unsigned char)p[2]) && isdigit((unsigned char)p[3]) &&
                       (p[4] == ' ' || p[4] == 0)) {
                const long v = ((p[1] - '0') * 100 + (p[2] - '0') * 10 + (p[3] - '0')) * 10000L;
                q.hasOffset = true; q.offsetHz = c == '-' ? -v : v;
                p += 4;
            } else if (lc == 'r' && isdigit((unsigned char)p[1]) && isdigit((unsigned char)p[2]) && (tolower((unsigned char)p[3]) == 'k' || tolower((unsigned char)p[3]) == 'm') &&
                       (p[4] == ' ' || p[4] == 0)) {
                const int v = (p[1] - '0') * 10 + (p[2] - '0');
                q.rangeKm = tolower((unsigned char)p[3]) == 'm' ? (int)(v * 1.609f + 0.5f) : v;
                p += 4;
            } else break;
        }
        return true;
    }

    void qsyToChannel(const Qsy& q, int region, Channel& c) {
        clearChannel(c, q.hz);
        snprintf(c.name, sizeof(c.name), "QSY");
        if (q.hasOffset) {
            c.shift = q.offsetHz > 0 ? 1 : (q.offsetHz < 0 ? -1 : 0);
            c.offsetHz = q.offsetHz == 0 ? standardOffset(q.hz, region) : labs(q.offsetHz);
        } else {
            int8_t sh = 0; long off = 0;
            if (autoRepeater(q.hz, region, sh, off)) { c.shift = sh; c.offsetHz = off; }
            else c.offsetHz = standardOffset(q.hz, region);
        }
        c.toneMode = q.toneMode;
        if (q.toneMode == TONE_ENC || q.toneMode == TONE_TSQL) c.tone = q.tone;
        if (q.toneMode == TONE_DCS) c.dcs = q.dcs;
        c.wide = !q.narrow;
    }

    size_t formatFreq(long hz, char* out, size_t outMax) {
        const int n = snprintf(out, outMax, "%ld.%04ld", hz / 1000000L, (hz % 1000000L) / 100L);
        return (n > 0 && (size_t)n < outMax) ? (size_t)n : 0;
    }

    size_t formatQsy(const Channel& c, int region, char* out, size_t outMax) {
        if (!out || outMax < 24) return 0;
        const long khz = (c.rxHz + 500) / 1000;
        char tone[16];
        switch (c.toneMode) {
            case TONE_ENC:  snprintf(tone, sizeof(tone), "T%03d", c.tone / 10); break;
            case TONE_TSQL: snprintf(tone, sizeof(tone), "C%03d", c.tone / 10); break;
            case TONE_DCS:  snprintf(tone, sizeof(tone), "D%03d", c.dcs); break;
            case TONE_1750: snprintf(tone, sizeof(tone), "1750"); break;
            default:        snprintf(tone, sizeof(tone), "Toff"); break;
        }
        if (!c.wide) tone[0] = tone[0] == '1' ? 'l' : (char)tolower((unsigned char)tone[0]);
        char off[24] = "";
        int8_t sh = 0; long std = 0;
        const bool rptSeg = autoRepeater(c.rxHz, region, sh, std);
        long tens = (c.offsetHz + 5000) / 10000;           // en decenas de kHz, como la especificacion (hasta 9,99 MHz)
        if (tens < 0) tens = 0;
        if (tens > 999) tens = 999;
        if (c.shift != 0) snprintf(off, sizeof(off), " %c%03ld", c.shift > 0 ? '+' : '-', tens);
        else if (rptSeg && c.toneMode != TONE_OFF) snprintf(off, sizeof(off), " -000");          // simplex en un segmento de repetidores
        const int n = snprintf(out, outMax, "%03ld.%03ldMHz %s%s", khz / 1000, khz % 1000, tone, off);
        return (n > 0 && (size_t)n < outMax) ? (size_t)n : 0;
    }

    // ---- Escaneo ----------------------------------------------------------------------------------------------------

    void Scanner::start(uint32_t now, uint8_t resume, uint32_t dwellMs) {
        active = true; holding = false; mode = resume; dwell = dwellMs; tunedAt = now; stopAt = 0; freeAt = 0;
    }

    void Scanner::tuned(uint32_t now) { tunedAt = now; holding = false; freeAt = 0; }

    bool Scanner::tick(uint32_t now, bool busy) {
        if (!active) return false;
        if (!holding) {
            if (now - tunedAt < dwell) return false;
            if (!busy) return true;
            holding = true; stopAt = now; freeAt = 0;
            if (mode == RESUME_STOP) active = false;        // se para en la primera senal
            return false;
        }
        if (busy) freeAt = 0;
        else if (freeAt == 0) freeAt = now ? now : 1;
        const bool freeLong = freeAt != 0 && now - freeAt >= 2000;
        if (mode == RESUME_CARRIER && freeLong) return true;
        if (mode == RESUME_TIME && (freeLong || now - stopAt >= 5000)) return true;
        return false;
    }

    // ---- ARTS -------------------------------------------------------------------------------------------------------

    void Arts::start(uint32_t now, int intervalS, bool cwId) {
        active = true; id = cwId; interval = intervalS == 15 ? 15000 : 25000;
        state = ARTS_UNKNOWN; everHeard = false; lastPoll = 0; startAt = now; lastId = now; lastHeard = 0;
    }

    bool Arts::wantPoll(uint32_t now, bool busy) { return active && !busy && (lastPoll == 0 || now - lastPoll >= interval); }
    void Arts::polled(uint32_t now) { lastPoll = now ? now : 1; }
    void Arts::heard(uint32_t now) { lastHeard = now ? now : 1; everHeard = true; }
    bool Arts::wantCwId(uint32_t now) { return active && id && now - lastId >= 600000UL; }

    bool Arts::update(uint32_t now) {
        if (!active) return false;
        const uint32_t timeout = 4 * interval;             // cuatro sondeos sin oir al otro (1 minuto con 15 s)
        uint8_t next = state;
        if (everHeard && now - lastHeard <= timeout) next = ARTS_IN;
        else if (everHeard || now - startAt > timeout) next = ARTS_OUT;
        if (next == state) return false;
        state = next;
        return true;
    }

    // ---- EAI --------------------------------------------------------------------------------------------------------

    void Eai::start(uint32_t now, uint8_t m, int minutes) {
        if (minutes < 1) minutes = 1;
        if (minutes > 50) minutes = 50;
        active = true; mode = m == EAI_CONTINUOUS ? EAI_CONTINUOUS : EAI_INTERVAL; micOn = false;
        startAt = now; endAt = now + (uint32_t)minutes * 60000UL; nextBeep = now; nextId = now; segAt = now; segEnd = 0;
    }

    uint32_t Eai::remainingS(uint32_t now) const { return active && (int32_t)(endAt - now) > 0 ? (endAt - now) / 1000 : 0; }

    EaiAction Eai::tick(uint32_t now) {
        if (!active) return EA_NONE;
        if ((int32_t)(now - endAt) >= 0) {
            if (micOn) { micOn = false; return EA_MIC_OFF; }
            active = false;
            return EA_END;
        }
        if ((int32_t)(now - nextId) >= 0) {
            if (micOn) { micOn = false; segAt = now; return EA_MIC_OFF; }
            nextId = now + 600000UL;
            nextBeep = now + 2500;
            return EA_CWID;
        }
        if (mode == EAI_INTERVAL) {
            if ((int32_t)(now - nextBeep) >= 0) { nextBeep = now + 2500; return EA_BEEP; }
            return EA_NONE;
        }
        if (!micOn && (int32_t)(now - segAt) >= 0) { micOn = true; segEnd = now + 50000UL; return EA_MIC_ON; }
        if (micOn && (int32_t)(now - segEnd) >= 0) { micOn = false; segAt = now + 10000UL; return EA_MIC_OFF; }
        return EA_NONE;
    }

    uint8_t ManDown::tick(uint32_t now) {
        if (limitMs == 0 || fired) return 0;
        const uint32_t idle = now - lastAct;
        if (idle >= limitMs) { fired = true; return 2; }
        if (!warned && idle + WARN_MS >= limitMs) { warned = true; return 1; }
        return 0;
    }
}
