// Prueba en PC de include/casic_pcas.h: g++ -std=c++17 -Wall -Wextra -Werror -Iinclude tools/test_casic.cpp -o /tmp/tc && /tmp/tc
// Copyright (c) 2026 Manuel - EA7JRS
#include "casic_pcas.h"
#include <cstdio>
#include <cstring>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FALLO linea %d: %s\n", __LINE__, #c); fails++; } } while (0)
static void line(const char* body, const char* want) {      // want = linea completa sin CRLF, tomada de la documentacion
    char b[96]; size_t n = Casic::build(body, b, sizeof b);
    char exp[96]; snprintf(exp, sizeof exp, "%s\r\n", want);
    if (n == 0 || strcmp(b, exp) != 0) { printf("FALLO %s -> %s (esperado %s)\n", body, b, want); fails++; }
}
int main() {
    // vectores de la documentacion del fabricante
    line("PCAS01,5", "$PCAS01,5*19");
    line("PCAS01,1", "$PCAS01,1*1D");
    line("PCAS02,1000", "$PCAS02,1000*2E");
    line("PCAS04,3", "$PCAS04,3*1A");
    line("PCAS04,1", "$PCAS04,1*18");
    line("PCAS04,2", "$PCAS04,2*1B");
    line("PCAS10,0", "$PCAS10,0*1C");
    line("PCAS10,1", "$PCAS10,1*1D");
    line("PCAS10,2", "$PCAS10,2*1E");
    line("PCAS10,3", "$PCAS10,3*1F");
    line("PCAS03,1,1,1,1,1,1,1,1,0,0,,,0,0", "$PCAS03,1,1,1,1,1,1,1,1,0,0,,,0,0*02");
    // constelaciones
    CHECK(Casic::constellationMode(Casic::GNSS_GPS) == 1);
    CHECK(Casic::constellationMode(Casic::GNSS_BEIDOU) == 2);
    CHECK(Casic::constellationMode(Casic::GNSS_GPS | Casic::GNSS_BEIDOU) == 3);
    CHECK(Casic::constellationMode(Casic::GNSS_GLONASS) == 4);
    CHECK(Casic::constellationMode(Casic::GNSS_GPS | Casic::GNSS_GLONASS) == 5);
    CHECK(Casic::constellationMode(Casic::GNSS_BEIDOU | Casic::GNSS_GLONASS) == 6);
    CHECK(Casic::constellationMode(7 | 4 | 8) == 7);
    CHECK(Casic::constellationMode(Casic::GNSS_GALILEO | Casic::GNSS_SBAS | Casic::GNSS_QZSS) == 0);
    CHECK(Casic::sanitizeMask(Casic::GNSS_GALILEO | Casic::GNSS_SBAS) == (Casic::GNSS_GPS | Casic::GNSS_BEIDOU));
    CHECK(Casic::sanitizeMask(35) == Casic::GNSS_GPS);                    // GPS + Galileo + SBAS (valor de fabrica del M10) -> GPS
    CHECK(Casic::sanitizeMask(Casic::GNSS_BEIDOU | Casic::GNSS_GLONASS | Casic::GNSS_QZSS) == (Casic::GNSS_BEIDOU | Casic::GNSS_GLONASS));
    // tasa
    CHECK(Casic::rateMs(200) == 200); CHECK(Casic::rateMs(250) == 250); CHECK(Casic::rateMs(500) == 500);
    CHECK(Casic::rateMs(1000) == 1000); CHECK(Casic::rateMs(2000) == 1000); CHECK(Casic::rateMs(10000) == 1000);
    CHECK(Casic::rateMs(100) == 200); CHECK(Casic::rateMs(700) == 500);
    // PCAS03
    char b[64];
    Casic::pcas03Body(false, 1000, b, sizeof b); CHECK(strcmp(b, "PCAS03,1,1,1,1,1,1,1,1,0,0,,,0,0") == 0);
    Casic::pcas03Body(true, 1000, b, sizeof b);  CHECK(strcmp(b, "PCAS03,1,0,0,1,1,0,0,1,0,0,,,0,0") == 0);
    Casic::pcas03Body(true, 200, b, sizeof b);   CHECK(strcmp(b, "PCAS03,1,0,0,5,1,0,0,1,0,0,,,0,0") == 0);
    { int fields = 1; for (const char* c = b; *c; c++) if (*c == ',') fields++; CHECK(fields == 15); }   // PCAS03 + 14 campos
    // antena
    CHECK(Casic::antennaFromText("ANTENNA OPEN") == Casic::ANT_OPEN);
    CHECK(Casic::antennaFromText("ANTENNA OK") == Casic::ANT_OK);
    CHECK(Casic::antennaFromText("ANTENNA SHORT") == Casic::ANT_SHORT);
    CHECK(Casic::antennaFromText("SW=URANUS5") == Casic::ANT_UNKNOWN);
    CHECK(Casic::antennaFromText(nullptr) == Casic::ANT_UNKNOWN);
    // baudios
    CHECK(Casic::needsFastBaud(500, 9600)); CHECK(Casic::needsFastBaud(200, 9600));
    CHECK(!Casic::needsFastBaud(1000, 9600)); CHECK(!Casic::needsFastBaud(200, 115200));
    // buffer pequeno
    char tiny[8]; CHECK(Casic::build("PCAS04,3", tiny, sizeof tiny) == 0);
    printf(fails ? "fails=%d\n" : "fails=0\n", fails);
    return fails != 0;
}
