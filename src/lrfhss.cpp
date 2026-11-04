/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of LoRa APRS Tracker.
 *
 * LoRa APRS Tracker is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LoRa APRS Tracker is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LoRa APRS Tracker. If not, see <https://www.gnu.org/licenses/>.
 */

#include "lrfhss.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

namespace LRFHSS {

    static const float BW_KHZ[BW_COUNT] = { 39.06f, 85.94f, 136.72f, 183.59f, 335.94f, 386.72f, 722.66f, 773.44f, 1523.4f, 1574.2f };
    float bwKHz(uint8_t i) { return BW_KHZ[i < BW_COUNT ? i : 0]; }
    const char* crName(uint8_t i) { static const char* const n[CR_COUNT] = { "5/6", "2/3", "1/2", "1/3" }; return n[i < CR_COUNT ? i : 3]; }

    uint32_t airtimeMs(size_t len, uint8_t crIdx) {
        // Estimacion: cabeceras de 233,5 ms repetidas 3 veces (CR 1/3) o 2 veces (resto); fragmentos de 48 bits a 488,28 baudios (~102,4 ms)
        static const float rate[CR_COUNT] = { 5.0f / 6.0f, 2.0f / 3.0f, 0.5f, 1.0f / 3.0f };
        const uint8_t hdr = (crIdx == 3) ? 3 : 2;
        const float codedBits = ((float)len * 8.0f + 16.0f + 6.0f) / rate[crIdx < CR_COUNT ? crIdx : 3];
        const uint32_t frags = (uint32_t)ceilf(codedBits / 48.0f);
        return (uint32_t)(hdr * 233.5f + frags * 102.4f);
    }

    int freqCheck(uint32_t freqHz, uint8_t bwIdx) {
        const float bw = bwKHz(bwIdx);
        const int64_t half = (int64_t)((bw / 2.0f + 2.0f) * 1000.0f);        // semiancho ocupado con margen (Hz)
        const int64_t lo = (int64_t)freqHz - half, hi = (int64_t)freqHz + half;
        if (lo < 430000000LL || hi > 440000000LL) return -1;
        if (hi > 432000000LL && lo < 432500000LL) return -2;
        if (hi > 435000000LL && lo < 438000000LL) return -3;
        return bw > 125.0f ? 1 : 0;
    }

    const char* freqCheckText(int code) {
        switch (code) {
            case 0:  return "Freq OK";
            case 1:  return "BW > 125 kHz";
            case -1: return "Out of 430-440 MHz";
            case -2: return "Overlaps 432.0-432.5";
            case -3: return "Overlaps 435-438";
            default: return "Freq invalid";
        }
    }

    bool callValid(const char* call) {
        if (!call) return false;
        size_t n = strlen(call);
        if (n < 3 || n > 10) return false;
        bool digit = false;
        for (size_t i = 0; i < n; i++) {
            char ch = call[i];
            if (ch >= '0' && ch <= '9') digit = true;
            else if (!((ch >= 'A' && ch <= 'Z') || ch == '/')) return false;
        }
        return digit;
    }

    bool gridValid(const char* g) {
        if (!g) return false;
        const size_t n = strlen(g);
        if (n != 4 && n != 6 && n != 8) return false;
        if (!(g[0] >= 'A' && g[0] <= 'R' && g[1] >= 'A' && g[1] <= 'R' && g[2] >= '0' && g[2] <= '9' && g[3] >= '0' && g[3] <= '9')) return false;
        if (n >= 6 && !(g[4] >= 'A' && g[4] <= 'X' && g[5] >= 'A' && g[5] <= 'X')) return false;
        if (n == 8 && !(g[6] >= '0' && g[6] <= '9' && g[7] >= '0' && g[7] <= '9')) return false;
        return true;
    }

    size_t beaconEncode(const char* call, const char* grid, int dbm, char* out, size_t cap) {
        if (!callValid(call) || !gridValid(grid) || dbm < -9 || dbm > 30) return 0;
        char tmp[40];
        int n = snprintf(tmp, sizeof(tmp), "PB1 %s %s %d", call, grid, dbm);
        if (n <= 0 || (size_t)n >= sizeof(tmp) || (size_t)n + 1 > cap || (size_t)n > MAX_PAYLOAD) return 0;
        memcpy(out, tmp, (size_t)n + 1);
        return (size_t)n;
    }

    bool beaconParse(const char* msg, char* call, size_t callCap, char* grid, size_t gridCap, int* dbm) {
        if (!msg || strncmp(msg, "PB1 ", 4) != 0 || callCap < 11 || gridCap < 9) return false;
        char c[16], g[16];
        int p = 0;
        if (sscanf(msg + 4, "%15s %15s %d", c, g, &p) != 3) return false;
        if (!callValid(c) || !gridValid(g)) return false;
        strcpy(call, c); strcpy(grid, g);
        if (dbm) *dbm = p;
        return true;
    }

    uint32_t msUntilBeacon(uint32_t msOfDay, uint16_t intervalMin, uint8_t offset) {
        if (intervalMin < 2) intervalMin = 2;
        if (intervalMin & 1) intervalMin++;
        const uint32_t slotsPerCycle = intervalMin / 2;
        const uint32_t pairMs = 120000UL;
        const uint32_t DAY = 86400000UL;
        const uint32_t pairIndex = msOfDay / pairMs;
        for (uint32_t k = 0; k <= slotsPerCycle + 1; k++) {
            const uint32_t start = (pairIndex + k) * pairMs + BEACON_PHASE_MS;
            if (start + 800UL < msOfDay) continue;                      // ranura ya pasada
            const uint32_t idx = (((start - BEACON_PHASE_MS) % DAY) / pairMs) % slotsPerCycle;   // el indice se reinicia a las 00:00 UTC
            if (idx == offset % slotsPerCycle) return start > msOfDay ? start - msOfDay : 0;
        }
        return pairMs;
    }

    uint8_t slotOffset(const char* call, uint16_t intervalMin) {
        uint32_t h = 11;
        for (const char* p = call; p && *p; p++) h = h * 37 + (uint8_t)*p;
        const uint32_t slots = (intervalMin < 2 ? 1 : intervalMin / 2);
        return (uint8_t)(h % slots);
    }
}
