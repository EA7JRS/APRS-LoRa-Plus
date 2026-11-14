/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of LoRa APRS Tracker.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "mt_check.h"

#ifdef APRS_MAPRS_TRANSPONDER

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <logger.h>
#include "configuration.h"
#include "ais_vessel.h"
#include "mtcheck.h"
#include "nmea_link.h"
#include "station_utils.h"

extern Configuration    Config;
extern logging::Logger  logger;

namespace MT_Check {

    static volatile bool busy = false, done = false;
    static int       httpCode = 0;
    static String    body;
    static String    message;
    static uint32_t  mmsi = 0;
    static char      call[8] = "";
    static String    key;

    static bool joinWifi() {
        if (WiFi.status() == WL_CONNECTED) return true;
        if (Config.fwUpdate.ssid.isEmpty()) return false;
        const wifi_mode_t m = WiFi.getMode();
        WiFi.mode((m == WIFI_MODE_AP || m == WIFI_MODE_APSTA) ? WIFI_MODE_APSTA : WIFI_MODE_STA);
        WiFi.begin(Config.fwUpdate.ssid.c_str(), Config.fwUpdate.password.c_str());
        const uint32_t t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(100);
        return WiFi.status() == WL_CONNECTED;
    }

    static void task(void*) {
        int code = -1;
        String text;
        if (joinWifi()) {
            WiFiClientSecure tls;
            tls.setInsecure();                                          // cifrado sin comprobar el certificado (sin reloj fiable)
            HTTPClient http;
            http.setConnectTimeout(8000);
            http.setTimeout(10000);
            http.setUserAgent("APRS-LoRa-Plus-MAPRS");
            if (http.begin(tls, plotter::mtcheck::searchUrl(key.c_str(), mmsi).c_str())) {
                code = http.GET();
                if (code > 0) text = http.getString();
                http.end();
            }
        }
        httpCode = code;
        body = text;
        done = true;
        vTaskDelete(nullptr);
    }

    static int aisIndex() { return Config.aisBeaconIndex(); }

    bool start() {
        if (busy) return false;
        const int i = aisIndex();
        if (i < 0) { message = "No nautical AIS profile"; return false; }
        const Beacon& b = Config.beacons[i];
        AIS_Vessel::sanitizeCall(b.aisCall.c_str(), call, sizeof(call));
        mmsi = b.aisMmsi;
        key = b.aisMtKey;
        if (!call[0] || !AIS_Vessel::validMmsi(mmsi)) { message = "Save the MMSI and the maritime call sign first"; return false; }
        if (!plotter::mtcheck::validKey(key.c_str())) { message = "Save the MarineTraffic API key (40 characters) first"; return false; }
        if (Config.fwUpdate.ssid.isEmpty() && WiFi.status() != WL_CONNECTED) {
            message = "No internet: set a WiFi network in Connectivity > WiFi (or check it from the plotter)";
            return false;
        }
        busy = true;
        done = false;
        message = "Asking MarineTraffic...";
        if (xTaskCreate(task, "mtcheck", 8192, nullptr, 1, nullptr) != pdPASS) { busy = false; message = "Not enough memory"; return false; }
        return true;
    }

    void loop() {
        if (!done) return;
        done = false;
        busy = false;
        if (httpCode < 0) { message = "No connection with MarineTraffic (WiFi / internet)"; return; }
        char mt[8];
        const plotter::mtcheck::Result r = plotter::mtcheck::parse(httpCode, body.c_str(), call, mt, sizeof(mt));
        body = "";
        message = plotter::mtcheck::resultEn(r);
        if (r == plotter::mtcheck::MISMATCH) message += String(" (") + mt + ")";
        const int i = aisIndex();
        if (i < 0) return;
        Beacon& b = Config.beacons[i];
        char pair[24];
        plotter::mtcheck::pairKey(mmsi, call, pair, sizeof(pair));
        if (r == plotter::mtcheck::MATCH) {
            b.aisCallOk = plotter::mtcheck::MARINETRAFFIC;
            b.aisCallPair = pair;
        } else if (r == plotter::mtcheck::MISMATCH && b.aisCallOk == plotter::mtcheck::MARINETRAFFIC) {
            b.aisCallOk = 0;
            b.aisCallPair = "";
        } else return;
        Config.writeFile();
        STATION_Utils::aisStaticChanged();
        NMEA_Link::ownVesselChanged();
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "AIS", "Maritime call sign %s: %s", call, message.c_str());
    }

    String statusJson() {
        JsonDocument doc;
        doc["busy"] = (bool) busy;
        doc["msg"] = message;
        const int i = aisIndex();
        if (i >= 0) {
            const Beacon& b = Config.beacons[i];
            doc["ok"] = Config.aisCallVerified(b);
            doc["src"] = Config.aisCallVerified(b) ? plotter::mtcheck::sourceName(b.aisCallOk) : "";
            doc["warn"] = !b.aisCall.isEmpty() && !Config.aisCallVerified(b);
            doc["sent"] = Config.aisCallToSend(b);
        }
        String out;
        serializeJson(doc, out);
        return out;
    }
}

#else

namespace MT_Check {
    bool start() { return false; }
    void loop() {}
    String statusJson() { return "{\"busy\":false,\"msg\":\"Only on the MAPRS transponder\"}"; }
}

#endif
