// Pruebas en el PC del barco propio como !AIVDO (transpondedor AIS por APRS)
//   g++ -std=c++17 -O2 -Wall -Wextra -Iinclude tools/nmea/test_vdo.cpp src/nmea_core.cpp src/nmea0183.cpp src/nmea2000.cpp
//       src/ais_vessel.cpp src/ais_core.cpp -o /tmp/test_vdo && /tmp/test_vdo
// Copyright (C) 2026 Manuel - EA7JRS. GPL-3.0-or-later.
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <string>
#include <vector>
#include "nmea_core.h"
#include "nmea0183.h"
#include "ais_core.h"
#include "ais_vessel.h"

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FALLO: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static std::vector<std::string> out;
static void sink(const char* text, size_t length, void*) {
    std::string s(text, length);
    size_t a = 0;
    while (a < s.size()) {
        size_t b = s.find("\r\n", a);
        if (b == std::string::npos) b = s.size();
        if (b > a) out.push_back(s.substr(a, b - a));
        a = b + 2;
    }
}

static bool checksumOk(const std::string& line) {
    size_t star = line.find('*');
    if (star == std::string::npos || star + 3 > line.size()) return false;
    uint8_t cs = 0;
    for (size_t i = 1; i < star; i++) cs ^= (uint8_t) line[i];
    char hex[3];
    snprintf(hex, sizeof(hex), "%02X", cs);
    return line.compare(star + 1, 2, hex) == 0;
}

static int countPrefix(const char* prefix) {
    int n = 0;
    for (auto& l : out) if (l.rfind(prefix, 0) == 0) n++;
    return n;
}

// Junta las frases !AIVDO y devuelve los mensajes decodificados
static std::vector<AisCore::Report> decodeVdo() {
    std::vector<AisCore::Report> r;
    AisCore::NmeaReader reader;
    for (auto& l : out) {
        if (l.rfind("!AIVDO", 0) != 0) continue;
        if (reader.feed(l.c_str())) {
            AisCore::Report rep;
            if (AisCore::decode(reader.data, reader.nbits, rep)) r.push_back(rep);
        }
    }
    return r;
}

static NMEA0183::Gnss fix(double lat, double lon, double sog, double cog, int second) {
    NMEA0183::Gnss g;
    g.valid = true; g.lat = lat; g.lon = lon; g.sogKnots = sog; g.cogDegrees = cog; g.satellites = 8; g.hdop = 1.0;
    g.hasTime = true; g.hour = 12; g.minute = 0; g.second = second; g.hasDate = true; g.day = 13; g.month = 11; g.year = 2026;
    return g;
}

int main() {
    // ---- toVdo ----
    {
        char line[200];
        size_t n = NMEA0183::aisType18(224123456, 36.5, -6.3, 5.0, 90.0, 10, line, sizeof(line), true);
        std::string before(line, n);
        int changed = NMEA0183::toVdo(line, n);
        std::string after(line, n);
        CHECK(changed == 1, "toVdo tipo 18: %d frases", changed);
        CHECK(after.rfind("!AIVDO,", 0) == 0, "toVdo tipo 18: %s", after.c_str());
        CHECK(checksumOk(after.substr(0, after.find("\r\n"))), "checksum tipo 18: %s", after.c_str());
        CHECK(before.substr(7, before.find('*') - 7) == after.substr(7, after.find('*') - 7), "datos iguales");
        CHECK(NMEA0183::toVdo(line, n) == 0, "segunda vez no cambia nada");

        char five[260];
        n = NMEA0183::aisType5(224123456, "EA7JRS", "SEA BREEZE", 37, 6, 6, 2, 2, 18, 3, five, sizeof(five));
        changed = NMEA0183::toVdo(five, n);
        CHECK(changed == 2, "toVdo tipo 5: %d frases", changed);
        out.clear(); sink(five, n, nullptr);
        CHECK(out.size() == 2 && checksumOk(out[0]) && checksumOk(out[1]), "checksum tipo 5");
        auto r = decodeVdo();
        CHECK(r.size() == 1 && r[0].type == 5 && strcmp(r[0].name, "SEA BREEZE") == 0 && strcmp(r[0].call, "EA7JRS") == 0, "tipo 5 decodificado");
        char gps[100];
        n = NMEA0183::rmc(fix(36, -6, 1, 1, 0), gps, sizeof(gps));
        CHECK(NMEA0183::toVdo(gps, n) == 0, "RMC no se toca");
    }

    // ---- motor: barco propio clase B "CS" ----
    NMEA_Core::Options o;
    o.gnss = false; o.environment = false;
    NMEA_Core::Engine e;
    e.configure(o);
    e.setSink(sink, nullptr);
    AIS_Vessel::Info info;
    info.aisClass = AIS_Vessel::CLASS_B_CS; info.mmsi = 224123456; info.length = 12; info.beam = 4; info.draughtDm = 18; info.shipType = 37;
    strcpy(info.name, "SEA BREEZE");
    e.setOwnVessel("EA7JRS-5", info);
    CHECK(e.hasOwnVessel() && e.ownMmsi() == 224123456, "barco propio con MMSI real");

    NMEA0183::Environment env;
    NMEA0183::Gnss nofix;
    out.clear();
    e.update(0, nofix, env);
    CHECK(countPrefix("!AIVDO") == 2, "sin fix: solo estaticos 24A/B (%d)", countPrefix("!AIVDO"));
    CHECK(countPrefix("!AIVDM") == 0, "sin fix: nada como VDM");
    out.clear();
    e.update(1000, fix(36.5, -6.3, 5.2, 270.0, 1), env);
    auto r = decodeVdo();
    CHECK(r.size() == 1 && r[0].type == 18 && r[0].mmsi == 224123456, "posicion tipo 18 propia");
    if (!r.empty()) {
        CHECK(fabs(r[0].lat - 36.5) < 1e-4 && fabs(r[0].lon + 6.3) < 1e-4, "posicion %f %f", r[0].lat, r[0].lon);
        CHECK(fabs(r[0].sogKn - 5.2f) < 0.06f && fabs(r[0].cogDeg - 270.0f) < 0.11f, "sog %f cog %f", r[0].sogKn, r[0].cogDeg);
    }
    out.clear();
    e.update(5000, fix(36.5, -6.3, 5.2, 270.0, 5), env);
    CHECK(out.empty(), "a los 4 s no repite");
    e.update(11000, fix(36.5, -6.3, 5.2, 270.0, 11), env);
    CHECK(countPrefix("!AIVDO") == 1, "a los 10 s repite la posicion");
    out.clear();
    e.update(361000, fix(36.5, -6.3, 5.2, 270.0, 1), env);
    CHECK(countPrefix("!AIVDO") == 3, "a los 6 min posicion y 24A/B (%d)", countPrefix("!AIVDO"));
    r = decodeVdo();
    bool a = false, b = false;
    for (auto& x : r) {
        if (x.type == 24 && x.part == 0) a = strcmp(x.name, "SEA BREEZE") == 0;
        if (x.type == 24 && x.part == 1) b = strcmp(x.call, "EA7JRS") == 0 && x.shipType == 37 && x.toBow + x.toStern == 12 && x.toPort + x.toStar == 4;
    }
    CHECK(a && b, "24A nombre y 24B indicativo, tipo y dimensiones");

    // nuestra propia baliza repetida por un digipeater no crea un blanco; otra estacion si (como VDM)
    out.clear();
    e.onAprsStation(362000, "EA7JRS-5", 36.5, -6.3, 5, 270, fix(36.5, -6.3, 5, 270, 2), &info);
    e.onAprsStation(362000, "EA7JRS-9", 36.5, -6.3, 5, 270, fix(36.5, -6.3, 5, 270, 2));
    CHECK(out.empty() && e.stationCount() == 0, "indicativo propio ignorado (%zu frases)", out.size());
    e.onAprsStation(362000, "EA1ABC-5", 36.6, -6.2, 3, 90, fix(36.5, -6.3, 5, 270, 2));
    CHECK(countPrefix("!AIVDM") == 3 && countPrefix("!AIVDO") == 0, "otra estacion como VDM");

    // mismo dato: no vuelve a mandar estaticos; dato nuevo: si
    out.clear();
    e.setOwnVessel("EA7JRS-5", info);
    e.update(363000, nofix, env);
    CHECK(out.empty(), "setOwnVessel igual: sin estaticos nuevos");
    strcpy(info.name, "SEA BREEZE II");
    e.setOwnVessel("EA7JRS-5", info);
    e.update(364000, nofix, env);
    CHECK(countPrefix("!AIVDO") == 2, "nombre cambiado: estaticos al momento");

    // ---- clase A y MMSI sin dar ----
    NMEA_Core::Engine ea;
    ea.configure(o);
    ea.setSink(sink, nullptr);
    AIS_Vessel::Info ia;
    ia.aisClass = AIS_Vessel::CLASS_A; ia.mmsi = 0; ia.length = 30; ia.beam = 8; ia.draughtDm = 25; ia.shipType = 36;
    strcpy(ia.name, "VELERO");
    ea.setOwnVessel("EA7JRS-8", ia);
    CHECK(ea.ownMmsi() / 100000000U == 8, "sin MMSI: el ficticio 8xxxxxxxx (%u)", ea.ownMmsi());
    out.clear();
    ea.update(0, fix(36.5, -6.3, 6.0, 45.0, 0), env);
    r = decodeVdo();
    bool p1 = false, s5 = false;
    for (auto& x : r) {
        if (x.type == 1) p1 = x.status == 8 && x.hdg == 45;
        if (x.type == 5) s5 = strcmp(x.name, "VELERO") == 0 && fabs(x.draughtM - 2.5f) < 0.05f;
    }
    CHECK(p1, "clase A: tipo 1 a vela con rumbo");
    CHECK(s5, "clase A: tipo 5 con nombre y calado");
    for (auto& l : out) CHECK(checksumOk(l), "checksum %s", l.c_str());

    // ---- desactivado ----
    o.ownVessel = false;
    NMEA_Core::Engine off;
    off.configure(o);
    off.setSink(sink, nullptr);
    off.setOwnVessel("EA7JRS-5", info);
    out.clear();
    off.update(0, fix(36.5, -6.3, 5, 270, 0), env);
    CHECK(out.empty(), "ownVessel = false: nada");
    off.clearOwnVessel();
    CHECK(!off.hasOwnVessel(), "clearOwnVessel");

    printf("%d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
