// Prueba en PC de las utilidades de la baliza LR-FHSS (Manuel - EA7JRS). Compilar: g++ -std=c++17 -Iinclude tools/test_lrfhss.cpp src/lrfhss.cpp -o t && ./t
#include "lrfhss.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FALLO linea %d: %s\n", __LINE__, #c); fails++; } } while (0)
int main() {
    using namespace LRFHSS;
    // comprobacion de frecuencia
    CHECK(freqCheck(434000000, 0) == 0);          // 434,000 MHz con 39,06 kHz: permitida
    CHECK(freqCheck(433900000, 0) == 0);
    CHECK(freqCheck(434000000, 2) == 1);          // 136,72 kHz > 125 kHz: aviso
    CHECK(freqCheck(434000000, 9) == 1 || freqCheck(434000000, 9) < 0);   // 1,5 MHz: aviso o bloqueo segun invada otros segmentos
    CHECK(freqCheck(432301500, 0) == -2);         // segmento WSPR / senales debiles
    CHECK(freqCheck(432450000, 0) == -2);         // balizas exclusivas
    CHECK(freqCheck(436500000, 0) == -3);         // satelites
    CHECK(freqCheck(435000000, 0) == -3);
    CHECK(freqCheck(868000000, 0) == -1);
    CHECK(freqCheck(429990000, 0) == -1);
    CHECK(freqCheck(439990000, 0) == -1);         // el borde ocupado sale de la banda
    CHECK(freqCheck(439900000, 0) == 0);
    CHECK(freqCheck(433000000, 4) == 1);          // 335,94 kHz: permitida con aviso (dentro de 430-440 y fuera de lo prohibido)
    CHECK(strcmp(freqCheckText(-2), "Overlaps 432.0-432.5") == 0);
    // mensaje
    char msg[40];
    CHECK(beaconEncode("EA7JRS", "IM67NG", 14, msg, sizeof(msg)) == strlen("PB1 EA7JRS IM67NG 14"));
    CHECK(strcmp(msg, "PB1 EA7JRS IM67NG 14") == 0);
    CHECK(beaconEncode("EA7JRS", "IM67", 14, msg, sizeof(msg)) == 0);       // locator de 4: invalido
    CHECK(beaconEncode("EA7JRS", "IM67NG", 99, msg, sizeof(msg)) == 0);
    CHECK(beaconEncode("ABC", "IM67NG", 14, msg, sizeof(msg)) == 0);        // sin digito
    char c[11], g[7]; int p;
    CHECK(beaconParse("PB1 EA7JRS IM67NG 14", c, sizeof(c), g, sizeof(g), &p) && !strcmp(c, "EA7JRS") && !strcmp(g, "IM67NG") && p == 14);
    CHECK(!beaconParse("XX1 EA7JRS IM67NG 14", c, sizeof(c), g, sizeof(g), &p));
    // duracion: el mensaje tipico dura unos 2 s y cabe en el hueco tras el WSPR
    CHECK(airtimeMs(20, 3) > 1500 && airtimeMs(26, 3) < 3200);
    // ranuras: segundo 114 del minuto par
    CHECK(msUntilBeacon(114000, 2, 0) == 0);
    CHECK(msUntilBeacon(0, 2, 0) == 114000);
    CHECK(msUntilBeacon(114500, 2, 0) == 0);                                // 0,5 s tarde: aun se emite
    CHECK(msUntilBeacon(116000, 2, 0) == 118000);                           // pasada: siguiente en 234 s
    CHECK(msUntilBeacon(0, 10, 2) == 354000);
    CHECK(msUntilBeacon(86399000UL, 2, 0) == 115000);                       // cruce de medianoche
    // todas las ranuras caen a segundo 114 de minuto par y no antes de que acabe el WSPR (111,6 s)
    for (uint32_t t = 0; t < 86400000UL; t += 37000UL) {
        for (int iv : {2, 4, 10, 60}) {
            uint32_t w = msUntilBeacon(t, (uint16_t)iv, slotOffset("EA7JRS", (uint16_t)iv));
            uint32_t at = (t + w) % 86400000UL;
            if (w == 0) CHECK(at % 120000UL >= BEACON_PHASE_MS && at % 120000UL <= BEACON_PHASE_MS + 800UL);   // ranura en curso (hasta 0,8 s tarde)
            else CHECK(at % 120000UL == BEACON_PHASE_MS);
            CHECK(w <= (uint32_t)iv * 60000UL + 120000UL);
        }
    }
    CHECK(slotOffset("EA7JRS", 10) < 5);
    printf("fails=%d\n", fails);
    return fails != 0;
}
