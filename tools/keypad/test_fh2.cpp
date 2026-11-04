// Copyright (C) 2026 Manuel - EA7JRS. g++ -std=c++17 -I include tools/keypad/test_fh2.cpp -o /tmp/test_fh2 && /tmp/test_fh2
#include <cstdio>
#include "keypad_fh2.h"
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
int main() {
    // 10 k pull-up a 3,3 V con la escalera del FH-2 (866, 1330, 1820, 2490, 3240 ohm): tension del divisor
    const double R[5] = {866, 1330, 1820, 2490, 3240};
    int cal[5];
    for (int i = 0; i < 5; i++) cal[i] = (int)(3300.0 * R[i] / (R[i] + 10000.0));
    for (int i = 0; i < 5; i++) {
        CHECK(FH2::classify(cal[i], cal, 5) == i);
        CHECK(FH2::classify(cal[i] + 20, cal, 5) == i);      // ruido de +-20 mV
        CHECK(FH2::classify(cal[i] - 20, cal, 5) == i);
    }
    CHECK(FH2::classify(3300, cal, 5) == -1);                // sin tecla
    CHECK(FH2::classify(3050, cal, 5) == -1);
    CHECK(FH2::classify(0, cal, 5) == -1);
    CHECK(FH2::classify(1500, cal, 5) == -1);                // tecla no calibrada (p. ej. MEM): lejos de las cinco
    int none[5] = {0, 0, 0, 0, 0};
    CHECK(FH2::classify(500, none, 5) == -1);                // sin calibrar
    int one[5] = {600, 0, 0, 0, 0};
    CHECK(FH2::classify(650, one, 5) == 0);
    CHECK(FH2::classify(1200, one, 5) == -1);
    printf(fails ? "FAILED\n" : "OK\n");
    return fails;
}
