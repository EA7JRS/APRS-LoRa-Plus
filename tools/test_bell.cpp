// Prueba en PC de include/bell_rings.h: g++ -std=c++17 -Wall -Wextra -Werror -Iinclude tools/test_bell.cpp -o /tmp/tb && /tmp/tb
// Copyright (c) 2026 Manuel - EA7JRS
#include "bell_rings.h"
#include <cstdio>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FALLO linea %d: %s\n", __LINE__, #c); fails++; } } while (0)

// Reproduce una melodia con pasos de 1 ms y cuenta las notas sonadas (flancos 0 -> frecuencia) y su duracion total
static void run(Bell::Id id, int& notes, uint32_t& total) {
    Bell::Player p; notes = 0; total = 0;
    CHECK(p.start(id, 1000));
    uint16_t hz, last = 0; uint32_t t = 1000;
    while (p.tick(t, hz)) { if (hz && !last) notes++; last = hz; t++; if (t > 10000) { CHECK(false); break; } }
    total = t - 1000;
    CHECK(!p.active());
}
int main() {
    int n; uint32_t total;
    // numero de notas segun la tabla del manual
    run(Bell::BELL_BEACON_RX, n, total);  CHECK(n == 2);        // ding dong
    run(Bell::BELL_MSG_TO_ME, n, total);  CHECK(n == 4);        // ding ding ding dong
    run(Bell::BELL_BULLETIN, n, total);   CHECK(n == 6);        // ding x5 dong
    run(Bell::BELL_MSG_OTHER, n, total);  CHECK(n == 1);
    run(Bell::BELL_ACK_RX, n, total);     CHECK(n == 1);
    run(Bell::BELL_REJ_RX, n, total);     CHECK(n == 1);
    run(Bell::BELL_TX, n, total);         CHECK(n == 1);
    run(Bell::BELL_BEACON_DUP, n, total); CHECK(n == 1);
    run(Bell::BELL_MSG_DUP, n, total);    CHECK(n == 2);
    // duracion: suma de notas + silencios entre ellas
    run(Bell::BELL_BEACON_RX, n, total);  CHECK(total >= 140 + 20 + 300 && total <= 140 + 20 + 300 + 25);
    // el ding es mas agudo que el dong y el reproductor entrega la frecuencia de cada nota
    { Bell::Player p; uint16_t hz; p.start(Bell::BELL_BEACON_RX, 0); CHECK(p.tick(10, hz) && hz == Bell::N_DING);
      CHECK(p.tick(150, hz) && hz == 0);                         // silencio entre notas
      CHECK(p.tick(200, hz) && hz == Bell::N_DONG); CHECK(Bell::N_DING > Bell::N_DONG); CHECK(!p.tick(600, hz)); }
    // prioridad: una melodia mas importante no se pisa con otra menor, pero si al reves
    { Bell::Player p; p.start(Bell::BELL_MSG_TO_ME, 0); CHECK(!p.start(Bell::BELL_BEACON_RX, 5)); CHECK(!p.start(Bell::BELL_MSG_TO_ME, 5));
      CHECK(p.start(Bell::BELL_BULLETIN, 5)); }
    { Bell::Player p; p.start(Bell::BELL_TX, 0); CHECK(p.start(Bell::BELL_ACK_RX, 5)); }
    // reloj con salto grande: no se queda colgado ni repite notas
    { Bell::Player p; uint16_t hz; p.start(Bell::BELL_BULLETIN, 100); CHECK(!p.tick(100 + 5000, hz)); CHECK(hz == 0); }
    // desbordamiento de millis()
    { Bell::Player p; uint16_t hz; uint32_t t0 = 0xFFFFFFF0u; p.start(Bell::BELL_BEACON_RX, t0); CHECK(p.tick(t0 + 10, hz) && hz == Bell::N_DING); }
    // todas las melodias tienen notas validas
    for (int i = 0; i < Bell::BELL_COUNT; i++) { CHECK(Bell::BELL_TABLE[i].count > 0); for (int k = 0; k < Bell::BELL_TABLE[i].count; k++) { CHECK(Bell::BELL_TABLE[i].notes[k].hz >= 400 && Bell::BELL_TABLE[i].notes[k].hz <= 2500); CHECK(Bell::BELL_TABLE[i].notes[k].ms > 0); } }
    printf(fails ? "fails=%d\n" : "fails=0\n", fails);
    return fails != 0;
}
