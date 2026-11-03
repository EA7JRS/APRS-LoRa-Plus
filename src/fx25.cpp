/* Copyright (C) 2026 Manuel - EA7JRS
 * FX.25 forward error correction for AX.25. See fx25.h
 * Part of AIS/LoRa APRS Tracker - Manuel Canca EA7JRS (based on LoRa APRS Tracker by Ricardo Guzman CA2RXU, GPL-3.0)
 *
 * Written from the FX.25 specification; the correlation tags, the block layout and the Reed-Solomon parameters
 * are the ones published in the specification and used by Dire Wolf (WB2OSZ).
 */
#include <string.h>
#include "fx25.h"

namespace FX25 {

    namespace {

        struct TagInfo { uint64_t value; uint8_t dataBytes; uint8_t checkBytes; };

        const TagInfo TAGS[TAG_MAX + 1] = {
            { 0x0000000000000000ULL,   0,  0 },     // unused (tag 0)
            { 0xB74DB7DF8A532F3EULL, 239, 16 },     // 0x01 RS(255,239)
            { 0x26FF60A600CC8FDEULL, 128, 16 },     // 0x02 RS(144,128)
            { 0xC7DC0508F3D9B09EULL,  64, 16 },     // 0x03 RS(80,64)
            { 0x8F056EB4369660EEULL,  32, 16 },     // 0x04 RS(48,32)
            { 0x6E260B1AC5835FAEULL, 223, 32 },     // 0x05 RS(255,223)
            { 0xFF94DC634F1CFF4EULL, 128, 32 },     // 0x06 RS(160,128)
            { 0x1EB7B9CDBC09C00EULL,  64, 32 },     // 0x07 RS(96,64)
            { 0xDBF869BD2DBB1776ULL,  32, 32 },     // 0x08 RS(64,32)
            { 0x3ADB0C13DEAE2836ULL, 191, 64 },     // 0x09 RS(255,191)
            { 0xAB69DB6A543188D6ULL, 128, 64 },     // 0x0A RS(192,128)
            { 0x4A4ABEC4A724B796ULL,  64, 64 },     // 0x0B RS(128,64)
        };

        // GF(2^8), x^8 + x^4 + x^3 + x^2 + 1 (0x11d)
        uint8_t gfExp[512];
        uint8_t gfLog[256];
        bool    gfReady = false;

        void gfInit() {
            if (gfReady) return;
            unsigned x = 1;
            for (int i = 0; i < 255; i++) {
                gfExp[i] = (uint8_t)x;
                gfLog[x] = (uint8_t)i;
                x <<= 1;
                if (x & 0x100) x ^= 0x11d;
            }
            for (int i = 255; i < 512; i++) gfExp[i] = gfExp[i - 255];
            gfLog[0] = 0;
            gfReady = true;
        }

        inline uint8_t gmul(uint8_t a, uint8_t b) { return (a && b) ? gfExp[gfLog[a] + gfLog[b]] : 0; }
        inline uint8_t gdiv(uint8_t a, uint8_t b) { return a ? gfExp[gfLog[a] + 255 - gfLog[b]] : 0; }   // b != 0

        inline int popcount64(uint64_t v) {
            int n = 0;
            while (v) { v &= v - 1; n++; }
            return n;
        }

    }

    uint64_t tagValue(int tag) { return (tag >= TAG_MIN && tag <= TAG_MAX) ? TAGS[tag].value : 0; }
    int      tagData(int tag)  { return (tag >= TAG_MIN && tag <= TAG_MAX) ? TAGS[tag].dataBytes : 0; }
    int      tagCheck(int tag) { return (tag >= TAG_MIN && tag <= TAG_MAX) ? TAGS[tag].checkBytes : 0; }

    int pickTag(int checkBytes, size_t dlen) {
        // the shortest block with that number of check bytes
        int best = 0;
        for (int t = TAG_MIN; t <= TAG_MAX; t++) {
            if (TAGS[t].checkBytes == checkBytes && dlen <= TAGS[t].dataBytes) {
                if (best == 0 || TAGS[t].dataBytes < TAGS[best].dataBytes) best = t;
            }
        }
        return best;
    }

    int matchTag(uint64_t reg) {
        int best = 0, bestErr = TAG_MAX_ERRORS + 1;
        for (int t = TAG_MIN; t <= TAG_MAX; t++) {
            int e = popcount64(reg ^ TAGS[t].value);
            if (e < bestErr) { bestErr = e; best = t; }
        }
        return best;
    }

    size_t stuffFrame(const uint8_t* frame, size_t len, uint8_t* out) {
        // frame = AX.25 frame WITHOUT FCS: the FCS is appended here. Data bits go LSB first
        memset(out, 0, MAX_DATA);
        const size_t maxBits = MAX_DATA * 8;
        size_t nbits = 0;
        bool   over = false;
        auto put = [&](int b) {
            if (nbits >= maxBits) { over = true; return; }
            if (b) out[nbits >> 3] |= (uint8_t)(1 << (nbits & 7));
            nbits++;
        };
        for (int i = 0; i < 8; i++) put((0x7E >> i) & 1);                        // opening flag
        // FCS (CRC-16/X.25) over the frame
        uint16_t crc = 0xFFFF;
        for (size_t i = 0; i < len; i++) {
            crc ^= frame[i];
            for (int b = 0; b < 8; b++) crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0x8408) : (uint16_t)(crc >> 1);
        }
        crc = (uint16_t)~crc;
        int ones = 0;
        for (size_t i = 0; i < len + 2; i++) {
            uint8_t v = (i < len) ? frame[i] : (i == len ? (uint8_t)(crc & 0xFF) : (uint8_t)(crc >> 8));
            for (int b = 0; b < 8; b++) {
                int x = (v >> b) & 1;
                put(x);
                if (x) { if (++ones == 5) { put(0); ones = 0; } } else ones = 0;
            }
        }
        for (int i = 0; i < 8; i++) put((0x7E >> i) & 1);                        // closing flag
        if (over) return 0;
        size_t used = (nbits + 7) >> 3;
        // pad with flag bits (the pattern keeps rotating, as an HDLC receiver sees the idle line)
        uint8_t mask = 1;
        while (nbits < maxBits) {
            if (0x7E & mask) out[nbits >> 3] |= (uint8_t)(1 << (nbits & 7));
            nbits++;
            mask = (uint8_t)((mask << 1) | (mask >> 7));
        }
        return used;
    }

    // ---- Reed-Solomon -----------------------------------------------------------------------------------------------
    // Generic shortened code over GF(256): the first root is alpha^fcr (fcr 0 or 1), nroots <= MAX_CHECK. A block of n bytes is
    // data followed by nroots check bytes; the leading zeros of a shortened code do not change anything and are not stored.

    void rsEncodeG(int fcr, int nroots, const uint8_t* data, size_t k, uint8_t* check) {
        gfInit();
        uint8_t g[MAX_CHECK + 1];                       // generator polynomial: g[m] = coefficient of x^m, g[nroots] = 1
        memset(g, 0, sizeof(g));
        g[0] = 1;
        for (int i = 0; i < nroots; i++) {              // multiply by (x + alpha^(fcr + i))
            uint8_t root = gfExp[fcr + i];
            for (int m = i + 1; m > 0; m--) g[m] = (uint8_t)(g[m - 1] ^ gmul(g[m], root));
            g[0] = gmul(g[0], root);
        }
        uint8_t r[MAX_CHECK];
        memset(r, 0, sizeof(r));
        for (size_t i = 0; i < k; i++) {
            uint8_t fb = (uint8_t)(data[i] ^ r[0]);
            for (int j = 0; j < nroots - 1; j++) r[j] = (uint8_t)(r[j + 1] ^ gmul(fb, g[nroots - 1 - j]));
            r[nroots - 1] = gmul(fb, g[0]);
        }
        memcpy(check, r, (size_t)nroots);
    }

    int rsDecodeG(int fcr, int nroots, uint8_t* block, size_t n, int* positions) {
        gfInit();
        uint8_t s[MAX_CHECK];
        bool    any = false;
        for (int j = 0; j < nroots; j++) {              // S_j = c(alpha^(fcr + j)), c(x) = block[0] x^(n-1) + ... + block[n-1]
            uint8_t x = gfExp[fcr + j], acc = 0;
            for (size_t i = 0; i < n; i++) acc = (uint8_t)(gmul(acc, x) ^ block[i]);
            s[j] = acc;
            if (acc) any = true;
        }
        if (!any) return 0;

        // Berlekamp-Massey
        uint8_t lam[MAX_CHECK + 2], bpoly[MAX_CHECK + 2], tmp[MAX_CHECK + 2];
        memset(lam, 0, sizeof(lam)); memset(bpoly, 0, sizeof(bpoly));
        lam[0] = 1; bpoly[0] = 1;
        int L = 0, m = 1;
        uint8_t bb = 1;
        for (int i = 0; i < nroots; i++) {
            uint8_t d = s[i];
            for (int j = 1; j <= L; j++) d ^= gmul(lam[j], s[i - j]);
            if (d == 0) { m++; continue; }
            uint8_t coef = gdiv(d, bb);
            if (2 * L <= i) {
                memcpy(tmp, lam, sizeof(lam));
                for (int j = 0; j + m <= nroots; j++) lam[j + m] ^= gmul(coef, bpoly[j]);
                L = i + 1 - L;
                memcpy(bpoly, tmp, sizeof(lam));
                bb = d; m = 1;
            } else {
                for (int j = 0; j + m <= nroots; j++) lam[j + m] ^= gmul(coef, bpoly[j]);
                m++;
            }
        }
        if (L == 0 || L > nroots / 2) return -1;

        // Omega(x) = S(x) * Lambda(x) mod x^nroots
        uint8_t omega[MAX_CHECK];
        for (int i = 0; i < nroots; i++) {
            uint8_t v = 0;
            for (int j = 0; j <= i && j <= L; j++) v ^= gmul(lam[j], s[i - j]);
            omega[i] = v;
        }

        // Chien search + Forney. A byte at index i has locator X = alpha^(n - 1 - i); look for Lambda(X^-1) = 0
        int found = 0;
        int pos[MAX_CHECK / 2 + 1];
        uint8_t val[MAX_CHECK / 2 + 1];
        for (size_t i = 0; i < n; i++) {
            int e = (int)(n - 1 - i);
            uint8_t xinv = gfExp[(255 - e % 255) % 255];
            uint8_t sum = 0, xp = 1;
            for (int j = 0; j <= L; j++) { sum ^= gmul(lam[j], xp); xp = gmul(xp, xinv); }
            if (sum) continue;
            if (found >= L) return -1;
            uint8_t num = 0; xp = 1;
            for (int j = 0; j < nroots; j++) { num ^= gmul(omega[j], xp); xp = gmul(xp, xinv); }
            uint8_t den = 0; xp = 1;
            for (int j = 1; j <= L; j += 2) {            // formal derivative: odd terms only
                den ^= gmul(lam[j], xp);
                xp = gmul(xp, gmul(xinv, xinv));
            }
            if (den == 0) return -1;
            uint8_t v = gdiv(num, den);
            if (fcr == 0) v = gmul(v, gfExp[e % 255]);   // X^(1 - fcr)
            pos[found] = (int)i;
            val[found] = v;
            found++;
        }
        if (found != L) return -1;
        for (int f = 0; f < found; f++) block[pos[f]] ^= val[f];
        // verification: every syndrome must now be zero
        for (int j = 0; j < nroots; j++) {
            uint8_t x = gfExp[fcr + j], acc = 0;
            for (size_t i = 0; i < n; i++) acc = (uint8_t)(gmul(acc, x) ^ block[i]);
            if (acc) return -1;
        }
        if (positions) for (int f = 0; f < found; f++) positions[f] = pos[f];
        return found;
    }

    void rsEncode(int nroots, const uint8_t* data, uint8_t* check) {
        rsEncodeG(1, nroots, data, BLOCK_SIZE - (size_t)nroots, check);
    }

    int rsDecode(int nroots, uint8_t* block, size_t fixedZeroFrom) {
        uint8_t saved[BLOCK_SIZE];
        memcpy(saved, block, BLOCK_SIZE);
        int pos[MAX_CHECK / 2 + 1];
        int r = rsDecodeG(1, nroots, block, BLOCK_SIZE, pos);
        if (r > 0) {
            const size_t zeroTo = BLOCK_SIZE - (size_t)nroots;
            for (int f = 0; f < r; f++)
                if ((size_t)pos[f] >= fixedZeroFrom && (size_t)pos[f] < zeroTo) { memcpy(block, saved, BLOCK_SIZE); return -1; }   // a byte that was never sent cannot have been wrong
        }
        return r;
    }

}
