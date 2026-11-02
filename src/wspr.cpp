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

#include "wspr.h"
#include <string.h>

namespace WSPR {

    static const uint8_t SYNC[SYMBOLS] = {
        1,1,0,0,0,0,0,0,1,0,0,0,1,1,1,0,0,0,1,0,0,1,0,1,1,1,1,0,0,0,0,0,0,0,1,0,0,1,0,1,0,0,0,0,0,0,1,0,1,1,0,0,1,1,0,1,0,0,0,1,1,0,1,0,0,0,0,1,1,0,1,0,1,0,1,0,1,0,0,1,0,0,1,0,1,1,0,0,0,1,1,0,1,0,1,0,0,0,1,0,0,0,0,0,1,0,0,1,0,0,1,1,1,0,1,1,0,0,1,1,0,1,0,0,0,1,1,1,0,0,0,0,0,1,0,1,0,0,1,1,0,0,0,0,0,0,0,1,1,0,1,0,1,1,0,0,0,1,1,0,0,0
    };

    bool validPower(int dbm) {
        if (dbm < 0 || dbm > 60) return false;
        int r = dbm % 10;
        return r == 0 || r == 3 || r == 7;
    }

    int nearestValidPower(int dbm) {
        int best = 0, bestDist = 1000;
        for (int p = 0; p <= 60; p++) {
            if (!validPower(p)) continue;
            int d = p > dbm ? p - dbm : dbm - p;
            if (d < bestDist) { bestDist = d; best = p; }
        }
        return best;
    }

    static int charCode(char c) {                 // 0-9 -> 0-9, A-Z -> 10-35, espacio -> 36
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
        if (c == ' ') return 36;
        return -1;
    }

    // Normaliza el indicativo a 6 caracteres con el 3.er digito. Devuelve false si no es tipo 1
    static bool normalizeCall(const char* in, char out[7]) {
        char tmp[16];
        size_t n = strlen(in);
        if (n < 3 || n > 6) return false;
        for (size_t i = 0; i < n; i++) {
            char c = in[i];
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            tmp[i] = c;
        }
        tmp[n] = '\0';
        char buf[8];
        if (tmp[1] >= '0' && tmp[1] <= '9' && !(tmp[2] >= '0' && tmp[2] <= '9')) {     // un solo caracter de prefijo: se anade un espacio delante
            if (n > 5) return false;
            buf[0] = ' ';
            memcpy(buf + 1, tmp, n + 1);
        } else {
            memcpy(buf, tmp, n + 1);
        }
        size_t len = strlen(buf);
        while (len < 6) buf[len++] = ' ';
        buf[6] = '\0';
        if (!(buf[2] >= '0' && buf[2] <= '9')) return false;
        if (buf[1] == ' ') return false;
        for (int i = 0; i < 6; i++) {
            char c = buf[i];
            bool digit = c >= '0' && c <= '9', letter = c >= 'A' && c <= 'Z';
            if (i < 2) { if (!(digit || letter || (i == 0 && c == ' '))) return false; }
            else if (i == 2) { if (!digit) return false; }
            else { if (!(letter || c == ' ')) return false; }
        }
        memcpy(out, buf, 7);
        return true;
    }

    bool callValid(const char* call) { char b[7]; return call != nullptr && normalizeCall(call, b); }

    bool gridValid(const char* g) {
        if (g == nullptr || strlen(g) != 4) return false;
        return g[0] >= 'A' && g[0] <= 'R' && g[1] >= 'A' && g[1] <= 'R' && g[2] >= '0' && g[2] <= '9' && g[3] >= '0' && g[3] <= '9';
    }

    static uint8_t parity32(uint32_t v) {
        v ^= v >> 16; v ^= v >> 8; v ^= v >> 4; v ^= v >> 2; v ^= v >> 1;
        return (uint8_t)(v & 1);
    }

    bool encode(const char* call, const char* grid, int dbm, uint8_t symbols[SYMBOLS]) {
        char c[7];
        if (!normalizeCall(call, c)) return false;
        if (!gridValid(grid) || !validPower(dbm)) return false;

        // indicativo: 28 bits
        uint32_t n = (uint32_t)charCode(c[0]);
        n = n * 36 + (uint32_t)charCode(c[1]);
        n = n * 10 + (uint32_t)charCode(c[2]);
        for (int i = 3; i < 6; i++) n = n * 27 + (c[i] == ' ' ? 26u : (uint32_t)(c[i] - 'A'));
        // locator y potencia: 22 bits
        uint32_t m = (uint32_t)((179 - 10 * (grid[0] - 'A') - (grid[2] - '0')) * 180 + 10 * (grid[1] - 'A') + (grid[3] - '0'));
        m = m * 128 + (uint32_t)dbm + 64;
        uint64_t packed = ((uint64_t)n << 22) | m;          // 50 bits

        // 50 bits + 31 de cola a cero -> codigo convolucional K=32, r=1/2 -> 162 bits
        uint8_t coded[SYMBOLS];
        uint32_t reg = 0;
        int out = 0;
        for (int i = 0; i < 81; i++) {
            uint32_t bit = i < 50 ? (uint32_t)((packed >> (49 - i)) & 1) : 0;
            reg = (reg << 1) | bit;
            coded[out++] = parity32(reg & 0xF2D05351u);
            coded[out++] = parity32(reg & 0xE4613C47u);
        }

        // entrelazado: orden por inversion de bits de 8 bits
        uint8_t inter[SYMBOLS];
        int k = 0;
        for (int i = 0; i < 256 && k < SYMBOLS; i++) {
            uint8_t r = 0, v = (uint8_t)i;
            for (int b = 0; b < 8; b++) { r = (uint8_t)((r << 1) | (v & 1)); v >>= 1; }
            if (r < SYMBOLS) inter[r] = coded[k++];
        }
        for (int i = 0; i < SYMBOLS; i++) symbols[i] = (uint8_t)(SYNC[i] + 2 * inter[i]);
        return true;
    }

    uint32_t symbolStartUs(uint8_t i) { return (uint32_t)(((uint64_t)i * 2048000ULL) / 3ULL); }    // i * 8192/12000 s

    uint32_t msUntilSlot(uint32_t msOfDay, uint16_t intervalMin, uint8_t offset) {
        if (intervalMin < 2) intervalMin = 2;
        if (intervalMin & 1) intervalMin++;
        const uint32_t slotsPerCycle = intervalMin / 2;
        const uint32_t pairMs = 120000UL;
        const uint32_t DAY = 86400000UL;
        for (uint32_t k = 0; k <= slotsPerCycle + 1; k++) {
            // candidatos: inicios de ranura (minuto par + 1 s) a partir de ahora
            uint32_t pairIndex = msOfDay / pairMs;
            uint32_t start = (pairIndex + k) * pairMs + START_DELAY_MS;
            if (start < msOfDay) continue;
            uint32_t idx = (((start - START_DELAY_MS) % DAY) / pairMs) % slotsPerCycle;     // el indice se reinicia a las 00:00 UTC
            // 720 ranuras por dia: con intervalos que no dividen 720 el ultimo ciclo del dia queda mas corto
            if (idx == offset % slotsPerCycle) return start - msOfDay;
        }
        return pairMs;
    }

    uint8_t slotOffset(const char* call, uint16_t intervalMin) {
        uint32_t h = 7;
        for (const char* p = call; p && *p; p++) h = h * 31 + (uint8_t)*p;
        uint32_t slots = (intervalMin < 2 ? 1 : intervalMin / 2);
        return (uint8_t)(h % slots);
    }

    uint8_t powerIndex(int dbm) {
        static const int8_t steps[POWER_STEPS] = { 0, 3, 7, 10, 13, 17, 20 };
        uint8_t best = 0;
        int bestDist = 1000;
        for (uint8_t i = 0; i < POWER_STEPS; i++) {
            int d = dbm > steps[i] ? dbm - steps[i] : steps[i] - dbm;
            if (d < bestDist) { bestDist = d; best = i; }
        }
        return best;
    }

    void Tones::begin(double centerHz, double corrHz, bool fine, double driftHzPerMin) {
        base_ = centerHz + corrHz - 1.5 * TONE_SPACING;
        fine_ = fine;
        driftHzPerMin_ = driftHzPerMin;
    }

    double Tones::hz(uint8_t tone, uint8_t index) const {
        const double symSec = 8192.0 / 12000.0;                          // 0,68267 s por simbolo
        const double t = ((double)index + 0.5 - (double)SYMBOLS / 2.0) * symSec;   // segundos respecto a la mitad de la emision
        return base_ + (double)tone * TONE_SPACING + driftHzPerMin_ * (t / 60.0);
    }

    uint32_t Tones::exact(uint8_t tone, uint8_t index) const { return pllWord(hz(tone, index)); }

    TonePlan Tones::plan(uint8_t tone, uint8_t index, uint32_t symbolUs) const {
        const double w = hz(tone, index) / PLL_STEP_HZ;
        const uint32_t lo = (uint32_t)w;
        const double frac = w - (double)lo;
        if (!fine_ || frac < 0.03 || frac > 0.97) {                      // sin segundo tramo: palabra mas cercana
            const uint32_t one = (uint32_t)(w + 0.5);
            return { one, one, 0 };
        }
        // lo durante (1-frac) del simbolo y lo+1 durante frac: media = w
        return { lo, lo + 1, (uint32_t)((1.0 - frac) * (double)symbolUs + 0.5) };
    }
}
