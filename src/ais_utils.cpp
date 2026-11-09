/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Receptor AIS del tracker. Ver include/ais_utils.h
 */

#include "ais_utils.h"
#include <TinyGPS++.h>
#include <logger.h>
#include "configuration.h"
#include "board_pinout.h"
#include "power_utils.h"
#include "gps_utils.h"
#include "display.h"
#include "pages_utils.h"
#include "ble_control.h"
#include "nmea_link.h"
#ifdef HAS_SA868
#include "sa868_radio.h"
#endif

extern Configuration    Config;
extern logging::Logger  logger;
extern TinyGPSPlus      gps;

namespace AIS_Utils {

    static AisCore::Table      tbl;
    static AisCore::NmeaReader moduleReader, appReader;
    static HardwareSerial*     uart = nullptr;
    static char                lineBuf[100];
    static size_t              lineLen = 0;
    static uint32_t            nMsgs = 0, lastMs = 0, lastPurgeMs = 0, lastAlarmMs = 0, chanMs = 0;
    static uint8_t             lastSrc = 0xFF;
    static int                 chanMode = 0;
    static int                 nmeaSeq = 0;
    static bool                fromApp = false;

    AisCore::Table& table() { return tbl; }
    uint32_t messages() { return nMsgs; }
    uint32_t lastRxMs() { return lastMs; }

    bool active() {
        return Config.operatingMode == MODE_AIS || uart != nullptr || fromApp || tbl.count() > 0;
    }

    const char* sourceName() {
        switch (lastSrc) {
            case SRC_RADIO:  return Config.operatingMode == MODE_AIS ? (channel() == 'B' ? "SA868 B" : "SA868 A") : "radio";
            case SRC_MODULE: return "module";
            case SRC_APP:    return "app";
            default:         return Config.operatingMode == MODE_AIS ? (channel() == 'B' ? "SA868 B" : "SA868 A") : (uart ? "module" : "-");
        }
    }

    char channel() {
        #ifdef HAS_SA868
            if (SA868::modeAis()) return SA868::aisChannelNow();
        #endif
        return 0;
    }

    void setChannelMode(int mode) {
        chanMode = constrain(mode, 0, 2);
        chanMs = millis();
        #ifdef HAS_SA868
            if (chanMode < 2) SA868::aisChannel(chanMode == 1 ? 'B' : 'A');
        #endif
    }

    bool own(AisCore::Own& o) {
        o.valid = false;
        double lat = 0, lon = 0;
        if (gps.location.isValid() && gps.location.age() < 10000UL) { lat = gps.location.lat(); lon = gps.location.lng(); }
        else if (GPS_Utils::hasLastPosition()) { lat = GPS_Utils::getLastLat(); lon = GPS_Utils::getLastLng(); }
        else return false;
        o.lat = lat; o.lon = lon;
        const bool moving = gps.speed.isValid() && gps.speed.knots() > 0.5;
        o.sogKn = moving ? (float)gps.speed.knots() : 0.0f;
        o.cogDeg = moving && gps.course.isValid() ? (float)gps.course.deg() : 0.0f;
        o.valid = true;
        return true;
    }

    int sorted(int* idx, int max) {
        AisCore::Own o; own(o);
        return tbl.sorted(o, idx, max);
    }

    void stationId(const AisCore::Vessel& v, char* out, size_t n) {
        // las estaciones de la pantalla tienen 10 caracteres: el MMSI (9 digitos) siempre cabe y es unico
        snprintf(out, n, "%09lu", (unsigned long)v.mmsi);
    }

    bool dangerous(const AisCore::Vessel& v, double* cpaOut, double* tcpaOut, double* rangeOut) {
        AisCore::Own o;
        if (!own(o) || !v.hasPos) return false;
        double nm, brg, c = 0, t = 0;
        AisCore::rangeBearing(o, v.lat, v.lon, nm, brg);
        AisCore::cpa(o, v, c, t);
        if (cpaOut) *cpaOut = c;
        if (tcpaOut) *tcpaOut = t;
        if (rangeOut) *rangeOut = nm;
        if (v.cls == AisCore::CL_ATON || v.cls == AisCore::CL_BASE) return false;
        if (Config.ais.guardTenthNm > 0 && nm < Config.ais.guardTenthNm / 10.0) return true;
        if (!Config.ais.cpaAlarm) return false;
        const bool moving = (v.hasSog && v.sogKn > 0.5f) || o.sogKn > 0.5f;
        return moving && c < Config.ais.cpaTenthNm / 10.0 && t > 0 && t < Config.ais.tcpaMin;
    }

    void goTo(uint32_t mmsi) {
        const AisCore::Vessel* v = tbl.find(mmsi);
        if (!v) return;
        char id[12]; stationId(*v, id, sizeof(id));
        char c[42];
        snprintf(c, sizeof(c), "AIS %s %s", v->name[0] ? v->name : "", AisCore::shipTypeName(v->shipType));
        #ifdef HAS_APRS_PAGES
        PAGES_Utils::aisTarget(id, v->hasPos, v->lat, v->lon, v->hasSog ? v->sogKn * 1.852f : 0.0f, v->hasCog ? v->cogDeg : 0.0f, c, true);
        #else
        (void)c;
        #endif
    }

    static void forward(const uint8_t* msg, size_t nbits, char ch) {
        if (!Config.ais.forwardNmea) return;
        char out[300];
        nmeaSeq = (nmeaSeq + 1) % 10;
        if (AisCore::toNmea(msg, nbits, ch ? ch : 'A', nmeaSeq, out, sizeof(out)) > 0) NMEA_Link::sendRaw(out, strlen(out));
    }

    // evento BLE para la app (como mucho uno por barco cada 3 s)
    static void notifyApp(const AisCore::Vessel& v) {
        static uint32_t lastSent[AisCore::Table::CAP];
        static uint32_t lastMmsi[AisCore::Table::CAP];
        const int slot = (int)(&v - tbl.at(0));
        if (slot < 0 || slot >= AisCore::Table::CAP) return;
        const uint32_t now = millis();
        if (lastMmsi[slot] == v.mmsi && now - lastSent[slot] < 3000) return;
        lastMmsi[slot] = v.mmsi; lastSent[slot] = now;
        BLE_Control::aisEvent(v);
    }

    static void handle(const uint8_t* msg, size_t nbits, char ch, Source src, bool fwd) {
        AisCore::Report r;
        if (fwd) forward(msg, nbits, ch);
        if (!AisCore::decode(msg, nbits, r)) return;
        nMsgs++; lastMs = millis(); lastSrc = src;
        if (src == SRC_APP) fromApp = true;
        AisCore::Vessel* v = tbl.update(r, lastMs, ch);
        if (!v) return;
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "AIS", "%c type %u MMSI %09lu %s", ch ? ch : '-', r.type, (unsigned long)r.mmsi, v->name);
        notifyApp(*v);
        // destino de la brujula / rotor: posicion al dia
        if (v->hasPos && r.hasPos) {
            char id[12]; stationId(*v, id, sizeof(id));
            #ifdef HAS_APRS_PAGES
            PAGES_Utils::aisTarget(id, true, v->lat, v->lon, v->hasSog ? v->sogKn * 1.852f : 0.0f, v->hasCog ? v->cogDeg : 0.0f, nullptr, false);
            #endif
        }
        // socorro: AIS-SART, MOB, EPIRB (por MMSI, estado 14 o mensaje 14 con "SART" / "MOB")
        const bool distress = AisCore::isDistress(v->mmsi) || (r.hasStatus && r.status == 14) ||
                              (r.type == 14 && (strstr(r.text, "SART") || strstr(r.text, "MOB") || strstr(r.text, "EPIRB")));
        if (distress && Config.ais.distressAlarm && !strstr(r.text, "TEST")) {
            char id[12]; stationId(*v, id, sizeof(id));
            char d[42];
            const char* kind = AisCore::distressName(v->mmsi);
            snprintf(d, sizeof(d), "%s %s", kind[0] ? kind : "AIS", r.type == 14 ? r.text : (v->name[0] ? v->name : "ACTIVE"));
            #ifdef HAS_APRS_PAGES
            PAGES_Utils::aisAlarm(id, d, true, v->hasPos, v->lat, v->lon);
            #endif
        }
    }

    void onMessage(const uint8_t* msg, size_t nbits, char ch, Source src) { handle(msg, nbits, ch, src, src != SRC_APP); }   // la app ya tiene lo que manda

    bool feedNmea(const char* line, Source src) {
        AisCore::NmeaReader& rd = src == SRC_APP ? appReader : moduleReader;
        if (!rd.feed(line)) return false;
        handle(rd.data, rd.nbits, rd.channel, src, src == SRC_MODULE);   // del modulo: a la salida NMEA (mensajes completos, otra vez armados)
        return true;
    }

    void clear() { tbl.clear(); fromApp = false; }

    void begin() {
        chanMode = Config.ais.channel;
        chanMs = millis();
        if (!Config.ais.module || Config.ais.rxPin < 0) return;
        if (POWER_Utils::pinReserved(Config.ais.rxPin)) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "AIS", "Module RX pin %d is used by the board: module off", Config.ais.rxPin);
            return;
        }
        uart = NMEA_Link::uartForAis();                              // con la salida NMEA por UART activa se comparte (TX al plotter, RX del modulo)
        if (!uart) {
            static HardwareSerial port(2);
            port.begin(Config.ais.baud, SERIAL_8N1, Config.ais.rxPin, -1);
            uart = &port;
        }
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "AIS", "External module on RX pin %d, %d baud", Config.ais.rxPin, Config.ais.baud);
    }

    static void readModule() {
        if (!uart) return;
        int guard = 0;
        while (uart->available() && guard++ < 512) {
            const char c = (char)uart->read();
            if (c == '\n' || c == '\r') {
                if (lineLen > 6) { lineBuf[lineLen] = 0; feedNmea(lineBuf, SRC_MODULE); }
                lineLen = 0;
            } else if (lineLen + 1 < sizeof(lineBuf)) lineBuf[lineLen++] = c;
            else lineLen = 0;
        }
    }

    void loop() {
        readModule();
        const uint32_t now = millis();
        #ifdef HAS_SA868
            if (SA868::modeAis() && chanMode == 2 && now - chanMs >= (uint32_t)Config.ais.altSec * 1000UL && !SA868::aisCarrier()) {
                chanMs = now;
                SA868::aisChannel(SA868::aisChannelNow() == 'A' ? 'B' : 'A');
            }
        #endif
        if (now - lastPurgeMs > 30000) { lastPurgeMs = now; tbl.purge(now, (uint32_t)Config.ais.maxAgeMin * 60000UL); }
        // riesgo de colision / zona de guardia: cada 5 s, el peor
        if (now - lastAlarmMs > 5000 && tbl.count() > 0) {
            lastAlarmMs = now;
            double worst = 1e9; AisCore::Vessel* wv = nullptr; double wt = 0, wr = 0;
            for (int i = 0; i < tbl.count(); i++) {
                AisCore::Vessel* v = tbl.at(i);
                double c, t, r;
                if (v && now - v->posMs < 180000UL && dangerous(*v, &c, &t, &r) && c < worst) { worst = c; wv = v; wt = t; wr = r; }
            }
            if (wv) {
                char id[12]; stationId(*wv, id, sizeof(id));
                char d[42];
                if (Config.ais.guardTenthNm > 0 && wr < Config.ais.guardTenthNm / 10.0) snprintf(d, sizeof(d), "GUARD %.1fnm %.10s", wr, wv->name);
                else snprintf(d, sizeof(d), "CPA %.2fnm in %.0fmin %.8s", worst, wt, wv->name);
                #ifdef HAS_APRS_PAGES
                PAGES_Utils::aisAlarm(id, d, false, wv->hasPos, wv->lat, wv->lon);
                #endif
            }
        }
    }
}
