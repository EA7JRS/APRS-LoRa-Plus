/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * Pruebas en PC de include/rotor_follow.h (fuentes del rotor: baliza CW y ultima estacion oida) y del \dump_state de rot_proto.h.
 *   g++ -std=gnu++17 -Wall -Wextra -Werror -I include tools/rotor_follow/test_rotor_follow.cpp -o /tmp/test_rotor_follow && /tmp/test_rotor_follow
 */
#include <stdio.h>
#include <math.h>
#include "rotor_follow.h"
#include "rot_proto.h"

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FALLO linea %d: %s\n", __LINE__, #c); } } while (0)
#define NEAR(a, b, t) CHECK(fabs((a) - (b)) <= (t))

using namespace RotorFollow;

struct Dummy : RotProto::Rot {
    bool setPos(float, float) override { return true; }
    bool getPos(float& a, float& e) override { a = 0; e = 0; return true; }
    bool park() override { return true; }
    const char* info() override { return "x"; }
};

int main() {
    double la, lo;
    // Locator -> centro del cuadro
    CHECK(locatorToLatLon("IM76", la, lo)); NEAR(la, 36.5, 1e-9); NEAR(lo, -5.0, 1e-9);
    CHECK(locatorToLatLon("im76hr", la, lo)); NEAR(la, 36.729167, 1e-5); NEAR(lo, -5.375, 1e-5);
    CHECK(locatorToLatLon("JN18EU", la, lo)); NEAR(la, 48.854167, 1e-5); NEAR(lo, 2.375, 1e-5);           // Paris
    CHECK(locatorToLatLon("IM76HR45", la, lo)); NEAR(la, 36.73125, 1e-4); NEAR(lo, -5.37917, 1e-4);
    CHECK(!locatorToLatLon("IM7", la, lo));
    CHECK(!locatorToLatLon("ZZ76", la, lo));
    CHECK(!locatorToLatLon("IM76ZZ", la, lo));
    CHECK(!locatorToLatLon("EA7JRS", la, lo));
    CHECK(!locatorToLatLon("5NN5", la, lo));

    // Indicativos
    CHECK(looksLikeCall("EA7JRS") && looksLikeCall("ED7YAA") && looksLikeCall("EA7JRS/P") && looksLikeCall("OH2B"));
    CHECK(!looksLikeCall("VVV") && !looksLikeCall("IM76") && !looksLikeCall("DE") && !looksLikeCall("599"));

    // Balizas CW
    char call[12], loc[10];
    CHECK(parseCwBeacon("VVV VVV DE ED7YAA IM76HR ", call, sizeof(call), loc, sizeof(loc)));
    CHECK(strcmp(call, "ED7YAA") == 0 && strcmp(loc, "IM76HR") == 0);
    CHECK(parseCwBeacon("vvv de eb5aaa/b in80 qrv", call, sizeof(call), loc, sizeof(loc)));
    CHECK(strcmp(call, "EB5AAA/B") == 0 && strcmp(loc, "IN80") == 0);
    CHECK(parseCwBeacon("OH2B KP20 OH2B KP20", call, sizeof(call), loc, sizeof(loc)));          // sin DE
    CHECK(strcmp(call, "OH2B") == 0 && strcmp(loc, "KP20") == 0);
    CHECK(parseCwBeacon("BEACON IO91WM G3XXX", call, sizeof(call), loc, sizeof(loc)));          // locator antes del indicativo
    CHECK(strcmp(call, "G3XXX") == 0 && strcmp(loc, "IO91WM") == 0);
    CHECK(parseCwBeacon("QTH JN18EU", call, sizeof(call), loc, sizeof(loc)) && call[0] == '\0');
    CHECK(!parseCwBeacon("CQ CQ DE EA7JRS K", call, sizeof(call), loc, sizeof(loc)));
    CHECK(!parseCwBeacon("", call, sizeof(call), loc, sizeof(loc)));
    // La ultima baliza del texto manda
    CHECK(parseCwBeacon("DE ED7YAA IM76HR   DE EA1AAA IN73XL", call, sizeof(call), loc, sizeof(loc)));
    CHECK(strcmp(call, "EA1AAA") == 0 && strcmp(loc, "IN73XL") == 0);
    // Palabras demasiado largas (ruido) no rompen nada
    CHECK(parseCwBeacon("EEEEEEEEEEEEEEEEEEEEEEEE DE ED7YAA IM76", call, sizeof(call), loc, sizeof(loc)) && strcmp(call, "ED7YAA") == 0);

    // Filtro de indicativos
    CHECK(callMatches("EA7JRS-9", ""));
    CHECK(callMatches("EA7JRS-9", "EA7JRS") && callMatches("EA7JRS", "ea7jrs"));
    CHECK(callMatches("EA7JRS-9", "EA1AAA, EA7JRS-9") && !callMatches("EA7JRS-7", "EA7JRS-9"));
    CHECK(callMatches("EA7ABC-5", "EA7*") && !callMatches("EA1ABC", "EA7*"));
    CHECK(!callMatches("EA7JRSX", "EA7JRS"));

    // Fuentes
    CHECK(validSource(0) && validSource(4) && validSource(5) && !validSource(3) && !validSource(6));
    CHECK(nextSource(SRC_FIXED, false) == SRC_LAST_HEARD && nextSource(SRC_CW_BEACON, false) == SRC_APRS_TARGET && nextSource(SRC_CW_BEACON, true) == SRC_SAT);

    // \dump_state con el protocolo 1 de Hamlib 4.x (netrotctl espera "done")
    Dummy d;
    RotProto::Session s;
    std::string out;
    s.feed("\\dump_state\n", 12, d, out);
    CHECK(out == "1\n2\nmin_az=0.000000\nmax_az=360.000000\nmin_el=0.000000\nmax_el=90.000000\nsouth_zero=0\nrot_type=AzEl\ndone\n");

    printf("%d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
