/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Decodificador RDS (IEC 62106) para la radio FM comercial: grupos que dan los receptores I2C (RDA5807M, Si4703, Si4735) como
 * cuatro bloques de 16 bits. PI, PTY, nombre de la emisora (PS, grupos 0A / 0B: cada trozo se acepta al oirlo dos veces igual),
 * RadioText (2A / 2B, se borra con el indicador A / B) y hora (CT, grupo 4A: fecha juliana modificada, hora UTC y desfase local).
 * Sin hardware: se prueba en el PC con tools/bcast/test_rds.cpp.
 */

#ifndef RDS_H_
#define RDS_H_

#include <stdint.h>
#include <string.h>

class RdsDecoder {
public:
    void reset() {
        memset(ps, ' ', 8); ps[8] = 0; memset(psCand, 0, sizeof(psCand)); psMask = 0;
        memset(rt, 0, sizeof(rt)); rtAb = -1; rtMask = 0;
        pi = 0; pty = 0; groups = 0; ctValid = false;
    }

    // Un grupo con sus cuatro bloques; errores = bloques que el receptor no pudo corregir (bit 0 = A ... bit 3 = D)
    void feed(uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint8_t errors = 0) {
        if (errors & 0x2) return;                           // sin el bloque B no se sabe que grupo es
        groups++;
        if (!(errors & 0x1)) pi = a;
        const uint8_t type = (uint8_t)(b >> 12), version = (uint8_t)((b >> 11) & 1);
        pty = (uint8_t)((b >> 5) & 0x1F);
        if (type == 0 && !(errors & 0x8)) {                 // nombre de la emisora: 2 caracteres por grupo
            const int seg = b & 0x3;
            const char c1 = clean((char)(d >> 8)), c2 = clean((char)(d & 0xFF));
            if (psCand[seg * 2] == c1 && psCand[seg * 2 + 1] == c2) { ps[seg * 2] = c1; ps[seg * 2 + 1] = c2; psMask |= (uint8_t)(1 << seg); }
            psCand[seg * 2] = c1; psCand[seg * 2 + 1] = c2;
        } else if (type == 2) {                             // RadioText: 4 (A) o 2 (B) caracteres por grupo
            const int ab = (b >> 4) & 1;
            if (ab != rtAb) { memset(rt, 0, sizeof(rt)); rtMask = 0; rtAb = ab; }
            const int seg = b & 0xF;
            if (version == 0) {
                if (!(errors & 0x4)) { put(seg * 4, (char)(c >> 8)); put(seg * 4 + 1, (char)(c & 0xFF)); }
                if (!(errors & 0x8)) { put(seg * 4 + 2, (char)(d >> 8)); put(seg * 4 + 3, (char)(d & 0xFF)); }
            } else if (!(errors & 0x8)) { put(seg * 2, (char)(d >> 8)); put(seg * 2 + 1, (char)(d & 0xFF)); }
            rtMask |= (uint16_t)(1u << seg);
        } else if (type == 4 && version == 0 && !(errors & 0xC)) {     // hora
            const uint32_t mjd = ((uint32_t)(b & 0x3) << 15) | (c >> 1);
            const int hour = ((c & 1) << 4) | (d >> 12), minute = (d >> 6) & 0x3F;
            const int offset = (d & 0x1F) * ((d & 0x20) ? -1 : 1);
            if (hour < 24 && minute < 60 && mjd > 40000) {
                mjdToDate(mjd, ctYear, ctMonth, ctDay);
                ctHour = hour; ctMinute = minute; ctOffsetHalfHours = offset; ctValid = true;
            }
        }
    }

    const char* name() const { return ps; }                 // 8 caracteres (espacios si no se sabe)
    bool  nameComplete() const { return psMask == 0x0F; }
    // RadioText sin espacios al final (hasta el retorno de carro)
    void  text(char* out, size_t n) const {
        size_t k = 0;
        for (int i = 0; i < 64 && k + 1 < n; i++) { const char ch = rt[i]; if (ch == '\r') break; out[k++] = ch ? ch : ' '; }
        while (k > 0 && out[k - 1] == ' ') k--;
        out[k] = 0;
    }
    uint16_t piCode() const { return pi; }
    uint8_t  programType() const { return pty; }
    uint32_t groupCount() const { return groups; }
    bool  clock(int& y, int& mo, int& d, int& h, int& mi, int& offsetHalfHours) const {
        if (!ctValid) return false;
        y = ctYear; mo = ctMonth; d = ctDay; h = ctHour; mi = ctMinute; offsetHalfHours = ctOffsetHalfHours;
        return true;
    }

    // Fecha juliana modificada -> fecha (anexo G de la norma)
    static void mjdToDate(uint32_t mjd, int& y, int& m, int& d) {
        const int yp = (int)((mjd - 15078.2) / 365.25);
        const int mp = (int)((mjd - 14956.1 - (int)(yp * 365.25)) / 30.6001);
        d = (int)(mjd - 14956 - (int)(yp * 365.25) - (int)(mp * 30.6001));
        const int k = (mp == 14 || mp == 15) ? 1 : 0;
        y = 1900 + yp + k;
        m = mp - 1 - k * 12;
    }

private:
    static char clean(char c) { return (c >= 0x20 && c < 0x7F) ? c : ' '; }
    void put(int i, char c) { if (i >= 0 && i < 64) rt[i] = (c == '\r') ? '\r' : clean(c); }
    char     ps[9], psCand[8];
    uint8_t  psMask = 0;
    char     rt[64];
    int      rtAb = -1;
    uint16_t rtMask = 0;
    uint16_t pi = 0;
    uint8_t  pty = 0;
    uint32_t groups = 0;
    bool     ctValid = false;
    int      ctYear = 0, ctMonth = 0, ctDay = 0, ctHour = 0, ctMinute = 0, ctOffsetHalfHours = 0;
};

#endif
