/*
 * Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Plotter (lo copian APRS LoRa+ Display y el rotor).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "gunzip.h"

#include <cstdlib>
#include <cstring>

namespace gz {
namespace {

constexpr size_t WIN = 32768, OUTB = 4096;

struct Huff {
    uint16_t count[16];
    uint16_t symbol[288];
};

struct St {
    const uint8_t* in;
    size_t n, pos = 0;
    uint32_t bitbuf = 0;
    int bitcnt = 0;
    int err = 0;
    uint8_t* win;
    size_t wpos = 0, total = 0;
    uint8_t* ob;
    size_t on = 0;
    uint32_t crc = 0xFFFFFFFFu;
    const Sink* out;

    uint32_t bits(int need) {
        while (bitcnt < need) {
            if (pos >= n) { err = -2; return 0; }
            bitbuf |= (uint32_t)in[pos++] << bitcnt;
            bitcnt += 8;
        }
        const uint32_t v = bitbuf & ((1u << need) - 1);
        bitbuf >>= need;
        bitcnt -= need;
        return v;
    }
    void flush() {
        if (!on) return;
        for (size_t i = 0; i < on; i++) {
            crc ^= ob[i];
            for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
        }
        (*out)(ob, on);
        on = 0;
    }
    void put(uint8_t b) {
        win[wpos] = b;
        wpos = (wpos + 1) & (WIN - 1);
        total++;
        ob[on++] = b;
        if (on == OUTB) flush();
    }
};

// Tabla canonica de Huffman a partir de las longitudes (como puff.c de zlib). Devuelve < 0 si las longitudes sobran.
int build(Huff& h, const uint8_t* len, int n) {
    for (int i = 0; i < 16; i++) h.count[i] = 0;
    for (int s = 0; s < n; s++) h.count[len[s]]++;
    if (h.count[0] == n) return 0;                       // ningun codigo (bloque sin distancias): vale
    int left = 1;
    for (int l = 1; l < 16; l++) {
        left <<= 1;
        left -= h.count[l];
        if (left < 0) return -1;
    }
    uint16_t offs[16];
    offs[1] = 0;
    for (int l = 1; l < 15; l++) offs[l + 1] = offs[l] + h.count[l];
    for (int s = 0; s < n; s++) if (len[s]) h.symbol[offs[len[s]]++] = (uint16_t)s;
    return left;                                         // > 0: incompleto (solo se admite con un unico codigo)
}

int decode(St& s, const Huff& h) {
    int code = 0, first = 0, index = 0;
    for (int l = 1; l < 16; l++) {
        code |= (int)s.bits(1);
        if (s.err) return -1;
        const int count = h.count[l];
        if (code - count < first) return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    s.err = -3;
    return -1;
}

const uint16_t LBASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t LEXT[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t DBASE[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
                            4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t DEXT[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void codes(St& s, const Huff& lc, const Huff& dc) {
    for (;;) {
        int sym = decode(s, lc);
        if (s.err) return;
        if (sym < 256) { s.put((uint8_t)sym); continue; }
        if (sym == 256) return;
        sym -= 257;
        if (sym >= 29) { s.err = -3; return; }
        const int len = LBASE[sym] + (int)s.bits(LEXT[sym]);
        int ds = decode(s, dc);
        if (s.err) return;
        if (ds >= 30) { s.err = -3; return; }
        const size_t dist = DBASE[ds] + s.bits(DEXT[ds]);
        if (s.err) return;
        if (dist > s.total || dist > WIN) { s.err = -3; return; }
        for (int i = 0; i < len; i++) s.put(s.win[(s.wpos - dist) & (WIN - 1)]);
    }
}

void stored(St& s) {
    s.bitbuf = 0;
    s.bitcnt = 0;
    if (s.pos + 4 > s.n) { s.err = -2; return; }
    const unsigned len = s.in[s.pos] | (s.in[s.pos + 1] << 8), nlen = s.in[s.pos + 2] | (s.in[s.pos + 3] << 8);
    s.pos += 4;
    if (len != (~nlen & 0xFFFF)) { s.err = -3; return; }
    if (s.pos + len > s.n) { s.err = -2; return; }
    for (unsigned i = 0; i < len; i++) s.put(s.in[s.pos++]);
}

void fixedBlock(St& s) {
    static Huff lc, dc;
    static bool ready = false;
    if (!ready) {
        uint8_t l[288];
        int i = 0;
        for (; i < 144; i++) l[i] = 8;
        for (; i < 256; i++) l[i] = 9;
        for (; i < 280; i++) l[i] = 7;
        for (; i < 288; i++) l[i] = 8;
        build(lc, l, 288);
        for (i = 0; i < 30; i++) l[i] = 5;
        build(dc, l, 30);
        ready = true;
    }
    codes(s, lc, dc);
}

void dynamicBlock(St& s) {
    static const uint8_t ORDER[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    const int nlen = (int)s.bits(5) + 257, ndist = (int)s.bits(5) + 1, ncode = (int)s.bits(4) + 4;
    if (s.err) return;
    if (nlen > 286 || ndist > 30) { s.err = -3; return; }
    uint8_t l[320] = {};
    for (int i = 0; i < ncode; i++) l[ORDER[i]] = (uint8_t)s.bits(3);
    if (s.err) return;
    Huff lc, dc;
    if (build(lc, l, 19) != 0) { s.err = -3; return; }
    int i = 0;
    while (i < nlen + ndist) {
        int sym = decode(s, lc);
        if (s.err) return;
        if (sym < 16) { l[i++] = (uint8_t)sym; continue; }
        int len = 0, rep;
        if (sym == 16) {
            if (i == 0) { s.err = -3; return; }
            len = l[i - 1];
            rep = 3 + (int)s.bits(2);
        } else if (sym == 17) rep = 3 + (int)s.bits(3);
        else rep = 11 + (int)s.bits(7);
        if (s.err) return;
        if (i + rep > nlen + ndist) { s.err = -3; return; }
        while (rep--) l[i++] = (uint8_t)len;
    }
    if (l[256] == 0) { s.err = -3; return; }
    int e = build(lc, l, nlen);
    if (e < 0 || (e > 0 && nlen - lc.count[0] != 1)) { s.err = -3; return; }
    e = build(dc, l + nlen, ndist);
    if (e < 0 || (e > 0 && ndist - dc.count[0] != 1)) { s.err = -3; return; }
    codes(s, lc, dc);
}

} // namespace

const char* errorText(int code) {
    switch (code) {
        case 0: return "ok";
        case -1: return "not a gzip file";
        case -2: return "file cut short";
        case -3: return "invalid compressed data";
        case -4: return "CRC error";
        case -5: return "out of memory";
        default: return "error";
    }
}

int gunzip(const uint8_t* in, size_t n, const Sink& out) {
    if (n < 2 || in[0] != 0x1F || in[1] != 0x8B) return -1;
    if (n < 18) return -2;
    St s;
    s.in = in;
    s.n = n;
    s.out = &out;
    s.win = (uint8_t*)malloc(WIN);
    s.ob = (uint8_t*)malloc(OUTB);
    if (!s.win || !s.ob) { free(s.win); free(s.ob); return -5; }
    int rc = 0;
    while (rc == 0 && s.pos + 18 <= n && in[s.pos] == 0x1F && in[s.pos + 1] == 0x8B) {   // un miembro tras otro
        if (in[s.pos + 2] != 8) { rc = -3; break; }
        const uint8_t flg = in[s.pos + 3];
        s.pos += 10;
        if (flg & 4) {                                   // FEXTRA
            if (s.pos + 2 > n) { rc = -2; break; }
            s.pos += 2 + (in[s.pos] | (in[s.pos + 1] << 8));
        }
        if (flg & 8) { while (s.pos < n && in[s.pos]) s.pos++; s.pos++; }     // nombre
        if (flg & 16) { while (s.pos < n && in[s.pos]) s.pos++; s.pos++; }    // comentario
        if (flg & 2) s.pos += 2;                         // CRC de la cabecera
        if (s.pos > n) { rc = -2; break; }
        s.bitbuf = 0; s.bitcnt = 0; s.crc = 0xFFFFFFFFu; s.total = 0; s.wpos = 0;
        size_t startTotal = 0;
        int last;
        do {
            last = (int)s.bits(1);
            const int type = (int)s.bits(2);
            if (s.err) break;
            if (type == 0) stored(s);
            else if (type == 1) fixedBlock(s);
            else if (type == 2) dynamicBlock(s);
            else s.err = -3;
        } while (!last && !s.err);
        if (s.err) { rc = s.err; break; }
        s.flush();
        s.bitbuf = 0; s.bitcnt = 0;                      // el resto del ultimo byte no cuenta
        if (s.pos + 8 > n) { rc = -2; break; }
        const uint32_t crc = (uint32_t)in[s.pos] | ((uint32_t)in[s.pos + 1] << 8) | ((uint32_t)in[s.pos + 2] << 16) | ((uint32_t)in[s.pos + 3] << 24);
        const uint32_t isize = (uint32_t)in[s.pos + 4] | ((uint32_t)in[s.pos + 5] << 8) | ((uint32_t)in[s.pos + 6] << 16) | ((uint32_t)in[s.pos + 7] << 24);
        s.pos += 8;
        if ((crc ^ 0xFFFFFFFFu) != s.crc || isize != (uint32_t)(s.total - startTotal)) rc = -4;
    }
    free(s.win);
    free(s.ob);
    return rc;
}

} // namespace gz
