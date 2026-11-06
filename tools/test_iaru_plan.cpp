// Prueba en el PC del modulo de regiones IARU (sin Arduino): g++ -std=c++11 -Iinclude -Isrc tools/test_iaru_plan.cpp src/iaru_plan.cpp -o /tmp/test_iaru
// Copyright (C) 2026 Manuel - EA7JRS   (GPL-3.0-or-later)
//   ./test_iaru           -> pruebas
//   ./test_iaru --tables  -> vuelca las tablas de canales en JSON (tools/check_iaru_tables.py las compara con script.js)
#include "iaru_plan.h"
#include <cstdio>
#include <cstring>
using namespace IARU_Plan;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FALLO linea %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main(int argc, char** argv) {
    if (argc > 1 && !strcmp(argv[1], "--tables")) {
        const char* names[] = {"lora", "fsk", "cw", "cwb"};
        printf("{");
        for (int k = 0; k < 4; k++) for (int r = 1; r <= 3; r++) {
            printf("%s\"%s%d\":[", (k || r > 1) ? "," : "", names[k], r);
            for (int i = 0; i < channelCount((Kind)k, r); i++) printf("%s%ld", i ? "," : "", channelHz((Kind)k, r, i));
            printf("]");
        }
        printf("}\n");
        return 0;
    }
    // regiones por posicion
    CHECK(regionAt(40.4f, -3.7f) == 1);       // Madrid
    CHECK(regionAt(40.7f, -74.0f) == 2);      // Nueva York
    CHECK(regionAt(35.7f, 139.7f) == 3);      // Tokio
    CHECK(regionAt(21.3f, -157.9f) == 2);     // Honolulu
    CHECK(regionAt(52.9f, 173.2f) == 2);      // Attu (longitud este, region 2)
    CHECK(regionAt(64.1f, -21.9f) == 1);      // Reikiavik
    CHECK(regionAt(35.7f, 51.4f) == 3);       // Teheran
    CHECK(regionAt(33.3f, 44.4f) == 1);       // Bagdad
    CHECK(regionAt(-33.9f, 151.2f) == 3);     // Sidney
    CHECK(regionAt(40.4f, -3.7f + 360.0f) == 1);   // longitud fuera de rango se normaliza
    // bandas
    CHECK(allowed(1, 433775000L) && allowed(3, 439100000L) && !allowed(1, 446000000L));
    CHECK(allowed(2, 446000000L) && allowed(2, 915000000L) && !allowed(2, 600000000L));
    // equivalencias de frecuencia
    CHECK(mapFrequency(LORA, 1, 2, 433775000L) == 433775000L);        // canal comun
    CHECK(mapFrequency(LORA, 1, 2, 433625000L) == 433775000L);        // canal sin equivalente -> por defecto
    CHECK(mapFrequency(LORA, 3, 1, 439100000L) == 433775000L);
    CHECK(mapFrequency(LORA, 1, 2, 446000000L) == 446000000L);        // Custom dentro de la banda de R2 (R1 no la permitia): se respeta
    CHECK(mapFrequency(LORA, 2, 1, 446000000L) == 433775000L);        // Custom fuera de la banda de R1 -> por defecto
    CHECK(mapFrequency(LORA, 2, 1, 915000000L) == 433775000L);        // 33 cm no existe en R1
    CHECK(mapFrequency(LORA, 1, 3, 434123000L) == 434123000L);        // Custom valido en ambas
    CHECK(mapFrequency(FSK, 1, 2, 432500000L) == 433775000L);         // 432.500 es solo de R1
    CHECK(mapFrequency(CW, 1, 2, 432075000L) == 432050000L);
    CHECK(mapFrequency(CW, 2, 3, 902100000L) == 432050000L);
    CHECK(mapFrequency(CWB, 1, 2, 432410000L) == 432320000L);
    CHECK(mapFrequency(CWB, 2, 1, 432350000L) == 432410000L);
    CHECK(mapFrequency(CWB, 1, 3, 432050000L) == 432050000L);
    CHECK(mapFrequency(LORA, 2, 2, 12345L) == 12345L);                // misma region: no se toca
    CHECK(mapFrequency(LORA, 1, 2, 0L) == 0L);
    printf(fails ? "%d fallos\n" : "OK\n", fails);
    return fails ? 1 : 0;
}
