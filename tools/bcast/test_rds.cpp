// Prueba en PC del decodificador RDS (include/rds.h)
// Copyright (C) 2026 Manuel - EA7JRS - GPL-3.0-or-later
//   g++ -std=c++17 -Wall -Wextra -Werror -Iinclude tools/bcast/test_rds.cpp -o /tmp/test_rds && /tmp/test_rds
#include "rds.h"
#include <cstdio>
#include <cstring>
#include <string>

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { printf("FALLO linea %d: %s\n", __LINE__, #c); fails++; } } while (0)

static uint16_t B(int type, int ver, int pty, int low) { return (uint16_t)((type << 12) | (ver << 11) | (pty << 5) | low); }

static void sendPs(RdsDecoder& r, const char* name, int times = 2) {
    for (int t = 0; t < times; t++)
        for (int s = 0; s < 4; s++) r.feed(0xE211, B(0, 0, 10, s), 0, (uint16_t)((name[s * 2] << 8) | name[s * 2 + 1]));
}

static void sendRt(RdsDecoder& r, const std::string& text, int ab) {
    std::string t = text + "\r";
    while (t.size() % 4) t += ' ';
    for (size_t s = 0; s * 4 < t.size(); s++)
        r.feed(0xE211, B(2, 0, 10, (ab << 4) | (int)s), (uint16_t)((t[s * 4] << 8) | t[s * 4 + 1]), (uint16_t)((t[s * 4 + 2] << 8) | t[s * 4 + 3]));
}

int main() {
    RdsDecoder r; r.reset();
    sendPs(r, "RNE R 3 ", 1);
    CHECK(!r.nameComplete());                               // una sola vez: aun no se acepta
    sendPs(r, "RNE R 3 ");
    CHECK(r.nameComplete() && !strcmp(r.name(), "RNE R 3 ") && r.piCode() == 0xE211 && r.programType() == 10);
    // un trozo con error en D no cambia el nombre; uno que solo se oye una vez tampoco
    r.feed(0xE211, B(0, 0, 10, 1), 0, 0x5858, 0x8);
    r.feed(0xE211, B(0, 0, 10, 1), 0, ('Z' << 8) | 'Z');
    CHECK(!strcmp(r.name(), "RNE R 3 "));
    char t[80];
    sendRt(r, "Hoy en Radio 3: musica sin fronteras", 0);
    r.text(t, sizeof(t));
    CHECK(!strcmp(t, "Hoy en Radio 3: musica sin fronteras"));
    sendRt(r, "Otro texto", 1);                             // cambio A/B: se borra el anterior
    r.text(t, sizeof(t));
    CHECK(!strcmp(t, "Otro texto"));
    // 2B: 2 caracteres por grupo en D
    RdsDecoder b; b.reset();
    const char* bt = "HOLA\r ";
    for (int s = 0; s < 3; s++) b.feed(0x1234, B(2, 1, 0, s), 0x1234, (uint16_t)((bt[s * 2] << 8) | bt[s * 2 + 1]));
    b.text(t, sizeof(t)); CHECK(!strcmp(t, "HOLA"));
    // hora: 9-11-2026 13:05 UTC, +1 h (dos medias horas). MJD de 2026-11-09 = 61353
    int y, mo, d, h, mi, off;
    RdsDecoder::mjdToDate(61353, y, mo, d); CHECK(y == 2026 && mo == 11 && d == 9);
    RdsDecoder::mjdToDate(45218, y, mo, d); CHECK(y == 1982 && mo == 9 && d == 6);           // ejemplo de la norma
    const uint32_t mjd = 61353;
    const uint16_t c = (uint16_t)(((mjd & 0x7FFF) << 1) | (13 >> 4));
    const uint16_t dd = (uint16_t)(((13 & 0xF) << 12) | (5 << 6) | 2);
    r.feed(0xE211, B(4, 0, 10, (int)(mjd >> 15) & 3), c, dd);
    CHECK(r.clock(y, mo, d, h, mi, off) && y == 2026 && mo == 11 && d == 9 && h == 13 && mi == 5 && off == 2);
    RdsDecoder e; e.reset(); e.feed(0, 0, 0, 0, 0x2); CHECK(e.groupCount() == 0);
    printf(fails ? "%d FALLOS de %d\n" : "OK (%d comprobaciones)\n", fails ? fails : checks, checks);
    return fails ? 1 : 0;
}
