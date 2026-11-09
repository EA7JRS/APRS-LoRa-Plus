/* Copyright (C) 2026 Ricardo Guzman - CA2RXU
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

#include <ArduinoJson.h>
#include "configuration.h"
#include "rotor_follow.h"
#include "board_pinout.h"
#include "lora_utils.h"
#include "gps_utils.h"
#include "web_utils.h"
#include "board_pins.h"
#include "display.h"
#include "utils.h"
#include "fw_update.h"
#include "iaru_auto.h"
#include "keypad_fh2.h"

extern Configuration               Config;
extern String                      versionNumber;

extern const char web_index_html[] asm("_binary_data_embed_index_html_gz_start");
extern const char web_index_html_end[] asm("_binary_data_embed_index_html_gz_end");
extern const size_t web_index_html_len = web_index_html_end - web_index_html;

extern const char web_style_css[] asm("_binary_data_embed_style_css_gz_start");
extern const char web_style_css_end[] asm("_binary_data_embed_style_css_gz_end");
extern const size_t web_style_css_len = web_style_css_end - web_style_css;

extern const char web_script_js[] asm("_binary_data_embed_script_js_gz_start");
extern const char web_script_js_end[] asm("_binary_data_embed_script_js_gz_end");
extern const size_t web_script_js_len = web_script_js_end - web_script_js;

extern const char web_bootstrap_css[] asm("_binary_data_embed_bootstrap_css_gz_start");
extern const char web_bootstrap_css_end[] asm("_binary_data_embed_bootstrap_css_gz_end");
extern const size_t web_bootstrap_css_len = web_bootstrap_css_end - web_bootstrap_css;

extern const char web_bootstrap_js[] asm("_binary_data_embed_bootstrap_js_gz_start");
extern const char web_bootstrap_js_end[] asm("_binary_data_embed_bootstrap_js_gz_end");
extern const size_t web_bootstrap_js_len = web_bootstrap_js_end - web_bootstrap_js;

// Declare external symbols for the embedded image data
extern const unsigned char favicon_data[] asm("_binary_data_embed_favicon_png_gz_start");
extern const unsigned char favicon_data_end[] asm("_binary_data_embed_favicon_png_gz_end");
extern const size_t favicon_data_len = favicon_data_end - favicon_data;

namespace WEB_Utils {

    AsyncWebServer server(80);

    void handleNotFound(AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(404, "text/plain", "Not found");
        response->addHeader("Cache-Control", "max-age=3600");
        request->send(response);
    }

    void handleStatus(AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "OK");
    }

    // Version del firmware para la cabecera y el apartado About de la WebUI (una sola fuente: versionNumber)
    void handleVersion(AsyncWebServerRequest *request) {
        request->send(200, "text/plain", versionNumber);
    }

    // Informacion del receptor GNSS para la seccion GPS de la WebUI
    void handleGpsInfo(AsyncWebServerRequest *request) {
        const uint8_t module = GPS_Utils::gpsModule();
        String json = "{\"configurable\":";
        json += GPS_Utils::gpsConfigurable() ? "true" : "false";
        json += ",\"module\":";
        json += String((int)module);                                  // 0 sin respuesta o sin configurar, 1 u-blox (UBX), 2 NMEA sin UBX (CASIC: L76K u otro)
        json += ",\"antenna\":";
        json += String((int)GPS_Utils::antennaStatus());              // L76K: 0 desconocido, 1 OK, 2 abierta, 3 cortocircuito
        json += "}";
        request->send(200, "application/json", json);
    }

    // Lectura del pin ADC del teclado FH-2 (calibracion desde la WebUI)
    void handleKeypad(AsyncWebServerRequest *request) {
        const int pin = request->hasParam("pin") ? request->getParam("pin")->value().toInt() : -1;
        String json;
        if (FH2::adcPinOk(pin)) json = "{\"ok\":true,\"mv\":" + String(FH2::readMv(pin)) + "}";
        else                    json = "{\"ok\":false}";
        AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
        response->addHeader("Cache-Control", "no-store");
        request->send(response);
    }

    void handleHome(AsyncWebServerRequest *request) {

        AsyncWebServerResponse *response = request->beginResponse(200, "text/html", (const uint8_t*)web_index_html, web_index_html_len);
        response->addHeader("Content-Encoding", "gzip");
        request->send(response);
    }

    void handleFavicon(AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(200, "image/x-icon", (const uint8_t*)favicon_data, favicon_data_len);
        response->addHeader("Content-Encoding", "gzip");
        request->send(response);
    }

    void handleReadConfiguration(AsyncWebServerRequest *request) {
        // Se envia el fichero en streaming: la configuracion ocupa varios KB y montarla en un String caracter a caracter fragmentaba el heap
        if (!SPIFFS.exists("/tracker_conf.json")) {
            request->send(404, "application/json", "{}");
            return;
        }
        AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/tracker_conf.json", "application/json");
        response->addHeader("Cache-Control", "no-store");
        request->send(response);
    }

    void handleReceivedPackets(AsyncWebServerRequest *request) {
        JsonDocument data;

        String buffer;

        serializeJson(data, buffer);

        request->send(200, "application/json", buffer);
    }

    void handleWriteConfiguration(AsyncWebServerRequest *request) {
        Serial.println("Got new config from www");

        auto getParamStringSafe = [&](const String& name, const String& defaultValue = "") -> String {
            if (request->hasParam(name, true)) {
                return request->getParam(name, true)->value();
            }
            return defaultValue;
        };

        auto getParamIntSafe = [&](const String& name, int defaultValue = 0) -> int {
            if (request->hasParam(name, true)) {
                return request->getParam(name, true)->value().toInt();
            }
            return defaultValue;
        };

        auto getParamFloatSafe = [&](const String& name, float defaultValue = 0.0) -> float {
            if (request->hasParam(name, true)) {
                return request->getParam(name, true)->value().toFloat();
            }
            return defaultValue;
        };

        auto getParamDoubleSafe = [&](const String& name, double defaultValue = 0.0) -> double {
            if (request->hasParam(name, true)) {
                return request->getParam(name, true)->value().toDouble();
            }
            return defaultValue;
        };

        //  Beacons
        for (int i = 0; i < (int) Config.beacons.size(); i++) {
            String prefix = "beacons." + String(i) + ".";
            Beacon& beacon = Config.beacons[i];

            beacon.callsign         = getParamStringSafe(prefix + "callsign", beacon.callsign);
            beacon.comment          = getParamStringSafe(prefix + "comment", beacon.comment);
            beacon.status           = getParamStringSafe(prefix + "status", beacon.status);
            beacon.profileLabel     = getParamStringSafe(prefix + "profileLabel", beacon.profileLabel);
            beacon.gpsEcoMode      = getParamStringSafe(prefix + "gpsEcoMode", "") == "1";

            if (beacon.aisProfile) {                    // perfil nautico AIS: simbolo, SSID y SmartBeacon salen del tipo de embarcacion
                beacon.aisVesselType = (uint8_t) getParamIntSafe(prefix + "aisVesselType", beacon.aisVesselType);
                beacon.aisName       = getParamStringSafe(prefix + "aisName", beacon.aisName);
                beacon.aisMmsi       = (uint32_t) strtoul(getParamStringSafe(prefix + "aisMmsi", String(beacon.aisMmsi)).c_str(), nullptr, 10);
                beacon.aisLength     = (uint16_t) constrain(getParamIntSafe(prefix + "aisLength", beacon.aisLength), 0, 999);
                beacon.aisBeam       = (uint16_t) constrain(getParamIntSafe(prefix + "aisBeam", beacon.aisBeam), 0, 126);
                beacon.aisDraft      = getParamFloatSafe(prefix + "aisDraft", beacon.aisDraft);
                Config.enforceAisBeacon(beacon);
                continue;
            }

            beacon.symbol           = getParamStringSafe(prefix + "symbol", beacon.symbol);
            beacon.overlay          = getParamStringSafe(prefix + "overlay", beacon.overlay);
            beacon.micE             = getParamStringSafe(prefix + "micE", beacon.micE);
            beacon.smartBeaconActive    = getParamStringSafe(prefix + "smartBeaconActive", "") == "1";
            beacon.smartBeaconSetting   = getParamIntSafe(prefix + "smartBeaconSetting", beacon.smartBeaconSetting);
        }

        //  Station Config
        Config.path                             = getParamStringSafe("path", Config.path);
        Config.sendCommentAfterXBeacons         = getParamIntSafe("sendCommentAfterXBeacons", Config.sendCommentAfterXBeacons);
        Config.nonSmartBeaconRate               = getParamIntSafe("nonSmartBeaconRate", Config.nonSmartBeaconRate);
        Config.standingUpdateTime               = getParamIntSafe("standingUpdateTime", Config.standingUpdateTime);
        Config.email                            = getParamStringSafe("email", Config.email);
        Config.rememberStationTime              = getParamIntSafe("rememberStationTime", Config.rememberStationTime);
        Config.sendAltitude                     = request->hasParam("sendAltitude", true);
        Config.disableGPS                       = request->hasParam("disableGPS", true);
        Config.cadActive                        = request->hasParam("cadActive", true);
        Config.cadWaitSec                       = constrain(getParamIntSafe("cadWaitSec", Config.cadWaitSec), 1, 60);
        Config.cadFskRssi                       = constrain(getParamIntSafe("cadFskRssi", Config.cadFskRssi), -120, -60);
        Config.simplifiedTrackerMode            = request->hasParam("simplifiedTrackerMode", true);

        //  Display
        Config.display.ecoMode                  = request->hasParam("display.ecoMode", true);
        if (Config.display.ecoMode) {
            Config.display.timeout              = getParamIntSafe("display.timeout", Config.display.timeout);
        }
        Config.display.turn180                  = request->hasParam("display.turn180", true);
        Config.display.showSymbol               = request->hasParam("display.showSymbol", true);

        //  Bluetooth
        Config.bluetooth.active                 = request->hasParam("bluetooth.active", true);
        if (Config.bluetooth.active) {
            Config.bluetooth.deviceName         = getParamStringSafe("bluetooth.deviceName", Config.bluetooth.deviceName);
            #ifdef HAS_BT_CLASSIC
                Config.bluetooth.useBLE         = getParamIntSafe("bluetooth.useBLE", Config.bluetooth.useBLE) == 1;
            #else
                Config.bluetooth.useBLE         = true;    // fixed as BLE
            #endif
            Config.bluetooth.useKISS            = getParamIntSafe("bluetooth.useKISS", Config.bluetooth.useKISS) == 1;
            Config.bluetooth.controlPin         = constrain(getParamIntSafe("bluetooth.controlPin", Config.bluetooth.controlPin), 0, 999999);
        }

        // CW
        Config.cw.wpm = constrain(getParamIntSafe("cw.wpm", Config.cw.wpm), 5, 40);
        Config.cw.keyMode     = constrain(getParamIntSafe("cw.keyMode", Config.cw.keyMode), 0, 2);
        Config.cw.keyPin      = constrain(getParamIntSafe("cw.keyPin", Config.cw.keyPin), -1, 48);
        Config.cw.dahPin      = constrain(getParamIntSafe("cw.dahPin", Config.cw.dahPin), -1, 48);
        Config.cw.swapPaddles = request->hasParam("cw.swapPaddles", true);
        Config.cw.keypadPin   = constrain(getParamIntSafe("cw.keypadPin", Config.cw.keypadPin), -1, 48);
        for (int i = 0; i < CW_MESSAGE_COUNT; i++) Config.cw.keypadMv[i] = constrain(getParamIntSafe("cw.keypadMv" + String(i), Config.cw.keypadMv[i]), 0, 3300);
        Config.cw.keyTx       = request->hasParam("cw.keyTx", true);
        Config.cw.sidetone    = request->hasParam("cw.sidetone", true);
        Config.cw.trainerLevel  = constrain(getParamIntSafe("cw.trainerLevel", Config.cw.trainerLevel), 2, 41);
        Config.cw.trainerGapWpm = constrain(getParamIntSafe("cw.trainerGapWpm", Config.cw.trainerGapWpm), 0, 40);
        Config.cw.squelchDbm    = constrain(getParamIntSafe("cw.squelchDbm", Config.cw.squelchDbm), -130, 0);
        if (Config.cw.squelchDbm > -40) Config.cw.squelchDbm = 0;
        Config.wspr.freqHz      = 432301500;                // la frecuencia WSPR es fija (dial 432,300 MHz + 1500 Hz): la WebUI no la modifica
        Config.wspr.corrHz      = constrain(getParamIntSafe("wspr.corrHz", Config.wspr.corrHz), -5000, 5000);
        Config.wspr.useGlobalPpm = request->hasParam("wspr.useGlobalPpm", true);
        {   // receptor DAPNET (POCSAG): MHz en la WebUI, Hz en la configuracion
            if (request->hasParam("dapnet.freqMHz", true)) {
                const double mhz = request->getParam("dapnet.freqMHz", true)->value().toDouble();
                Config.dapnet.freqHz = constrain((int)lround(mhz * 1e6), 400000000, 480000000);
            }
            const int baud = getParamIntSafe("dapnet.baud", Config.dapnet.baud);
            Config.dapnet.baud = (baud == 512 || baud == 2400) ? baud : 1200;
            if (request->hasParam("dapnet.devKHz", true)) {
                const float dev = request->getParam("dapnet.devKHz", true)->value().toFloat();
                Config.dapnet.devTenthsKHz = constrain((int)lroundf(dev * 10.0f), 20, 80);
            }
            const int bw = getParamIntSafe("dapnet.rxBwTenthsKHz", Config.dapnet.rxBwTenthsKHz);
            Config.dapnet.rxBwTenthsKHz = (bw == 117 || bw == 195 || bw == 234 || bw == 293) ? bw : 146;
            if (request->hasParam("dapnet.rics", true)) {
                String rics = request->getParam("dapnet.rics", true)->value();
                rics.trim();
                if (rics.length() > 80) rics = rics.substring(0, 80);
                Config.dapnet.rics = rics;
            }
            Config.dapnet.monitorAll = request->hasParam("dapnet.monitorAll", true);
            Config.dapnet.timeSync   = request->hasParam("dapnet.timeSync", true);
            Config.dapnet.charset    = constrain(getParamIntSafe("dapnet.charset", Config.dapnet.charset), 0, 1);
        }
        #ifdef HAS_SA868
        if (request->hasParam("sa868.rxMHz", true)) {      // radio FM SA868 (LilyGO T-TWR): MHz en la WebUI, Hz en la configuracion
            const double rx = request->getParam("sa868.rxMHz", true)->value().toDouble();
            Config.sa868.rxFreqHz = constrain((int)lround(rx * 1e6), 134000000, 480000000);
            if (request->hasParam("sa868.txMHz", true)) {
                const double tx = request->getParam("sa868.txMHz", true)->value().toDouble();
                Config.sa868.txFreqHz = constrain((int)lround(tx * 1e6), 134000000, 480000000);
            }
            Config.sa868.wide      = getParamIntSafe("sa868.wide", Config.sa868.wide ? 1 : 0) != 0;
            Config.sa868.highPower = getParamIntSafe("sa868.highPower", Config.sa868.highPower ? 1 : 0) != 0;
            Config.sa868.volume    = constrain(getParamIntSafe("sa868.volume", Config.sa868.volume), 1, 8);
            Config.sa868.squelch   = constrain(getParamIntSafe("sa868.squelch", Config.sa868.squelch), 0, 8);
            Config.sa868.txLevel   = constrain(getParamIntSafe("sa868.txLevel", Config.sa868.txLevel), 1, 100);
            Config.sa868.txDelayMs = constrain(getParamIntSafe("sa868.txDelayMs", Config.sa868.txDelayMs), 50, 1000);
            Config.sa868.txTailMs  = constrain(getParamIntSafe("sa868.txTailMs", Config.sa868.txTailMs), 10, 300);
            const int r = getParamIntSafe("sa868.revision", Config.sa868.revision);
            Config.sa868.revision  = (r == 20 || r == 21) ? r : 0;
            Config.sa868.iaruRegion = constrain(getParamIntSafe("sa868.iaruRegion", Config.sa868.iaruRegion), 1, 3);
            Config.sa868.filters   = request->hasParam("sa868.filters", true);
            Config.sa868.speaker   = request->hasParam("sa868.speaker", true);
        }
        #endif
        {   // rotor (modo SAT)
            Config.rotor.enabled    = request->hasParam("rotor.enabled", true);
            Config.rotor.transport  = constrain(getParamIntSafe("rotor.transport", Config.rotor.transport), 0, 1);
            if (request->hasParam("rotor.host", true)) { String h = request->getParam("rotor.host", true)->value(); h.trim(); Config.rotor.host = h.length() > 63 ? h.substring(0, 63) : h; }
            Config.rotor.port       = constrain(getParamIntSafe("rotor.port", Config.rotor.port), 1, 65535);
            if (request->hasParam("rotor.btName", true)) { String n = request->getParam("rotor.btName", true)->value(); n.trim(); Config.rotor.btName = n.length() > 31 ? n.substring(0, 31) : n; }
            Config.rotor.stepTenths = constrain(getParamIntSafe("rotor.stepTenths", Config.rotor.stepTenths), 1, 100);
            Config.rotor.preSec     = constrain(getParamIntSafe("rotor.preSec", Config.rotor.preSec), 0, 600);
            Config.rotor.azOffset   = constrain(getParamIntSafe("rotor.azOffset", Config.rotor.azOffset), -180, 180);
            Config.rotor.elOffset   = constrain(getParamIntSafe("rotor.elOffset", Config.rotor.elOffset), -90, 90);
            Config.rotor.park       = request->hasParam("rotor.park", true);
            Config.rotor.parkAz     = constrain(getParamIntSafe("rotor.parkAz", Config.rotor.parkAz), 0, 359);
            Config.rotor.parkEl     = constrain(getParamIntSafe("rotor.parkEl", Config.rotor.parkEl), 0, 90);
            Config.rotor.bleEvents  = request->hasParam("rotor.bleEvents", true);
            Config.rotor.serverEnabled = request->hasParam("rotor.serverEnabled", true);
            Config.rotor.serverPort = constrain(getParamIntSafe("rotor.serverPort", Config.rotor.serverPort), 1, 65535);
            Config.rotor.tilt        = constrain(getParamIntSafe("rotor.tilt", Config.rotor.tilt), 0, 1);
            Config.rotor.mount       = constrain(getParamIntSafe("rotor.mount", Config.rotor.mount), 0, 1);
            {
                const int src = getParamIntSafe("rotor.source", Config.rotor.source);
                if (RotorFollow::validSource(src)) Config.rotor.source = src;
            }
            if (request->hasParam("rotor.followFilter", true)) {
                String f = request->getParam("rotor.followFilter", true)->value();
                f.trim(); f.toUpperCase();
                Config.rotor.followFilter = f.substring(0, 80);
            }
            Config.rotor.followMaxMin = constrain(getParamIntSafe("rotor.followMaxMin", Config.rotor.followMaxMin), 1, 720);
            Config.rotor.predict      = request->hasParam("rotor.predict", true);
            Config.rotor.predictMaxS  = constrain(getParamIntSafe("rotor.predictMaxS", Config.rotor.predictMaxS), 10, 1800);
            Config.rotor.targetIndex = constrain(getParamIntSafe("rotor.targetIndex", Config.rotor.targetIndex), 0, RotorConfig::TARGETS - 1);
            for (int i = 0; i < RotorConfig::TARGETS; i++) {
                char key[24];
                snprintf(key, sizeof(key), "rotor.t%d.n", i);
                if (!request->hasParam(key, true)) continue;                                   // fila ausente: se deja como esta
                String name = request->getParam(key, true)->value();
                name.trim();
                if (name.length() > 20) name = name.substring(0, 20);
                RotorConfig::Target& t = Config.rotor.targets[i];
                if (name.isEmpty()) { t = RotorConfig::Target{ "", 0.0, 0.0, 0, 0 }; continue; }
                t.name = name;
                snprintf(key, sizeof(key), "rotor.t%d.lat", i);
                t.lat = request->hasParam(key, true) ? constrain(request->getParam(key, true)->value().toDouble(), -90.0, 90.0) : 0.0;
                snprintf(key, sizeof(key), "rotor.t%d.lon", i);
                t.lon = request->hasParam(key, true) ? constrain(request->getParam(key, true)->value().toDouble(), -180.0, 180.0) : 0.0;
                snprintf(key, sizeof(key), "rotor.t%d.alt", i);
                t.alt = request->hasParam(key, true) ? constrain((int)request->getParam(key, true)->value().toInt(), -500, 9000) : 0;
                snprintf(key, sizeof(key), "rotor.t%d.f", i);
                t.freqHz = request->hasParam(key, true) ? (long)lround(request->getParam(key, true)->value().toDouble() * 1e6) : 0L;
            }
        }
        {   // modo SAT
            Config.sat.posSource = constrain(getParamIntSafe("sat.posSource", Config.sat.posSource), 0, 2);
            Config.sat.minEl = constrain(getParamIntSafe("sat.minEl", Config.sat.minEl), 0, 60);
            if (request->hasParam("sat.lat", true)) Config.sat.lat = constrain(request->getParam("sat.lat", true)->value().toDouble(), -90.0, 90.0);
            if (request->hasParam("sat.lon", true)) Config.sat.lon = constrain(request->getParam("sat.lon", true)->value().toDouble(), -180.0, 180.0);
            Config.sat.txEnabled = request->hasParam("sat.txEnabled", true);
            Config.sat.txMinEl = constrain(getParamIntSafe("sat.txMinEl", Config.sat.txMinEl), 0, 60);
            Config.sat.txPower = constrain(getParamIntSafe("sat.txPower", Config.sat.txPower), -9, 22);
            Config.sat.txMinIntervalS = constrain(getParamIntSafe("sat.txMinIntervalS", Config.sat.txMinIntervalS), 5, 3600);
            if (request->hasParam("sat.txText", true)) {
                String t = request->getParam("sat.txText", true)->value();
                t.trim();
                if (t.length() > 64) t = t.substring(0, 64);
                Config.sat.txText = t;
            }
            Config.sat.lnaPin = constrain(getParamIntSafe("sat.lnaPin", Config.sat.lnaPin), -1, 99);
            Config.sat.paPin  = constrain(getParamIntSafe("sat.paPin", Config.sat.paPin), -1, 99);
            Config.sat.pttPin = constrain(getParamIntSafe("sat.pttPin", Config.sat.pttPin), -1, 99);
            Config.sat.lnaRev = request->hasParam("sat.lnaRev", true);
            Config.sat.paRev  = request->hasParam("sat.paRev", true);
            Config.sat.pttRev = request->hasParam("sat.pttRev", true);
            Config.sat.seqLeadMs = constrain(getParamIntSafe("sat.seqLeadMs", Config.sat.seqLeadMs), 0, 2000);
            Config.sat.seqPreMs  = constrain(getParamIntSafe("sat.seqPreMs", Config.sat.seqPreMs), 0, 2000);
            Config.sat.seqPostMs = constrain(getParamIntSafe("sat.seqPostMs", Config.sat.seqPostMs), 0, 2000);
            Config.sat.seqTailMs = constrain(getParamIntSafe("sat.seqTailMs", Config.sat.seqTailMs), 0, 2000);
        }
        if (request->hasParam("radio.freqCorrPpm", true)) {                                  // ppm con hasta 3 decimales -> milesimas
            const double ppm = request->getParam("radio.freqCorrPpm", true)->value().toDouble();
            Config.radioPpm1000 = constrain((int)lround(ppm * 1000.0), -50000, 50000);
        }
        Config.wspr.powerDbm    = constrain(getParamIntSafe("wspr.powerDbm", Config.wspr.powerDbm), 0, 20);
        Config.wspr.intervalMin = constrain(getParamIntSafe("wspr.intervalMin", Config.wspr.intervalMin), 2, 60);
        {
            String loc = getParamStringSafe("wspr.locator", Config.wspr.locator);
            loc.trim();
            loc.toUpperCase();
            if (loc.length() != 4) loc = "";
            Config.wspr.locator = loc;
        }
        Config.wspr.dither      = request->hasParam("wspr.dither", true);
        Config.wspr.screenOff   = request->hasParam("wspr.screenOff", true);
        Config.wspr.driftTenths = constrain(getParamIntSafe("wspr.driftTenths", Config.wspr.driftTenths), -200, 200);
        Config.wspr.perPower    = request->hasParam("wspr.perPower", true);
        for (int i = 0; i < 7; i++) {
            Config.wspr.ppCorrHz[i]      = constrain(getParamIntSafe("wspr.ppCorrHz" + String(i), Config.wspr.ppCorrHz[i]), -5000, 5000);
            Config.wspr.ppDriftTenths[i] = constrain(getParamIntSafe("wspr.ppDriftTenths" + String(i), Config.wspr.ppDriftTenths[i]), -200, 200);
        }
        {   // balizas FT8 y FT4: frecuencias de dial en MHz en la WebUI, en Hz en la configuracion
            if (request->hasParam("ft.ft8FreqMHz", true)) {
                const double mhz = request->getParam("ft.ft8FreqMHz", true)->value().toDouble();
                if (mhz > 0.0) Config.ft.ft8FreqHz = constrain((int)lround(mhz * 1e6), 400000000, 480000000);
            }
            if (request->hasParam("ft.ft4FreqMHz", true)) {
                const double mhz = request->getParam("ft.ft4FreqMHz", true)->value().toDouble();
                Config.ft.ft4FreqHz = mhz > 0.0 ? constrain((int)lround(mhz * 1e6), 400000000, 480000000) : 0;      // vacio = sin definir
            }
            if (request->hasParam("ft.fstFreqMHz", true)) {
                const double mhz = request->getParam("ft.fstFreqMHz", true)->value().toDouble();
                Config.ft.fstFreqHz = mhz > 0.0 ? constrain((int)lround(mhz * 1e6), 400000000, 480000000) : 0;
            }
            Config.ft.fstRegion   = constrain(getParamIntSafe("ft.fstRegion", Config.ft.fstRegion), 1, 3);
            Config.ft.fstSeconds  = getParamIntSafe("ft.fstSeconds", Config.ft.fstSeconds) == 300 ? 300 : 120;
            Config.ft.fstEvery    = constrain(getParamIntSafe("ft.fstEvery", Config.ft.fstEvery), 2, 240);
            Config.ft.ft8Region   = constrain(getParamIntSafe("ft.ft8Region", Config.ft.ft8Region), 1, 3);
            Config.ft.ft4Region   = constrain(getParamIntSafe("ft.ft4Region", Config.ft.ft4Region), 1, 3);
            Config.ft.corrHz      = constrain(getParamIntSafe("ft.corrHz", Config.ft.corrHz), -5000, 5000);
            Config.ft.powerDbm    = constrain(getParamIntSafe("ft.powerDbm", Config.ft.powerDbm), 0, 22);
            Config.ft.everyCycles = constrain(getParamIntSafe("ft.everyCycles", Config.ft.everyCycles), 2, 240);
            String ftLoc = getParamStringSafe("ft.locator", Config.ft.locator);
            ftLoc.trim();
            ftLoc.toUpperCase();
            if (ftLoc.length() != 4) ftLoc = "";
            Config.ft.locator = ftLoc;
            String ftText = getParamStringSafe("ft.text", Config.ft.text);
            ftText.trim();
            ftText.toUpperCase();
            if (ftText.length() > 35) ftText = ftText.substring(0, 35);
            Config.ft.text = ftText;
            Config.ft.screenOff = request->hasParam("ft.screenOff", true);
        }
        // GPS (u-blox M10 / L76K): se aplica al arrancar. Las combinaciones no validas en el M10 se rechazan y se conserva la mascara anterior
        if (request->hasParam("gps.present", true)) {
            int mask = (request->hasParam("gps.useGps", true) ? 1 : 0) | (request->hasParam("gps.useGalileo", true) ? 2 : 0) | (request->hasParam("gps.useBeidou", true) ? 4 : 0) |
                       (request->hasParam("gps.useGlonass", true) ? 8 : 0) | (request->hasParam("gps.useQzss", true) ? 16 : 0) | (request->hasParam("gps.useSbas", true) ? 32 : 0);
            if (GPS_Utils::gpsIsCasic()) {                       // L76K: solo GPS / BeiDou / GLONASS, en cualquier combinacion; mascara propia (no pisa la del M10)
                if (mask & 13) Config.gps.cnMask = mask & 13;
                Config.gps.nmeaLean = request->hasParam("gps.nmeaLean", true);
            } else {
                const bool okMask = !(((mask & 16) || (mask & 32)) && !(mask & 1)) && !((mask & 4) && (mask & 8)) && (mask & 15) != 0;
                if (okMask) Config.gps.gnssMask = mask;
            }
            Config.gps.assistNow       = request->hasParam("gps.assistNow", true);
            int dyn = getParamIntSafe("gps.dynModel", Config.gps.dynModel);
            if (dyn == 0 || (dyn >= 2 && dyn <= 9)) Config.gps.dynModel = dyn;
            Config.gps.rateMs          = constrain(getParamIntSafe("gps.rateMs", Config.gps.rateMs), GPS_Utils::gpsIsCasic() ? 200 : 500, 10000);   // el L76K llega a 5 Hz
            Config.gps.minElev         = constrain(getParamIntSafe("gps.minElev", Config.gps.minElev), 0, 30);
            Config.gps.staticHoldCms   = constrain(getParamIntSafe("gps.staticHoldCms", Config.gps.staticHoldCms), 0, 255);
            Config.gps.staticHoldDistM = constrain(getParamIntSafe("gps.staticHoldDistM", Config.gps.staticHoldDistM), 0, 1000);
            Config.gps.sbasDiffCorr    = request->hasParam("gps.sbasDiffCorr", true);
            Config.gps.sbasRanging     = request->hasParam("gps.sbasRanging", true);
            Config.gps.sbasIntegrity   = request->hasParam("gps.sbasIntegrity", true);
            Config.gps.ppsEnabled      = request->hasParam("gps.ppsEnabled", true);
            Config.gps.ppsFreqHz       = constrain(getParamIntSafe("gps.ppsFreqHz", Config.gps.ppsFreqHz), 1, 10);
            Config.gps.ppsLenMs        = constrain(getParamIntSafe("gps.ppsLenMs", Config.gps.ppsLenMs), 1, 500);
            Config.gps.ppsRising       = getParamIntSafe("gps.ppsRising", Config.gps.ppsRising ? 1 : 0) != 0;
            Config.gps.ppsCableDelayNs = constrain(getParamIntSafe("gps.ppsCableDelayNs", Config.gps.ppsCableDelayNs), -32000, 32000);
            Config.gps.ppsUserDelayNs  = constrain(getParamIntSafe("gps.ppsUserDelayNs", Config.gps.ppsUserDelayNs), -1000000, 1000000);
        }
        if (request->hasParam("lrfhss.freqHz", true)) {         // los ajustes LR-FHSS solo se modifican si la WebUI los envia (grupo reservado: no los envia)
            Config.lrfhss.enabled     = request->hasParam("lrfhss.enabled", true);
            Config.lrfhss.freqHz      = constrain(getParamIntSafe("lrfhss.freqHz", Config.lrfhss.freqHz), 400000000, 480000000);
            Config.lrfhss.bwIdx       = constrain(getParamIntSafe("lrfhss.bwIdx", Config.lrfhss.bwIdx), 0, 9);
            Config.lrfhss.crIdx       = constrain(getParamIntSafe("lrfhss.crIdx", Config.lrfhss.crIdx), 0, 3);
            Config.lrfhss.narrowGrid  = request->hasParam("lrfhss.narrowGrid", true);
            Config.lrfhss.powerDbm    = constrain(getParamIntSafe("lrfhss.powerDbm", Config.lrfhss.powerDbm), -9, 22);
            Config.lrfhss.intervalMin = constrain(getParamIntSafe("lrfhss.intervalMin", Config.lrfhss.intervalMin), 2, 60);
            {
                String loc = getParamStringSafe("lrfhss.locator", Config.lrfhss.locator);
                loc.trim();
                loc.toUpperCase();
                if (loc.length() != 4 && loc.length() != 6 && loc.length() != 8) loc = "";
                Config.lrfhss.locator = loc;
            }
            {
                const int len = getParamIntSafe("lrfhss.locatorLen", Config.lrfhss.locatorLen);
                Config.lrfhss.locatorLen = (len == 4 || len == 8) ? len : 6;
            }
        }
        Config.cw.beaconMin = constrain(getParamIntSafe("cw.beaconMin", Config.cw.beaconMin), 0, 240);
        Config.cw.beaconFreq   = constrain((long)getParamDoubleSafe("cw.beaconFreq", Config.cw.beaconFreq), 137000000L, 1020000000L);
        Config.cw.beaconPower  = LoRa_Utils::validPower(getParamIntSafe("cw.beaconPower", Config.cw.beaconPower));
        Config.cw.beaconRegion = constrain(getParamIntSafe("cw.beaconRegion", Config.cw.beaconRegion), 1, 3);
        Config.cw.keyFreq      = constrain((long)getParamDoubleSafe("cw.keyFreq", Config.cw.keyFreq), 137000000L, 1020000000L);
        Config.cw.keyPower     = LoRa_Utils::validPower(getParamIntSafe("cw.keyPower", Config.cw.keyPower));
        Config.cw.keyRegion    = constrain(getParamIntSafe("cw.keyRegion", Config.cw.keyRegion), 1, 3);
        {
            String text = getParamStringSafe("cw.beaconText", Config.cw.beaconText);
            text.trim();
            if (text.length() > 60) text = text.substring(0, 60);
            Config.cw.beaconText = text;
        }
        for (int i = 0; i < CW_MESSAGE_COUNT; i++) {
            String text = getParamStringSafe("cw.message." + String(i), Config.cw.messages[i]);
            text.trim();
            if (text.length() > 60) text = text.substring(0, 60);
            Config.cw.messages[i] = text;
        }

        // LORA
        for (int i = 0; i < (int) Config.loraTypes.size(); i++) {
            Config.loraTypes[i].frequency       = getParamDoubleSafe("lora." + String(i) + ".frequency", Config.loraTypes[i].frequency);
            Config.loraTypes[i].spreadingFactor = getParamIntSafe("lora." + String(i) + ".spreadingFactor", Config.loraTypes[i].spreadingFactor);
            Config.loraTypes[i].codingRate4     = getParamIntSafe("lora." + String(i) + ".codingRate4", Config.loraTypes[i].codingRate4);
            Config.loraTypes[i].signalBandwidth = getParamIntSafe("lora." + String(i) + ".signalBandwidth", Config.loraTypes[i].signalBandwidth);
            if (!Config.loraTypes[i].ais) {
                String lp = "lora." + String(i);
                Config.loraTypes[i].fsk            = getParamIntSafe(lp + ".fsk", Config.loraTypes[i].fsk ? 1 : 0) == 1;
                Config.loraTypes[i].fskBitRate     = constrain(getParamFloatSafe(lp + ".fskBitRate", Config.loraTypes[i].fskBitRate), 1.2f, 100.0f);
                Config.loraTypes[i].fskFreqDev     = constrain(getParamFloatSafe(lp + ".fskFreqDev", Config.loraTypes[i].fskFreqDev), 0.6f, 200.0f);
                Config.loraTypes[i].fskRxBandwidth = getParamFloatSafe(lp + ".fskRxBandwidth", Config.loraTypes[i].fskRxBandwidth);
                Config.loraTypes[i].fskFrequency   = constrain((long)getParamDoubleSafe(lp + ".fskFrequency", Config.loraTypes[i].fskFrequency), 137000000L, 1020000000L);
                Config.loraTypes[i].fskPower       = LoRa_Utils::validPower(getParamIntSafe(lp + ".fskPower", Config.loraTypes[i].fskPower));
                Config.loraTypes[i].fskIaruRegion  = constrain(getParamIntSafe(lp + ".fskIaruRegion", Config.loraTypes[i].fskIaruRegion), 1, 3);
                Config.loraTypes[i].fskShaping     = constrain(getParamIntSafe(lp + ".fskShaping", Config.loraTypes[i].fskShaping), 0, 4);
                Config.loraTypes[i].fskFec         = constrain(getParamIntSafe(lp + ".fskFec", Config.loraTypes[i].fskFec), 0, 4);
            }
            Config.loraTypes[i].iaruRegion      = constrain(getParamIntSafe("lora." + String(i) + ".iaruRegion", Config.loraTypes[i].iaruRegion), 1, 3);
            Config.loraTypes[i].power           = LoRa_Utils::validPower(getParamIntSafe("lora." + String(i) + ".power", Config.loraTypes[i].power));  // saved as the power the radio chip really uses
        }
        {
            int aisIndex = Config.aisLoraIndex();
            if (aisIndex >= 0) {                        // el perfil LoRa del modo nautico AIS es siempre SF9 y CR 4/5
                Config.loraTypes[aisIndex].spreadingFactor = 9;
                Config.loraTypes[aisIndex].codingRate4     = 5;
            }
        }

        //  Battery
        Config.battery.sendVoltage              = request->hasParam("battery.sendVoltage", true);
        if (Config.battery.sendVoltage) {
            Config.battery.voltageAsTelemetry   = request->hasParam("battery.voltageAsTelemetry", true);
            Config.battery.sendVoltageAlways    = request->hasParam("battery.sendVoltageAlways", true);
        }
        Config.battery.monitorVoltage           = request->hasParam("battery.monitorVoltage", true);
        if (Config.battery.monitorVoltage) Config.battery.sleepVoltage = getParamFloatSafe("battery.sleepVoltage", Config.battery.sleepVoltage);

        //  Telemetry
        Config.telemetry.active                 = request->hasParam("telemetry.active", true);
        if (Config.telemetry.active) {
            Config.telemetry.sendTelemetry          = request->hasParam("telemetry.sendTelemetry", true);
            Config.telemetry.temperatureCorrection  = getParamFloatSafe("telemetry.temperatureCorrection", Config.telemetry.temperatureCorrection);
        }

        //  Winlink
        Config.winlink.password                 = getParamStringSafe("winlink.password", Config.winlink.password);

        //  WiFi AP
        Config.wifiAP.password                  = getParamStringSafe("wifiAP.password", Config.wifiAP.password);
        Config.wifiAP.active                    = false; // when Configuration is finished Tracker returns to normal mode.

        //  Firmware update (5.3.1)
        Config.fwUpdate.ssid                    = getParamStringSafe("fwUpdate.ssid", Config.fwUpdate.ssid);
        Config.fwUpdate.password                = getParamStringSafe("fwUpdate.password", Config.fwUpdate.password);

        //  Group watch and remote emergency
        Config.kissNet.enabled                  = request->hasParam("kissNet.enabled", true);          // con el interruptor apagado los campos van desactivados (no se envian): se conservan los guardados
        Config.kissNet.tcpPort                  = constrain(getParamIntSafe("kissNet.tcpPort", Config.kissNet.tcpPort), 1, 65535);
        Config.kissNet.udpPort                  = constrain(getParamIntSafe("kissNet.udpPort", Config.kissNet.udpPort), 0, 65535);
        Config.kissNet.allowed                  = getParamStringSafe("kissNet.allowed", Config.kissNet.allowed);
        Config.kissNet.allowed.trim();
        Config.nmeaUart.txPin                   = constrain(getParamIntSafe("nmeaUart.txPin", Config.nmeaUart.txPin), -1, 48);
        { const int b = getParamIntSafe("nmeaUart.baud", Config.nmeaUart.baud); Config.nmeaUart.baud = (b == 4800 || b == 9600 || b == 38400) ? b : 38400; }
        Config.iaruAuto.mode                    = constrain(getParamIntSafe("iaruAuto.mode", Config.iaruAuto.mode), 0, 2);
        if (Config.iaruAuto.mode != 0) Config.iaruAuto.confirm = request->hasParam("iaruAuto.confirm", true);     // con el modo apagado el campo no se envia: se conserva
        Config.cat.enabled                      = request->hasParam("cat.enabled", true);
        Config.cat.usb                          = request->hasParam("cat.usb", true);
        Config.cat.port                         = constrain(getParamIntSafe("cat.port", Config.cat.port), 1, 65535);
        Config.cat.allowed                      = getParamStringSafe("cat.allowed", Config.cat.allowed);
        Config.cat.allowed.trim();
        if (Config.cat.enabled || Config.cat.usb) Config.cat.pttBeacon = request->hasParam("cat.pttBeacon", true);       // con el interruptor apagado los campos no se envian: se conservan
        Config.cat.tuneKHz                      = constrain(getParamIntSafe("cat.tuneKHz", Config.cat.tuneKHz), 0, 500);
        Config.team.watchActive                 = request->hasParam("team.watchActive", true);
        Config.team.watchCalls                  = getParamStringSafe("team.watchCalls", Config.team.watchCalls);
        Config.team.timeoutMin                  = constrain(getParamIntSafe("team.timeoutMin", Config.team.timeoutMin), 1, 120);
        Config.team.maxKm                       = constrain(getParamIntSafe("team.maxKm", Config.team.maxKm), 0, 500);
        Config.team.pollSec                     = getParamIntSafe("team.pollSec", Config.team.pollSec);
        if (Config.team.pollSec != 0) Config.team.pollSec = constrain(Config.team.pollSec, 60, 3600);
        Config.team.eaiActive                   = request->hasParam("team.eaiActive", true);
        Config.team.eaiCalls                    = getParamStringSafe("team.eaiCalls", Config.team.eaiCalls);
        Config.team.eaiCode                     = getParamStringSafe("team.eaiCode", Config.team.eaiCode);
        Config.team.eaiMinutes                  = constrain(getParamIntSafe("team.eaiMinutes", Config.team.eaiMinutes), 1, 50);

        //  Notification
        Config.notification.ledTx               = request->hasParam("notification.ledTx", true);
        if (Config.notification.ledTx) Config.notification.ledTxPin = getParamIntSafe("notification.ledTxPin", Config.notification.ledTxPin);
        Config.notification.ledMessage          = request->hasParam("notification.ledMessage", true);
        Config.notification.ledRxBeacon         = request->hasParam("notification.ledRxBeacon", true);
        Config.notification.flashEmergency      = request->hasParam("notification.flashEmergency", true);
        Config.notification.ledTxCw             = request->hasParam("notification.ledTxCw", true);
        Config.notification.ledRxCw             = request->hasParam("notification.ledRxCw", true);
        Config.notification.flashCw             = request->hasParam("notification.flashCw", true);
        if (Config.notification.ledMessage || Config.notification.ledRxBeacon || Config.notification.ledRxCw) Config.notification.ledMessagePin = getParamIntSafe("notification.ledMessagePin", Config.notification.ledMessagePin);
        Config.notification.buzzerActive        = request->hasParam("notification.buzzerActive", true);
        if (Config.notification.buzzerActive) {
            Config.notification.buzzerPinTone   = getParamIntSafe("notification.buzzerPinTone", Config.notification.buzzerPinTone);
            Config.notification.buzzerPinVcc    = getParamIntSafe("notification.buzzerPinVcc", Config.notification.buzzerPinVcc);
            Config.notification.bootUpBeep      = request->hasParam("notification.bootUpBeep", true);
            Config.notification.txBeep          = request->hasParam("notification.txBeep", true);
            Config.notification.messageRxBeep   = request->hasParam("notification.messageRxBeep", true);
            Config.notification.stationBeep     = request->hasParam("notification.stationBeep", true);
            Config.notification.lowBatteryBeep  = request->hasParam("notification.lowBatteryBeep", true);
            Config.notification.shutDownBeep    = request->hasParam("notification.shutDownBeep", true);
            Config.notification.bellRings       = request->hasParam("notification.bellRings", true);
        }
        Config.notification.ledFlashlight           = request->hasParam("notification.ledFlashlight", true);
        if (Config.notification.ledFlashlight) Config.notification.ledFlashlightPin = getParamIntSafe("notification.ledFlashlightPin", Config.notification.ledFlashlightPin);

        //  PTT Trigger
        Config.ptt.active                       = request->hasParam("ptt.active", true);
        if (Config.ptt.active) {
            Config.ptt.reverse                  = request->hasParam("ptt.reverse", true);
            Config.ptt.io_pin                   = getParamIntSafe("ptt.io_pin", Config.ptt.io_pin);
            Config.ptt.preDelay                 = getParamIntSafe("ptt.preDelay", Config.ptt.preDelay);
            Config.ptt.postDelay                = getParamIntSafe("ptt.postDelay", Config.ptt.postDelay);
        }

        bool saveSuccess = Config.writeFile();

        if (saveSuccess) {
            Serial.println("Configuration saved successfully");
            AsyncWebServerResponse *response = request->beginResponse(302, "text/html", "");
            response->addHeader("Location", "/?success=1");
            request->send(response);

            displayToggle(false);
            delay(500);
            ESP.restart();
        } else {
            Serial.println("Error saving configuration!");
            String errorPage = "<!DOCTYPE html><html><head><title>Error</title></head><body>";
            errorPage += "<h1>Configuration Error:</h1>";
            errorPage += "<p>Couldn't save new configuration. Please try again.</p>";
            errorPage += "<a href='/'>Back</a></body></html>";

            AsyncWebServerResponse *response = request->beginResponse(500, "text/html", errorPage);
            request->send(response);
        }
    }

    void handleAction(AsyncWebServerRequest *request) {
        String type = request->getParam("type", false)->value();

        if (type == "send-beacon") {
            //lastBeaconTx = 0;

            request->send(200, "text/plain", "Beacon will be sent in a while");
        } else if (type == "iaru-detect") {                  // region IARU segun el GPS, bajo peticion: lo evalua el bucle principal
            IARU_Auto::request();
            AsyncWebServerResponse *r = request->beginResponse(200, "application/json", IARU_Auto::json());
            r->addHeader("Cache-Control", "no-store");
            request->send(r);
        } else if (type == "reboot") {
            displayToggle(false);
            ESP.restart();
        } else {
            request->send(404, "text/plain", "Not Found");
        }
    }

    void handleStyle(AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(200, "text/css", (const uint8_t*)web_style_css, web_style_css_len);
        response->addHeader("Content-Encoding", "gzip");
        request->send(response);
    }

    void handleScript(AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(200, "text/javascript", (const uint8_t*)web_script_js, web_script_js_len);
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", "no-cache");       // tras actualizar el firmware el navegador no debe reutilizar un script antiguo
        request->send(response);
    }

    void handleBootstrapStyle(AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(200, "text/css", (const uint8_t*)web_bootstrap_css, web_bootstrap_css_len);
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", "max-age=3600");
        request->send(response);
    }

    void handleBootstrapScript(AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(200, "text/javascript", (const uint8_t*)web_bootstrap_js, web_bootstrap_js_len);
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", "max-age=3600");
        request->send(response);
    }

    void setup() {
        server.on("/", HTTP_GET, handleHome);
        server.on("/status", HTTP_GET, handleStatus);
        server.on("/version", HTTP_GET, handleVersion);
        server.on("/gpsinfo", HTTP_GET, handleGpsInfo);
        server.on("/api/keypad", HTTP_GET, handleKeypad);
        //server.on("/received-packets.json", HTTP_GET, handleReceivedPackets);
        server.on("/configuration.json", HTTP_GET, handleReadConfiguration);
        server.on("/configuration.json", HTTP_POST, handleWriteConfiguration);
        server.on("/action", HTTP_POST, handleAction);
        server.on("/style.css", HTTP_GET, handleStyle);
        server.on("/script.js", HTTP_GET, handleScript);
        server.on("/bootstrap.css", HTTP_GET, handleBootstrapStyle);
        server.on("/bootstrap.js", HTTP_GET, handleBootstrapScript);
        server.on("/favicon.png", HTTP_GET, handleFavicon);

        server.on("/api/pins", HTTP_GET, [](AsyncWebServerRequest *request) {      // GPIO ocupados por la placa (WebUI: guia de pines)
            AsyncWebServerResponse *r = request->beginResponse(200, "application/json", BOARD_PINS::json());
            r->addHeader("Cache-Control", "no-store");
            request->send(r);
        });
        server.on("/api/iaru", HTTP_GET, [](AsyncWebServerRequest *request) {      // region IARU automatica: estado (modo, fix GPS, region detectada)
            AsyncWebServerResponse *r = request->beginResponse(200, "application/json", IARU_Auto::json());
            r->addHeader("Cache-Control", "no-store");
            request->send(r);
        });
        FW_Update::registerRoutes(server);

        server.onNotFound(handleNotFound);

        server.begin();
    }

}