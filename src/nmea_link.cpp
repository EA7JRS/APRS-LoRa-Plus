/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of LoRa APRS Tracker.
 *
 * LoRa APRS Tracker is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LoRa APRS Tracker is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LoRa APRS Tracker. If not, see <https://www.gnu.org/licenses/>.
 */

// Pegamento con el hardware del motor NMEA_Core: GPS propio, sensor ambiental, USB, TCP (WiFi) y UART.
// Se activa con NMEA_OUTPUT en el board_pinout.h de la variante.

#include "board_pinout.h"
#include "nmea_link.h"
#include "power_utils.h"
#include "board_pins.h"

#ifdef NMEA_OUTPUT

#include <Arduino.h>
#include <WiFi.h>
#include <TinyGPS++.h>
#include <TimeLib.h>
#include <logger.h>
#include "configuration.h"
#include "nmea_core.h"
#include "gps_utils.h"
#include "mag_utils.h"
#include "baro_utils.h"
#include "wx_utils.h"
#include <string.h>

#ifndef NMEA_USB
    #define NMEA_USB                1
#endif
#ifndef NMEA_TCP
    #define NMEA_TCP                0
#endif
#ifndef NMEA_TCP_PORT
    #define NMEA_TCP_PORT           10110
#endif
#ifndef NMEA_WIFI_SSID
    #define NMEA_WIFI_SSID          ""
#endif
#ifndef NMEA_WIFI_PASSWORD
    #define NMEA_WIFI_PASSWORD      ""
#endif
#ifndef NMEA_UART_TX_PIN
    #define NMEA_UART_TX_PIN        -1
#endif
#ifndef NMEA_UART_BAUD
    #define NMEA_UART_BAUD          38400
#endif
// La UART (hacia un conversor RS-422 y un plotter Garmin) solo envia lo que el plotter acepta por NMEA 0183: !AIVDM (VDM) y MDA
#ifndef NMEA_UART_PLOTTER_ONLY
    #define NMEA_UART_PLOTTER_ONLY  1
#endif
// Grupos de sentencias que se envian (Garmin GPSMAP 7x3/9x3/12x3/16x3 por NMEA 0183: solo acepta MDA y VDM; la posicion propia no)
#ifndef NMEA_OUT_GNSS
    #define NMEA_OUT_GNSS           1
#endif
#ifndef NMEA_OUT_ENV
    #define NMEA_OUT_ENV            1
#endif
#ifndef NMEA_OUT_AIS
    #define NMEA_OUT_AIS            1
#endif
#ifndef NMEA_N2K_SEASMART
    #define NMEA_N2K_SEASMART       0
#endif
#ifndef NMEA_N2K_ACTISENSE
    #define NMEA_N2K_ACTISENSE      0
#endif
#ifndef NMEA_AIS_MID
    #define NMEA_AIS_MID            224
#endif
#ifndef NMEA_N2K_SOURCE
    #define NMEA_N2K_SOURCE         0x42
#endif

extern TinyGPSPlus      gps;
extern Configuration    Config;
extern logging::Logger  logger;
extern bool             gpsIsActive;

namespace {

    constexpr uint8_t   MAX_CLIENTS = 3;

    NMEA_Core::Engine   engine;
    __attribute__((unused)) WiFiServer*         server              = nullptr;      // solo se usa con NMEA_TCP
    WiFiClient          clients[MAX_CLIENTS];
    HardwareSerial      nmeaSerial(2);
    bool                uartActive          = false;
    __attribute__((unused)) uint32_t            lastNetworkCheck    = 0;

    // Garmin GPSMAP 7x3/9x3/12x3/16x3: por NMEA 0183 solo se reciben VDM (AIS) y MDA entre las sentencias que genera el tracker
    bool plotterAccepts(const char* text, size_t length) {
        if (length < 10) return false;
        if (strncmp(text, "!AIVDM", 6) == 0) return true;
        return text[0] == '$' && strncmp(text + 3, "MDA,", 4) == 0;
    }

    // Entrega cada frase a todos los destinos activos, sin bloquear nunca el bucle principal
    void deliver(const char* text, size_t length, void*) {
        #if NMEA_USB
            // En ESP32-S3 (HWCDC), escribir con setTxTimeoutMs(0) envia datos incluso a programas de PC que no activan la linea DTR
            Serial.write((const uint8_t*) text, length);
        #endif
        if (uartActive && (!NMEA_UART_PLOTTER_ONLY || plotterAccepts(text, length)) && nmeaSerial.availableForWrite() >= (int) length) {
            nmeaSerial.write((const uint8_t*) text, length);
        }
        for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i] && clients[i].connected()) {
                if (clients[i].write((const uint8_t*) text, length) == 0) clients[i].stop();
            }
        }
    }

    void startNetwork() {
        #if NMEA_TCP
            if (Config.wifiAP.active) return;                   // en modo configuracion el WiFi lo usa el configurador web
            if (strlen(NMEA_WIFI_SSID) > 0) {
                WiFi.mode(WIFI_STA);
                WiFi.begin(NMEA_WIFI_SSID, NMEA_WIFI_PASSWORD);
            } else {
                WiFi.mode(WIFI_AP);
                WiFi.softAP("LoRaTracker-NMEA", Config.wifiAP.password);
            }
            if (server == nullptr) server = new WiFiServer(NMEA_TCP_PORT);
            server->begin();
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "NMEA", "TCP server on port %d", NMEA_TCP_PORT);
        #endif
    }

    __attribute__((unused)) void serveClients() {                   // sin NMEA_TCP no se llama
        #if NMEA_TCP
            if (server == nullptr) return;
            WiFiClient incoming = server->available();
            if (!incoming) return;
            for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
                if (!clients[i] || !clients[i].connected()) {
                    clients[i] = incoming;
                    clients[i].setNoDelay(true);
                    return;
                }
            }
            incoming.stop();                                    // sin hueco libre
        #endif
    }

    NMEA0183::Gnss readGnss() {
        NMEA0183::Gnss g;
        // En Static Hold el M10 puede congelar coordenadas aunque siga enviando tramas validas: miramos tambien time.age() y satellites.age()
        bool streamAlive = (gps.location.age() < 15000) || (gps.time.age() < 15000) || (gps.satellites.age() < 15000) || !gpsIsActive;
        bool liveFix     = gps.location.isValid() && streamAlive && (gps.satellites.value() >= 3 || !gpsIsActive);
        bool ecoFallback = !liveFix && !gpsIsActive && GPS_Utils::hasLastPosition();

        g.valid      = liveFix || ecoFallback;
        g.satellites = gps.satellites.isValid() ? (int) gps.satellites.value() : (ecoFallback ? 4 : 0);
        g.hdop       = (gps.hdop.isValid() && gps.hdop.hdop() > 0.0) ? gps.hdop.hdop() : (g.valid ? 1.2 : 99.9);

        if (liveFix) {
            g.lat        = gps.location.lat();
            g.lon        = gps.location.lng();
            g.sogKnots   = GPS_Utils::getSpeedKnots();
            g.cogDegrees = GPS_Utils::isMoving() ? GPS_Utils::getCourseDeg()
                         : (MAG_Utils::isHeadingValid() ? MAG_Utils::heading() : GPS_Utils::getCourseDeg());
            if (gps.altitude.isValid() || BARO_Utils::altitudeValid()) {
                g.hasAltitude = true;
                g.altitudeM   = GPS_Utils::getAltitudeMeters();
            }
        } else if (ecoFallback) {
            g.lat        = GPS_Utils::getLastLat();
            g.lon        = GPS_Utils::getLastLng();
            g.sogKnots   = 0.0;
            g.cogDegrees = MAG_Utils::isHeadingValid() ? MAG_Utils::heading() : GPS_Utils::getCourseDeg();
            if (BARO_Utils::altitudeValid()) {
                g.hasAltitude = true;
                g.altitudeM   = BARO_Utils::getAltitude();
            } else if (GPS_Utils::hasLastAltitude()) {
                g.hasAltitude = true;
                g.altitudeM   = GPS_Utils::getLastAltitude();
            }
        }

        // Fecha y hora: del GPS si es reciente, o del reloj del sistema/RTC si ya esta sincronizado
        if (gps.time.isValid() && gps.date.isValid() && gps.date.year() >= 2024 && gps.time.age() < 5000) {
            g.hasTime = true;
            g.hour    = gps.time.hour();
            g.minute  = gps.time.minute();
            g.second  = gps.time.second();
            g.centis  = gps.time.centisecond();
            g.hasDate = true;
            g.day     = gps.date.day();
            g.month   = gps.date.month();
            g.year    = gps.date.year();
        } else if (year() >= 2024) {
            time_t t  = now();
            g.hasTime = true;
            g.hour    = hour(t);
            g.minute  = minute(t);
            g.second  = second(t);
            g.centis  = 0;
            g.hasDate = true;
            g.day     = day(t);
            g.month   = month(t);
            g.year    = year(t);
        }
        return g;
    }

    NMEA0183::Environment readEnvironment() {
        NMEA0183::Environment e;
        float temperature = 0, humidity = 0, pressure = 0;
        bool  hasTemperature = false, hasHumidity = false, hasPressure = false;
        if (WX_Utils::getEnvironment(temperature, hasTemperature, humidity, hasHumidity, pressure, hasPressure)) {
            e.hasTemperature = hasTemperature;
            e.temperatureC   = temperature;
            e.hasHumidity    = hasHumidity;
            e.humidityPct    = humidity;
            e.hasPressure    = hasPressure;
            e.pressureHpa    = pressure;
        }
        return e;
    }

}

namespace NMEA_Link {

    void setup() {
        #if NMEA_USB
            Serial.setTxTimeoutMs(0);   // evita que el USB CDC se bloquee o descarte tramas si el software del PC no activa DTR
        #endif
        NMEA_Core::Options options;
        options.gnss        = NMEA_OUT_GNSS != 0;
        options.environment = NMEA_OUT_ENV != 0;
        options.ais         = NMEA_OUT_AIS != 0;
        options.seaSmart    = NMEA_N2K_SEASMART != 0;
        options.actisense   = NMEA_N2K_ACTISENSE != 0;
        options.mid         = NMEA_AIS_MID;
        options.n2kSource   = NMEA_N2K_SOURCE;
        engine.configure(options);
        engine.setSink(deliver, nullptr);
        if (Config.nmeaUart.txPin >= 0) {                           // pin y velocidad de la WebUI (Hardware > NMEA output); aplican al reiniciar
            if (POWER_Utils::pinReserved(Config.nmeaUart.txPin) || BOARD_PINS::inputOnly(Config.nmeaUart.txPin)) {
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "NMEA", "UART pin %d conflicts with an internal bus, disabled", Config.nmeaUart.txPin);
            } else {
                nmeaSerial.begin(Config.nmeaUart.baud, SERIAL_8N1, -1, Config.nmeaUart.txPin);
                uartActive = true;
            }
        }
        startNetwork();
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "NMEA", "Output ready (USB %d, TCP %d, UART %d)", NMEA_USB, NMEA_TCP, uartActive ? 1 : 0);
    }

    void update() {
        uint32_t now = millis();
        #if NMEA_TCP
            serveClients();
            if (now - lastNetworkCheck > 5000) {                // el configurador puede apagar el WiFi: se reactiva
                lastNetworkCheck = now;
                if (WiFi.getMode() == WIFI_MODE_NULL && !Config.wifiAP.active) startNetwork();
            }
        #endif
        engine.update(now, readGnss(), readEnvironment());
    }

    void onAprsPosition(const char* callsign, double lat, double lon, double speedKmh, double courseDeg, const char* packetText) {
        double sogKnots = speedKmh >= 0.0 ? speedKmh / 1.852 : -1.0;
        double cog      = speedKmh > 1.0 ? courseDeg : -1.0;       // el rumbo solo es fiable en movimiento
        AIS_Vessel::Info info;
        bool hasInfo = packetText != nullptr && AIS_Vessel::parseTag(packetText, info);
        engine.onAprsStation(millis(), callsign, lat, lon, sogKnots, cog, readGnss(), hasInfo ? &info : nullptr);
    }

}

#else

namespace NMEA_Link {
    void setup() {}
    void update() {}
    void onAprsPosition(const char*, double, double, double, double, const char*) {}
}

#endif