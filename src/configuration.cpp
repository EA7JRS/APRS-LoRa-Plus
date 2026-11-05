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
#include <SPIFFS.h>
#include "configuration.h"
#include "board_pins.h"
#include "power_utils.h"
#include "board_pinout.h"
#include "display.h"
#include "ais_vessel.h"
#include "logger.h"

#ifdef NMEA_UART_TX_PIN
    #define NMEA_UART_TX_DEFAULT    NMEA_UART_TX_PIN        // el board_pinout.h de la variante (-1 = apagado)
#else
    #define NMEA_UART_TX_DEFAULT    -1
#endif
#ifdef NMEA_UART_BAUD
    #define NMEA_UART_BAUD_DEFAULT  NMEA_UART_BAUD
#else
    #define NMEA_UART_BAUD_DEFAULT  38400
#endif

// Pines por defecto de LEDs, flash y buzzer (todo apagado por defecto). En la T-Beam S3 Supreme los de otras placas chocan con la radio, el IMU o el RTC:
// se usan pines libres de su cabecera de expansion (38 LED Tx, 21 LED mensaje, 45 flash, 39 tono y 48 Vcc del buzzer; el 2 (ADC1) queda para el teclado FH-2, 44 / 46 para el manipulador y 43 para la salida NMEA; esquema en docs/HARDWARE_SUPREME_EXTRAS.md)
#ifdef TTGO_T_Beam_S3_SUPREME_V3
    #define NOTIF_DEF_LED_TX        38
    #define NOTIF_DEF_LED_MESSAGE   21
    #define NOTIF_DEF_FLASHLIGHT    45
    #define NOTIF_DEF_BUZZER_TONE   39
    #define NOTIF_DEF_BUZZER_VCC    48
#else
    #define NOTIF_DEF_LED_TX        13
    #define NOTIF_DEF_LED_MESSAGE   2
    #define NOTIF_DEF_FLASHLIGHT    14
    #define NOTIF_DEF_BUZZER_TONE   33
    #define NOTIF_DEF_BUZZER_VCC    25
#endif

// Pin por defecto de LEDs, flash y buzzer: -1 (sin asignar) si lo ocupa la propia placa o no sirve para una salida
static int defaultPin(int pin) {
    return (POWER_Utils::pinReserved(pin) || BOARD_PINS::inputOnly(pin)) ? -1 : pin;
}

extern logging::Logger logger;

bool Configuration::writeFile() {

    Serial.println("Saving config..");

    JsonDocument data;
    // Se escribe en un fichero temporal y solo al terminar se sustituye el definitivo: un corte o fallo a mitad de guardado
    // ya no deja la configuracion vacia (causa de perder todos los perfiles).
    File configFile = SPIFFS.open("/tracker_conf.tmp", "w");

    if (!configFile) {
        Serial.println("Error: Could not open config file for writing");
        return false;
    }
    try {

        data["wifiAP"]["active"]                    = wifiAP.active;
        data["wifiAP"]["password"]                  = wifiAP.password;

        data["fwUpdate"]["ssid"]                    = fwUpdate.ssid;
        data["fwUpdate"]["password"]                = fwUpdate.password;
        data["fwUpdate"]["autoCheck"]               = fwUpdate.autoCheck;
        data["fwUpdate"]["autoInstall"]             = fwUpdate.autoInstall;
        data["kissNet"]["enabled"]                  = kissNet.enabled;
        data["kissNet"]["tcpPort"]                  = kissNet.tcpPort;
        data["kissNet"]["udpPort"]                  = kissNet.udpPort;
        data["kissNet"]["allowed"]                  = kissNet.allowed;
        data["nmeaUart"]["txPin"]                   = nmeaUart.txPin;
        data["nmeaUart"]["baud"]                    = nmeaUart.baud;
        data["cat"]["enabled"]                      = cat.enabled;
        data["cat"]["usb"]                          = cat.usb;
        data["cat"]["port"]                         = cat.port;
        data["cat"]["allowed"]                      = cat.allowed;
        data["cat"]["pttBeacon"]                    = cat.pttBeacon;
        data["cat"]["tuneKHz"]                      = cat.tuneKHz;
        data["team"]["watchActive"]                 = team.watchActive;
        data["team"]["watchCalls"]                  = team.watchCalls;
        data["team"]["timeoutMin"]                  = team.timeoutMin;
        data["team"]["maxKm"]                       = team.maxKm;
        data["team"]["pollSec"]                     = team.pollSec;
        data["team"]["eaiActive"]                   = team.eaiActive;
        data["team"]["eaiCalls"]                    = team.eaiCalls;
        data["team"]["eaiCode"]                     = team.eaiCode;
        data["team"]["eaiMinutes"]                  = team.eaiMinutes;

        for (int i = 0; i < beacons.size(); i++) {
            beacons[i].callsign.trim();
            beacons[i].callsign.toUpperCase();
            data["beacons"][i]["callsign"]              = beacons[i].callsign;
            data["beacons"][i]["symbol"]                = beacons[i].symbol;
            data["beacons"][i]["overlay"]               = beacons[i].overlay;
            data["beacons"][i]["micE"]                  = beacons[i].micE;
            data["beacons"][i]["comment"]               = beacons[i].comment;
            data["beacons"][i]["smartBeaconActive"]     = beacons[i].smartBeaconActive;
            data["beacons"][i]["smartBeaconSetting"]    = beacons[i].smartBeaconSetting;
            data["beacons"][i]["gpsEcoMode"]            = beacons[i].gpsEcoMode;
            data["beacons"][i]["profileLabel"]          = beacons[i].profileLabel;
            data["beacons"][i]["status"]                = beacons[i].status;
            data["beacons"][i]["aisProfile"]            = beacons[i].aisProfile;
            data["beacons"][i]["aisVesselType"]         = beacons[i].aisVesselType;
            data["beacons"][i]["aisName"]               = beacons[i].aisName;
            data["beacons"][i]["aisMmsi"]               = beacons[i].aisMmsi;
            data["beacons"][i]["aisLength"]             = beacons[i].aisLength;
            data["beacons"][i]["aisBeam"]               = beacons[i].aisBeam;
            data["beacons"][i]["aisDraft"]              = beacons[i].aisDraft;
        }

        data["display"]["ecoMode"]                  = display.ecoMode;
        data["display"]["timeout"]                  = display.timeout;
        data["display"]["turn180"]                  = display.turn180;
        data["display"]["showSymbol"]               = display.showSymbol;

        data["bluetooth"]["active"]                 = bluetooth.active;
        data["bluetooth"]["deviceName"]             = bluetooth.deviceName;
        #ifdef HAS_BT_CLASSIC
            data["bluetooth"]["useBLE"]             = bluetooth.useBLE;
        #else
            data["bluetooth"]["useBLE"]             = true; // fixed as BLE
        #endif
        data["bluetooth"]["useKISS"]                = bluetooth.useKISS;

        data["operatingMode"]                       = savedMode;
        data["radioPpm1000"]                        = radioPpm1000;
        data["cw"]["wpm"]                           = cw.wpm;
        data["cw"]["keyMode"]                       = cw.keyMode;
        data["cw"]["keyPin"]                        = cw.keyPin;
        data["cw"]["dahPin"]                        = cw.dahPin;
        data["cw"]["swapPaddles"]                   = cw.swapPaddles;
        data["cw"]["keypadPin"]                     = cw.keypadPin;
        for (int i = 0; i < CW_MESSAGE_COUNT; i++) data["cw"]["keypadMv"][i] = cw.keypadMv[i];
        data["cw"]["keyTx"]                         = cw.keyTx;
        data["cw"]["sidetone"]                      = cw.sidetone;
        data["cw"]["trainerLevel"]                  = cw.trainerLevel;
        data["cw"]["trainerGapWpm"]                 = cw.trainerGapWpm;
        data["gps"]["gnssMask"]                     = gps.gnssMask;
        data["gps"]["assistNow"]                    = gps.assistNow;
        data["gps"]["dynModel"]                     = gps.dynModel;
        data["gps"]["rateMs"]                       = gps.rateMs;
        data["gps"]["minElev"]                      = gps.minElev;
        data["gps"]["staticHoldCms"]                = gps.staticHoldCms;
        data["gps"]["staticHoldDistM"]              = gps.staticHoldDistM;
        data["gps"]["sbasDiffCorr"]                 = gps.sbasDiffCorr;
        data["gps"]["sbasRanging"]                  = gps.sbasRanging;
        data["gps"]["sbasIntegrity"]                = gps.sbasIntegrity;
        data["gps"]["ppsEnabled"]                   = gps.ppsEnabled;
        data["gps"]["ppsFreqHz"]                    = gps.ppsFreqHz;
        data["gps"]["ppsLenMs"]                     = gps.ppsLenMs;
        data["gps"]["ppsRising"]                    = gps.ppsRising;
        data["gps"]["ppsCableDelayNs"]              = gps.ppsCableDelayNs;
        data["gps"]["ppsUserDelayNs"]               = gps.ppsUserDelayNs;
        data["gps"]["cnMask"]                       = gps.cnMask;
        data["gps"]["nmeaLean"]                     = gps.nmeaLean;
        data["wspr"]["freqHz"]                      = wspr.freqHz;
        data["wspr"]["corrHz"]                      = wspr.corrHz;
        data["wspr"]["useGlobalPpm"]                = wspr.useGlobalPpm;
        data["wspr"]["powerDbm"]                    = wspr.powerDbm;
        data["wspr"]["intervalMin"]                 = wspr.intervalMin;
        data["wspr"]["locator"]                     = wspr.locator;
        data["wspr"]["dither"]                      = wspr.dither;
        data["wspr"]["screenOff"]                   = wspr.screenOff;
        data["wspr"]["driftTenths"]                 = wspr.driftTenths;
        data["wspr"]["perPower"]                    = wspr.perPower;
        data["dapnet"]["freqHz"]                    = dapnet.freqHz;
        data["dapnet"]["baud"]                      = dapnet.baud;
        data["dapnet"]["devTenthsKHz"]              = dapnet.devTenthsKHz;
        data["dapnet"]["rxBwTenthsKHz"]             = dapnet.rxBwTenthsKHz;
        data["dapnet"]["rics"]                      = dapnet.rics;
        data["dapnet"]["monitorAll"]                = dapnet.monitorAll;
        data["dapnet"]["timeSync"]                  = dapnet.timeSync;
        data["dapnet"]["charset"]                   = dapnet.charset;
        data["ft"]["ft8FreqHz"]                     = ft.ft8FreqHz;
        data["ft"]["ft4FreqHz"]                     = ft.ft4FreqHz;
        data["ft"]["ft8Region"]                     = ft.ft8Region;
        data["ft"]["ft4Region"]                     = ft.ft4Region;
        data["ft"]["fstFreqHz"]                     = ft.fstFreqHz;
        data["ft"]["fstRegion"]                     = ft.fstRegion;
        data["ft"]["fstSeconds"]                    = ft.fstSeconds;
        data["ft"]["fstEvery"]                      = ft.fstEvery;
        data["ft"]["corrHz"]                        = ft.corrHz;
        data["ft"]["powerDbm"]                      = ft.powerDbm;
        data["ft"]["everyCycles"]                   = ft.everyCycles;
        data["ft"]["locator"]                       = ft.locator;
        data["ft"]["text"]                          = ft.text;
        data["ft"]["screenOff"]                     = ft.screenOff;
        data["lrfhss"]["enabled"]                   = lrfhss.enabled;
        data["lrfhss"]["freqHz"]                    = lrfhss.freqHz;
        data["lrfhss"]["bwIdx"]                     = lrfhss.bwIdx;
        data["lrfhss"]["crIdx"]                     = lrfhss.crIdx;
        data["lrfhss"]["narrowGrid"]                = lrfhss.narrowGrid;
        data["lrfhss"]["powerDbm"]                  = lrfhss.powerDbm;
        data["lrfhss"]["intervalMin"]               = lrfhss.intervalMin;
        data["lrfhss"]["locator"]                   = lrfhss.locator;
        data["lrfhss"]["locatorLen"]                = lrfhss.locatorLen;
        for (int i = 0; i < 7; i++) {
            data["wspr"]["ppCorrHz"][i]             = wspr.ppCorrHz[i];
            data["wspr"]["ppDriftTenths"][i]        = wspr.ppDriftTenths[i];
        }
        data["cw"]["beaconMin"]                     = cw.beaconMin;
        data["cw"]["beaconFreq"]                    = cw.beaconFreq;
        data["cw"]["beaconPower"]                   = cw.beaconPower;
        data["cw"]["beaconRegion"]                  = cw.beaconRegion;
        data["cw"]["keyFreq"]                       = cw.keyFreq;
        data["cw"]["keyPower"]                      = cw.keyPower;
        data["cw"]["keyRegion"]                     = cw.keyRegion;
        data["cw"]["beaconText"]                    = cw.beaconText;
        for (int i = 0; i < CW_MESSAGE_COUNT; i++) data["cw"]["messages"][i] = cw.messages[i];

        for (int i = 0; i < loraTypes.size(); i++) {
            data["lora"][i]["frequency"]                = loraTypes[i].frequency;
            data["lora"][i]["spreadingFactor"]          = loraTypes[i].spreadingFactor;
            data["lora"][i]["signalBandwidth"]          = loraTypes[i].signalBandwidth;
            data["lora"][i]["codingRate4"]              = loraTypes[i].codingRate4;
            data["lora"][i]["power"]                    = loraTypes[i].power;
            data["lora"][i]["ais"]                      = loraTypes[i].ais;
            data["lora"][i]["fsk"]                      = loraTypes[i].fsk;
            data["lora"][i]["fskBitRate"]               = loraTypes[i].fskBitRate;
            data["lora"][i]["fskFreqDev"]               = loraTypes[i].fskFreqDev;
            data["lora"][i]["fskRxBandwidth"]           = loraTypes[i].fskRxBandwidth;
            data["lora"][i]["iaruRegion"]               = loraTypes[i].iaruRegion;
            data["lora"][i]["fskFrequency"]             = loraTypes[i].fskFrequency;
            data["lora"][i]["fskPower"]                 = loraTypes[i].fskPower;
            data["lora"][i]["fskShaping"]               = loraTypes[i].fskShaping;
            data["lora"][i]["fskFec"]                   = loraTypes[i].fskFec;
            data["lora"][i]["fskIaruRegion"]            = loraTypes[i].fskIaruRegion;
        }

        data["battery"]["sendVoltage"]              = battery.sendVoltage;
        data["battery"]["voltageAsTelemetry"]       = battery.voltageAsTelemetry;
        data["battery"]["sendVoltageAlways"]        = battery.sendVoltageAlways;
        data["battery"]["monitorVoltage"]           = battery.monitorVoltage;
        data["battery"]["sleepVoltage"]             = battery.sleepVoltage;

        data["telemetry"]["active"]                 = telemetry.active;
        data["telemetry"]["sendTelemetry"]          = telemetry.sendTelemetry;
        data["telemetry"]["temperatureCorrection"]  = telemetry.temperatureCorrection;

        data["winlink"]["password"]                 = winlink.password;

        data["notification"]["ledTx"]               = notification.ledTx;
        data["notification"]["ledTxPin"]            = notification.ledTxPin;
        data["notification"]["ledMessage"]          = notification.ledMessage;
        data["notification"]["ledMessagePin"]       = notification.ledMessagePin;
        data["notification"]["ledRxBeacon"]         = notification.ledRxBeacon;
        data["notification"]["flashEmergency"]      = notification.flashEmergency;
        data["notification"]["buzzerActive"]        = notification.buzzerActive;
        data["notification"]["buzzerPinTone"]       = notification.buzzerPinTone;
        data["notification"]["buzzerPinVcc"]        = notification.buzzerPinVcc;
        data["notification"]["bootUpBeep"]          = notification.bootUpBeep;
        data["notification"]["txBeep"]              = notification.txBeep;
        data["notification"]["messageRxBeep"]       = notification.messageRxBeep;
        data["notification"]["stationBeep"]         = notification.stationBeep;
        data["notification"]["lowBatteryBeep"]      = notification.lowBatteryBeep;
        data["notification"]["shutDownBeep"]        = notification.shutDownBeep;
        data["notification"]["bellRings"]           = notification.bellRings;
        data["notification"]["ledFlashlight"]       = notification.ledFlashlight;
        data["notification"]["ledFlashlightPin"]    = notification.ledFlashlightPin;

        data["pttTrigger"]["active"]                = ptt.active;
        data["pttTrigger"]["reverse"]               = ptt.reverse;
        data["pttTrigger"]["preDelay"]              = ptt.preDelay;
        data["pttTrigger"]["postDelay"]             = ptt.postDelay;
        data["pttTrigger"]["io_pin"]                = ptt.io_pin;

        data["other"]["simplifiedTrackerMode"]      = simplifiedTrackerMode;
        data["other"]["sendCommentAfterXBeacons"]   = sendCommentAfterXBeacons;
        data["other"]["path"]                       = path;
        data["other"]["nonSmartBeaconRate"]         = nonSmartBeaconRate;
        data["other"]["rememberStationTime"]        = rememberStationTime;
        data["other"]["standingUpdateTime"]         = standingUpdateTime;
        data["other"]["sendAltitude"]               = sendAltitude;
        data["other"]["disableGPS"]                 = disableGPS;
        data["other"]["cadActive"]                  = cadActive;
        data["other"]["cadWaitSec"]                 = cadWaitSec;
        data["other"]["cadFskRssi"]                 = cadFskRssi;
        data["other"]["email"]                      = email;

        const size_t written = serializeJson(data, configFile);
        configFile.close();
        if (written == 0) {
            Serial.println("Error: config not written, keeping the previous one");
            SPIFFS.remove("/tracker_conf.tmp");
            return false;
        }
        SPIFFS.remove("/tracker_conf.json");
        if (!SPIFFS.rename("/tracker_conf.tmp", "/tracker_conf.json")) {
            Serial.println("Error: could not replace the config file");
            return false;
        }
        return true;
    } catch (...) {
        Serial.println("Error: Exception occurred while saving config");
        configFile.close();
        SPIFFS.remove("/tracker_conf.tmp");
        return false;
    }
}

bool Configuration::readFile() {
    Serial.println("Reading config..");
    File configFile = SPIFFS.open("/tracker_conf.json", "r");

    if (configFile) {
        bool needsRewrite = false;
        JsonDocument data;
        DeserializationError error = deserializeJson(data, configFile);
        if (error) {
            // fichero vacio o danado (p. ej. corte durante un guardado): configuracion por defecto COMPLETA (3 balizas + 4 perfiles LoRa + AIS),
            // no un JSON vacio, que dejaba la WebUI sin perfiles
            Serial.println("Failed to read file, using default configuration");
            configFile.close();
            setDefaultValues();
            return writeFile();
        }

        if (data["wifiAP"]["active"].isNull() ||
            data["wifiAP"]["password"].isNull()) needsRewrite = true;
        wifiAP.active               = data["wifiAP"]["active"] | true;
        wifiAP.password             = data["wifiAP"]["password"] | "1234567890";

        if (data["fwUpdate"]["autoCheck"].isNull()) needsRewrite = true;
        fwUpdate.ssid               = data["fwUpdate"]["ssid"] | "";
        fwUpdate.password           = data["fwUpdate"]["password"] | "";
        fwUpdate.autoCheck          = data["fwUpdate"]["autoCheck"] | false;
        fwUpdate.autoInstall        = data["fwUpdate"]["autoInstall"] | false;

        if (data["kissNet"]["tcpPort"].isNull()) needsRewrite = true;
        kissNet.enabled             = data["kissNet"]["enabled"] | false;
        kissNet.tcpPort             = constrain((int)(data["kissNet"]["tcpPort"] | 8001), 1, 65535);
        kissNet.udpPort             = constrain((int)(data["kissNet"]["udpPort"] | 0), 0, 65535);
        kissNet.allowed             = data["kissNet"]["allowed"] | "";
        if (data["nmeaUart"]["baud"].isNull()) needsRewrite = true;
        nmeaUart.txPin              = constrain((int)(data["nmeaUart"]["txPin"] | NMEA_UART_TX_DEFAULT), -1, 48);
        nmeaUart.baud               = data["nmeaUart"]["baud"] | NMEA_UART_BAUD_DEFAULT;
        if (nmeaUart.baud != 4800 && nmeaUart.baud != 9600 && nmeaUart.baud != 38400) nmeaUart.baud = NMEA_UART_BAUD_DEFAULT;
        if (data["cat"]["port"].isNull()) needsRewrite = true;
        cat.enabled                 = data["cat"]["enabled"] | false;
        cat.usb                     = data["cat"]["usb"] | false;
        cat.port                    = constrain((int)(data["cat"]["port"] | 4532), 1, 65535);
        cat.allowed                 = data["cat"]["allowed"] | "";
        cat.pttBeacon               = data["cat"]["pttBeacon"] | false;
        cat.tuneKHz                 = constrain((int)(data["cat"]["tuneKHz"] | 0), 0, 500);
        if (data["team"]["watchActive"].isNull()) needsRewrite = true;
        team.watchActive            = data["team"]["watchActive"] | false;
        team.watchCalls             = data["team"]["watchCalls"] | "";
        team.timeoutMin             = constrain((int)(data["team"]["timeoutMin"] | 10), 1, 120);
        team.maxKm                  = constrain((int)(data["team"]["maxKm"] | 0), 0, 500);
        team.pollSec                = (int)(data["team"]["pollSec"] | 0);
        if (team.pollSec != 0) team.pollSec = constrain(team.pollSec, 60, 3600);
        team.eaiActive              = data["team"]["eaiActive"] | false;
        team.eaiCalls               = data["team"]["eaiCalls"] | "";
        team.eaiCode                = data["team"]["eaiCode"] | "";
        team.eaiMinutes             = constrain((int)(data["team"]["eaiMinutes"] | 30), 1, 50);

        JsonArray BeaconsArray = data["beacons"];
        for (int i = 0; i < BeaconsArray.size(); i++) {
            Beacon bcn;

            bcn.callsign                = BeaconsArray[i]["callsign"] | "NOCALL-7";
            bcn.callsign.toUpperCase();
            bcn.symbol                  = BeaconsArray[i]["symbol"] | "[";
            bcn.overlay                 = BeaconsArray[i]["overlay"] | "/";
            bcn.micE                    = BeaconsArray[i]["micE"] | "";
            bcn.comment                 = BeaconsArray[i]["comment"] | "";
            bcn.status                  = BeaconsArray[i]["status"] | "";
            bcn.smartBeaconActive       = BeaconsArray[i]["smartBeaconActive"] | true;
            bcn.smartBeaconSetting      = BeaconsArray[i]["smartBeaconSetting"] | 0;
            bcn.gpsEcoMode              = BeaconsArray[i]["gpsEcoMode"] | false;
            bcn.profileLabel            = BeaconsArray[i]["profileLabel"] | "";
            bcn.aisProfile              = BeaconsArray[i]["aisProfile"] | false;
            bcn.aisVesselType           = BeaconsArray[i]["aisVesselType"] | 0;
            bcn.aisName                 = BeaconsArray[i]["aisName"] | "";
            bcn.aisMmsi                 = BeaconsArray[i]["aisMmsi"] | 0;
            bcn.aisLength               = BeaconsArray[i]["aisLength"] | 0;
            bcn.aisBeam                 = BeaconsArray[i]["aisBeam"] | 0;
            bcn.aisDraft                = BeaconsArray[i]["aisDraft"] | 0.0f;
            if (bcn.aisProfile) enforceAisBeacon(bcn);
            beacons.push_back(bcn);
        }

        if (data["display"]["ecoMode"].isNull() ||
            data["display"]["timeout"].isNull() ||
            data["display"]["turn180"].isNull() ||
            data["display"]["showSymbol"].isNull()) needsRewrite = true;
        display.ecoMode                 = data["display"]["ecoMode"] | false;
        display.timeout                 = data["display"]["timeout"] | 4;
        display.turn180                 = data["display"]["turn180"] | false;
        display.showSymbol              = data["display"]["showSymbol"] | true;

        if (data["bluetooth"]["active"].isNull() ||
            data["bluetooth"]["deviceName"].isNull() ||
            data["bluetooth"]["useBLE"].isNull() ||
            data["bluetooth"]["useKISS"].isNull()) needsRewrite = true;
        if (data["operatingMode"].isNull()) needsRewrite = true;
        if (data["radioPpm1000"].isNull() || data["wspr"]["useGlobalPpm"].isNull()) needsRewrite = true;
        if (!data["radioPpm1000"].isNull()) radioPpm1000 = constrain((int)(data["radioPpm1000"] | 0), -50000, 50000);
        else                                radioPpm1000 = constrain((int)(data["radioPpm100"] | 0) * 10, -50000, 50000);          // configuraciones de 5.2.11 (centesimas de ppm)
        savedMode = constrain((int)(data["operatingMode"] | (int)MODE_APRS_LORA), (int)MODE_APRS_LORA, (int)MODE_APRS_FSK);   // MORSE nunca arranca solo
        operatingMode = savedMode;
        if (data["cw"]["wpm"].isNull() || data["cw"]["messages"].isNull()) needsRewrite = true;
        setCwDefaults();
        cw.wpm = constrain((int)(data["cw"]["wpm"] | cw.wpm), 5, 40);
        if (data["cw"]["keyMode"].isNull()) needsRewrite = true;
        cw.keyMode      = constrain((int)(data["cw"]["keyMode"] | cw.keyMode), 0, 2);
        cw.keyPin       = data["cw"]["keyPin"] | cw.keyPin;
        cw.dahPin       = data["cw"]["dahPin"] | cw.dahPin;
        cw.swapPaddles  = data["cw"]["swapPaddles"] | cw.swapPaddles;
        cw.keypadPin    = data["cw"]["keypadPin"] | cw.keypadPin;
        for (int i = 0; i < CW_MESSAGE_COUNT; i++) cw.keypadMv[i] = data["cw"]["keypadMv"][i] | cw.keypadMv[i];
        cw.keyTx        = data["cw"]["keyTx"] | cw.keyTx;
        cw.sidetone     = data["cw"]["sidetone"] | cw.sidetone;
        if (data["cw"]["trainerLevel"].isNull()) needsRewrite = true;
        cw.trainerLevel  = constrain((int)(data["cw"]["trainerLevel"] | cw.trainerLevel), 2, 41);
        cw.trainerGapWpm = constrain((int)(data["cw"]["trainerGapWpm"] | cw.trainerGapWpm), 0, 40);
        if (data["gps"]["gnssMask"].isNull()) needsRewrite = true;
        setGpsDefaults();
        {
            int mask = data["gps"]["gnssMask"] | gps.gnssMask;
            const bool okMask = (mask > 0 && mask < 64) && !(((mask & 16) || (mask & 32)) && !(mask & 1)) && !((mask & 4) && (mask & 8)) && (mask & 15) != 0;
            gps.gnssMask = okMask ? mask : gps.gnssMask;               // combinacion no valida en el M10: se conserva la de fabrica
        }
        gps.assistNow       = data["gps"]["assistNow"] | gps.assistNow;
        gps.dynModel        = data["gps"]["dynModel"] | gps.dynModel;
        if (!(gps.dynModel == 0 || (gps.dynModel >= 2 && gps.dynModel <= 9))) gps.dynModel = 0;
        gps.rateMs          = constrain((int)(data["gps"]["rateMs"] | gps.rateMs), 200, 10000);        // 200-250 ms solo los aplica el L76K; el M10 se limita a 500 ms
        gps.minElev         = constrain((int)(data["gps"]["minElev"] | gps.minElev), 0, 30);
        gps.staticHoldCms   = constrain((int)(data["gps"]["staticHoldCms"] | gps.staticHoldCms), 0, 255);
        gps.staticHoldDistM = constrain((int)(data["gps"]["staticHoldDistM"] | gps.staticHoldDistM), 0, 1000);
        gps.sbasDiffCorr    = data["gps"]["sbasDiffCorr"] | gps.sbasDiffCorr;
        gps.sbasRanging     = data["gps"]["sbasRanging"] | gps.sbasRanging;
        gps.sbasIntegrity   = data["gps"]["sbasIntegrity"] | gps.sbasIntegrity;
        gps.ppsEnabled      = data["gps"]["ppsEnabled"] | gps.ppsEnabled;
        gps.ppsFreqHz       = constrain((int)(data["gps"]["ppsFreqHz"] | gps.ppsFreqHz), 1, 10);
        gps.ppsLenMs        = constrain((int)(data["gps"]["ppsLenMs"] | gps.ppsLenMs), 1, 500);
        gps.ppsRising       = data["gps"]["ppsRising"] | gps.ppsRising;
        gps.ppsCableDelayNs = constrain((int)(data["gps"]["ppsCableDelayNs"] | gps.ppsCableDelayNs), -32000, 32000);
        gps.ppsUserDelayNs  = constrain((int)(data["gps"]["ppsUserDelayNs"] | gps.ppsUserDelayNs), -1000000, 1000000);
        if (data["gps"]["cnMask"].isNull() || data["gps"]["nmeaLean"].isNull()) needsRewrite = true;
        gps.cnMask          = data["gps"]["cnMask"] | gps.cnMask;
        gps.cnMask         &= 13;                                       // solo GPS (1), BeiDou (4) y GLONASS (8)
        if (gps.cnMask == 0) gps.cnMask = 5;                            // GPS + BeiDou: valor de fabrica del L76K
        gps.nmeaLean        = data["gps"]["nmeaLean"] | gps.nmeaLean;
        if (data["wspr"]["freqHz"].isNull()) needsRewrite = true;
        setWsprDefaults();
        wspr.freqHz      = constrain((int)(data["wspr"]["freqHz"] | wspr.freqHz), 400000000, 480000000);
        wspr.corrHz      = constrain((int)(data["wspr"]["corrHz"] | wspr.corrHz), -5000, 5000);
        wspr.useGlobalPpm = data["wspr"]["useGlobalPpm"] | false;
        wspr.powerDbm    = constrain((int)(data["wspr"]["powerDbm"] | wspr.powerDbm), 0, 20);
        wspr.intervalMin = constrain((int)(data["wspr"]["intervalMin"] | wspr.intervalMin), 2, 60);
        wspr.locator     = data["wspr"]["locator"] | "";
        wspr.dither      = data["wspr"]["dither"] | wspr.dither;
        wspr.screenOff   = data["wspr"]["screenOff"] | wspr.screenOff;
        wspr.driftTenths = constrain((int)(data["wspr"]["driftTenths"] | wspr.driftTenths), -200, 200);
        wspr.perPower    = data["wspr"]["perPower"] | wspr.perPower;
        for (int i = 0; i < 7; i++) {                                       // sin tabla guardada: cada potencia parte de los valores generales
            wspr.ppCorrHz[i]       = constrain((int)(data["wspr"]["ppCorrHz"][i] | wspr.corrHz), -5000, 5000);
            wspr.ppDriftTenths[i]  = constrain((int)(data["wspr"]["ppDriftTenths"][i] | wspr.driftTenths), -200, 200);
        }
        if (data["dapnet"]["freqHz"].isNull()) needsRewrite = true;
        setDapnetDefaults();
        dapnet.freqHz        = constrain((int)(data["dapnet"]["freqHz"] | dapnet.freqHz), 400000000, 480000000);
        dapnet.baud          = (int)(data["dapnet"]["baud"] | dapnet.baud);
        if (dapnet.baud != 512 && dapnet.baud != 1200 && dapnet.baud != 2400) dapnet.baud = 1200;
        dapnet.devTenthsKHz  = constrain((int)(data["dapnet"]["devTenthsKHz"] | dapnet.devTenthsKHz), 20, 80);
        dapnet.rxBwTenthsKHz = (int)(data["dapnet"]["rxBwTenthsKHz"] | dapnet.rxBwTenthsKHz);
        if (dapnet.rxBwTenthsKHz != 117 && dapnet.rxBwTenthsKHz != 146 && dapnet.rxBwTenthsKHz != 195 && dapnet.rxBwTenthsKHz != 234 && dapnet.rxBwTenthsKHz != 293) dapnet.rxBwTenthsKHz = 146;
        dapnet.rics          = data["dapnet"]["rics"] | "";
        dapnet.monitorAll    = data["dapnet"]["monitorAll"] | dapnet.monitorAll;
        dapnet.timeSync      = data["dapnet"]["timeSync"] | dapnet.timeSync;
        dapnet.charset       = constrain((int)(data["dapnet"]["charset"] | dapnet.charset), 0, 1);
        if (data["ft"]["ft8FreqHz"].isNull()) needsRewrite = true;
        setFtDefaults();
        ft.ft8FreqHz     = constrain((int)(data["ft"]["ft8FreqHz"] | ft.ft8FreqHz), 400000000, 480000000);
        ft.ft4FreqHz     = (int)(data["ft"]["ft4FreqHz"] | ft.ft4FreqHz);
        if (ft.ft4FreqHz != 0) ft.ft4FreqHz = constrain(ft.ft4FreqHz, 400000000, 480000000);
        ft.ft8Region     = constrain((int)(data["ft"]["ft8Region"] | ft.ft8Region), 1, 3);
        ft.ft4Region     = constrain((int)(data["ft"]["ft4Region"] | ft.ft4Region), 1, 3);
        ft.fstFreqHz     = (int)(data["ft"]["fstFreqHz"] | ft.fstFreqHz);
        if (ft.fstFreqHz != 0) ft.fstFreqHz = constrain(ft.fstFreqHz, 400000000, 480000000);
        ft.fstRegion     = constrain((int)(data["ft"]["fstRegion"] | ft.fstRegion), 1, 3);
        ft.fstSeconds    = ((int)(data["ft"]["fstSeconds"] | ft.fstSeconds) == 300) ? 300 : 120;
        ft.fstEvery      = constrain((int)(data["ft"]["fstEvery"] | ft.fstEvery), 2, 240);
        ft.corrHz        = constrain((int)(data["ft"]["corrHz"] | ft.corrHz), -5000, 5000);
        ft.powerDbm      = constrain((int)(data["ft"]["powerDbm"] | ft.powerDbm), 0, 22);
        ft.everyCycles   = constrain((int)(data["ft"]["everyCycles"] | ft.everyCycles), 2, 240);
        ft.locator       = data["ft"]["locator"] | "";
        ft.text          = data["ft"]["text"] | "";
        ft.screenOff     = data["ft"]["screenOff"] | ft.screenOff;
        if (data["lrfhss"]["freqHz"].isNull()) needsRewrite = true;
        setLrfhssDefaults();
        lrfhss.enabled     = data["lrfhss"]["enabled"] | lrfhss.enabled;
        lrfhss.freqHz      = constrain((int)(data["lrfhss"]["freqHz"] | lrfhss.freqHz), 400000000, 480000000);
        lrfhss.bwIdx       = constrain((int)(data["lrfhss"]["bwIdx"] | lrfhss.bwIdx), 0, 9);
        lrfhss.crIdx       = constrain((int)(data["lrfhss"]["crIdx"] | lrfhss.crIdx), 0, 3);
        lrfhss.narrowGrid  = data["lrfhss"]["narrowGrid"] | lrfhss.narrowGrid;
        lrfhss.powerDbm    = constrain((int)(data["lrfhss"]["powerDbm"] | lrfhss.powerDbm), -9, 22);
        lrfhss.intervalMin = constrain((int)(data["lrfhss"]["intervalMin"] | lrfhss.intervalMin), 2, 60);
        lrfhss.locator     = data["lrfhss"]["locator"] | "";
        lrfhss.locatorLen  = (int)(data["lrfhss"]["locatorLen"] | lrfhss.locatorLen);
        if (lrfhss.locatorLen != 4 && lrfhss.locatorLen != 8) lrfhss.locatorLen = 6;
        if (data["cw"]["beaconMin"].isNull()) needsRewrite = true;
        cw.beaconMin    = constrain((int)(data["cw"]["beaconMin"] | cw.beaconMin), 0, 240);
        cw.beaconFreq   = data["cw"]["beaconFreq"] | 0L;                    // 0 = se completa con el perfil LoRa 1 al terminar de leer
        cw.beaconPower  = data["cw"]["beaconPower"] | 0;
        cw.beaconRegion = constrain((int)(data["cw"]["beaconRegion"] | 1), 1, 3);
        cw.keyFreq      = data["cw"]["keyFreq"] | 0L;
        cw.keyPower     = data["cw"]["keyPower"] | 0;
        cw.keyRegion    = constrain((int)(data["cw"]["keyRegion"] | 1), 1, 3);
        if (!data["cw"]["beaconText"].isNull()) cw.beaconText = data["cw"]["beaconText"].as<String>();
        for (int i = 0; i < CW_MESSAGE_COUNT; i++) {
            if (!data["cw"]["messages"][i].isNull()) cw.messages[i] = data["cw"]["messages"][i].as<String>();
        }
        bluetooth.active                = data["bluetooth"]["active"] | false;
        bluetooth.deviceName            = data["bluetooth"]["deviceName"] | "LoRaTracker";
        #ifdef HAS_BT_CLASSIC
            bluetooth.useBLE            = data["bluetooth"]["useBLE"] | false;
            bluetooth.useKISS           = data["bluetooth"]["useKISS"] | false;
        #else
            bluetooth.useBLE            = true;    // fixed as BLE
            bluetooth.useKISS           = data["bluetooth"]["useKISS"] | true;    // true=KISS,  false=TNC2
        #endif

        JsonArray LoraTypesArray = data["lora"];
        for (int j = 0; j < LoraTypesArray.size(); j++) {
            LoraType loraType;

            loraType.frequency          = LoraTypesArray[j]["frequency"] | 433775000;
            loraType.spreadingFactor    = LoraTypesArray[j]["spreadingFactor"] | 12;
            loraType.signalBandwidth    = LoraTypesArray[j]["signalBandwidth"] | 125000;
            loraType.codingRate4        = LoraTypesArray[j]["codingRate4"] | 5;
            loraType.power              = LoraTypesArray[j]["power"] | 20;
            loraType.ais                = LoraTypesArray[j]["ais"] | false;
            loraType.fsk                = LoraTypesArray[j]["fsk"] | false;
            loraType.fskBitRate         = LoraTypesArray[j]["fskBitRate"] | 9.6f;
            loraType.fskFreqDev         = LoraTypesArray[j]["fskFreqDev"] | 3.0f;
            loraType.fskRxBandwidth     = LoraTypesArray[j]["fskRxBandwidth"] | 39.0f;
            loraType.iaruRegion         = LoraTypesArray[j]["iaruRegion"] | 1;
            if (loraType.iaruRegion < 1 || loraType.iaruRegion > 3) loraType.iaruRegion = 1;
            if (LoraTypesArray[j]["fskFrequency"].isNull() || LoraTypesArray[j]["fskPower"].isNull()) needsRewrite = true;
            loraType.fskFrequency       = LoraTypesArray[j]["fskFrequency"] | 0L;
            loraType.fskPower           = LoraTypesArray[j]["fskPower"] | 0;
            loraType.fskShaping         = constrain((int)(LoraTypesArray[j]["fskShaping"] | 1), 0, 4);
            loraType.fskFec             = constrain((int)(LoraTypesArray[j]["fskFec"] | 0), 0, 4);
            loraType.fskIaruRegion      = LoraTypesArray[j]["fskIaruRegion"] | loraType.iaruRegion;
            if (loraType.fskIaruRegion < 1 || loraType.fskIaruRegion > 3) loraType.fskIaruRegion = 1;
            if (loraType.fskFrequency <= 0) loraType.fskFrequency = loraType.frequency;     // configuraciones anteriores: el FSK parte del perfil LoRa
            if (loraType.fskPower <= 0)     loraType.fskPower     = loraType.power;
            if (loraType.ais) loraType.fsk = false;     // el perfil AIS es siempre LoRa
            if (loraType.ais) {                     // el perfil LoRa del modo nautico AIS es siempre SF9 y CR 4/5
                loraType.spreadingFactor = 9;
                loraType.codingRate4     = 5;
            }
            loraTypes.push_back(loraType);
        }
        {   // reparacion: debe haber 3 perfiles de baliza y 4 perfiles LoRa regionales delante de los perfiles AIS
            int baseBeacons = 0, baseLora = 0;
            for (const Beacon &b : beacons)  if (!b.aisProfile) baseBeacons++;
            for (const LoraType &l : loraTypes) if (!l.ais) baseLora++;
            while (baseBeacons < 3) {
                Beacon bcn;
                bcn.callsign = "NOCALL-7"; bcn.symbol = "["; bcn.overlay = "/"; bcn.micE = ""; bcn.comment = ""; bcn.status = "";
                bcn.smartBeaconActive = true; bcn.smartBeaconSetting = 0; bcn.gpsEcoMode = false; bcn.profileLabel = "";
                beacons.insert(beacons.begin() + baseBeacons, bcn);
                baseBeacons++;
                needsRewrite = true;
            }
            while (baseLora < 4) {
                LoraType lt;
                switch (baseLora) {
                    case 0: lt.frequency = 433775000; lt.spreadingFactor = 12; lt.codingRate4 = 5; break;
                    case 1: lt.frequency = 434855000; lt.spreadingFactor = 9;  lt.codingRate4 = 7; break;
                    case 2: lt.frequency = 439912500; lt.spreadingFactor = 12; lt.codingRate4 = 5; break;
                    default: lt.frequency = 915000000; lt.spreadingFactor = 12; lt.codingRate4 = 5; break;
                }
                lt.signalBandwidth = 125000; lt.power = 20;
                loraTypes.insert(loraTypes.begin() + baseLora, lt);
                baseLora++;
                needsRewrite = true;
            }
        }
        if (ensureAisProfiles()) needsRewrite = true;       // configuraciones anteriores: se anade el perfil nautico AIS
        if (cw.beaconFreq <= 0 || cw.keyFreq <= 0 || cw.beaconPower <= 0 || cw.keyPower <= 0) {      // sin perfiles CW guardados: parten del perfil LoRa 1
            const long baseFreq = loraTypes.empty() ? 433775000L : loraTypes[0].frequency;
            const int  basePow  = loraTypes.empty() ? 17 : loraTypes[0].power;
            (void)baseFreq;                                  // CW parte del segmento de telegrafia IARU (432,050 MHz), no de la frecuencia LoRa
            if (cw.beaconFreq  <= 0) cw.beaconFreq  = 432050000L;
            if (cw.keyFreq     <= 0) cw.keyFreq     = 432050000L;
            if (cw.beaconPower <= 0) cw.beaconPower = basePow;
            if (cw.keyPower    <= 0) cw.keyPower    = basePow;
            needsRewrite = true;
        }
        if (cw.beaconFreq == 433775000L) { cw.beaconFreq = 432050000L; needsRewrite = true; }       // 5.2.11 inicial copiaba la frecuencia LoRa: CW pasa al segmento de telegrafia
        if (cw.keyFreq    == 433775000L) { cw.keyFreq    = 432050000L; needsRewrite = true; }
        wspr.freqHz = 432301500;                            // la frecuencia WSPR es fija

        if (data["battery"]["sendVoltage"].isNull() ||
            data["battery"]["voltageAsTelemetry"].isNull() ||
            data["battery"]["sendVoltageAlways"].isNull() ||
            data["battery"]["monitorVoltage"].isNull() ||
            data["battery"]["sleepVoltage"].isNull()) needsRewrite = true;
        battery.sendVoltage             = data["battery"]["sendVoltage"] | false;
        battery.voltageAsTelemetry      = data["battery"]["voltageAsTelemetry"] | false;
        battery.sendVoltageAlways       = data["battery"]["sendVoltageAlways"] | false;
        battery.monitorVoltage          = data["battery"]["monitorVoltage"] | false;
        battery.sleepVoltage            = data["battery"]["sleepVoltage"] | 2.9;

        if (data["telemetry"]["active"].isNull() ||
            data["telemetry"]["sendTelemetry"].isNull() ||
            data["telemetry"]["temperatureCorrection"].isNull()) needsRewrite = true;
        telemetry.active                = data["telemetry"]["active"] | false;
        telemetry.sendTelemetry         = data["telemetry"]["sendTelemetry"] | false;
        telemetry.temperatureCorrection = data["telemetry"]["temperatureCorrection"] | 0.0;

        if (data["winlink"]["password"].isNull()) needsRewrite = true;
        winlink.password                = data["winlink"]["password"] | "NOPASS";

        if (data["notification"]["ledTx"].isNull() ||
            data["notification"]["ledTxPin"].isNull() ||
            data["notification"]["ledMessage"].isNull() ||
            data["notification"]["ledMessagePin"].isNull() ||
            data["notification"]["buzzerActive"].isNull() ||
            data["notification"]["buzzerPinTone"].isNull() ||
            data["notification"]["buzzerPinVcc"].isNull() ||
            data["notification"]["bootUpBeep"].isNull() ||
            data["notification"]["txBeep"].isNull() ||
            data["notification"]["messageRxBeep"].isNull() ||
            data["notification"]["stationBeep"].isNull() ||
            data["notification"]["lowBatteryBeep"].isNull() ||
            data["notification"]["shutDownBeep"].isNull() ||
            data["notification"]["bellRings"].isNull() ||
            data["notification"]["ledFlashlight"].isNull() ||
            data["notification"]["ledFlashlightPin"].isNull()) needsRewrite = true;
        notification.ledTx              = data["notification"]["ledTx"] | false;
        notification.ledTxPin           = data["notification"]["ledTxPin"] | defaultPin(NOTIF_DEF_LED_TX);
        notification.ledMessage         = data["notification"]["ledMessage"] | false;
        notification.ledRxBeacon        = data["notification"]["ledRxBeacon"] | false;
        notification.flashEmergency     = data["notification"]["flashEmergency"] | false;
        notification.ledMessagePin      = data["notification"]["ledMessagePin"] | defaultPin(NOTIF_DEF_LED_MESSAGE);
        notification.buzzerActive       = data["notification"]["buzzerActive"] | false;
        notification.buzzerPinTone      = data["notification"]["buzzerPinTone"] | defaultPin(NOTIF_DEF_BUZZER_TONE);
        notification.buzzerPinVcc       = data["notification"]["buzzerPinVcc"] | defaultPin(NOTIF_DEF_BUZZER_VCC);
        notification.bootUpBeep         = data["notification"]["bootUpBeep"] | false;
        notification.txBeep             = data["notification"]["txBeep"] | false;
        notification.messageRxBeep      = data["notification"]["messageRxBeep"] | false;
        notification.stationBeep        = data["notification"]["stationBeep"] | false;
        notification.lowBatteryBeep     = data["notification"]["lowBatteryBeep"] | false;
        notification.shutDownBeep       = data["notification"]["shutDownBeep"] | false;
        notification.bellRings          = data["notification"]["bellRings"] | true;
        notification.ledFlashlight      = data["notification"]["ledFlashlight"] | false;
        notification.ledFlashlightPin   = data["notification"]["ledFlashlightPin"] | defaultPin(NOTIF_DEF_FLASHLIGHT);

        if (data["pttTrigger"]["active"].isNull() ||
            data["pttTrigger"]["reverse"].isNull() ||
            data["pttTrigger"]["preDelay"].isNull() ||
            data["pttTrigger"]["postDelay"].isNull() ||
            data["pttTrigger"]["io_pin"].isNull()) needsRewrite = true;
        ptt.active                      = data["pttTrigger"]["active"] | false;
        ptt.reverse                     = data["pttTrigger"]["reverse"] | false;
        ptt.preDelay                    = data["pttTrigger"]["preDelay"] | 0;
        ptt.postDelay                   = data["pttTrigger"]["postDelay"] | 0;
        ptt.io_pin                      = data["pttTrigger"]["io_pin"] | 4;

        if (data["other"]["simplifiedTrackerMode"].isNull() ||
            data["other"]["sendCommentAfterXBeacons"].isNull() ||
            data["other"]["path"].isNull() ||
            data["other"]["nonSmartBeaconRate"].isNull() ||
            data["other"]["rememberStationTime"].isNull() ||
            data["other"]["standingUpdateTime"].isNull() ||
            data["other"]["sendAltitude"].isNull() ||
            data["other"]["disableGPS"].isNull() ||
            data["other"]["cadActive"].isNull() ||
            data["other"]["email"].isNull()) needsRewrite = true;
        simplifiedTrackerMode           = data["other"]["simplifiedTrackerMode"] | false;
        sendCommentAfterXBeacons        = data["other"]["sendCommentAfterXBeacons"] | 10;
        path                            = data["other"]["path"] | "WIDE1-1";
        nonSmartBeaconRate              = data["other"]["nonSmartBeaconRate"] | 15;
        rememberStationTime             = data["other"]["rememberStationTime"] | 30;
        standingUpdateTime              = data["other"]["standingUpdateTime"] | 15;
        sendAltitude                    = data["other"]["sendAltitude"] | true;
        disableGPS                      = data["other"]["disableGPS"] | false;
        cadActive                       = data["other"]["cadActive"] | true;
        cadWaitSec                      = constrain((int)(data["other"]["cadWaitSec"] | 10), 1, 60);
        cadFskRssi                      = constrain((int)(data["other"]["cadFskRssi"] | -95), -120, -60);
        email                           = data["other"]["email"] | "";

        configFile.close();

        if (needsRewrite) {
            // La configuracion en memoria ya esta completa (valores por defecto en lo que faltaba): se guarda y se sigue.
            // No se reinicia: este codigo corre en el constructor global de Config, antes de que arranque FreeRTOS, y
            // ESP.restart() ahi aborta (xTaskGetIdleTaskHandleForCore) y, si algo vuelve a faltar, deja la placa en bucle.
            Serial.println("Config JSON incomplete, rewriting...");
            writeFile();
        }
        Serial.println("Config read successfuly");
        return true;
    } else {
        Serial.println("Config file not found");
        return false;
    }
}

void Configuration::enforceAisBeacon(Beacon& beacon) {
    const AIS_Vessel::VesselType& vessel = AIS_Vessel::vesselType(beacon.aisVesselType);
    beacon.aisVesselType        = vessel.id;
    char forced[24];
    if (AIS_Vessel::forceCallsign(beacon.callsign.c_str(), vessel.ssid, forced, sizeof(forced)) > 0) beacon.callsign = forced;   // el SSID no se puede cambiar
    beacon.symbol               = String(vessel.symbol);
    beacon.overlay              = String(vessel.overlay);
    beacon.micE                 = "";                                   // el perfil nautico usa siempre posicion comprimida (Base91)
    beacon.smartBeaconActive    = true;
    beacon.smartBeaconSetting   = AIS_Vessel::smartBeaconSettingFor(vessel.aisClass);
    char name[21];
    AIS_Vessel::sanitizeName(beacon.aisName.c_str(), name, sizeof(name));
    beacon.aisName              = name;
    if (!AIS_Vessel::validMmsi(beacon.aisMmsi)) beacon.aisMmsi = 0;
    beacon.aisLength = AIS_Vessel::clampLength(vessel.aisClass, beacon.aisLength);   // clase B (recreo): 6 - 24 m
    if (beacon.aisBeam > 126)   beacon.aisBeam   = 126;
    if (!(beacon.aisDraft >= 0.0f)) beacon.aisDraft = 0.0f;
    if (beacon.aisDraft > 25.5f)    beacon.aisDraft = 25.5f;
}

int Configuration::aisLoraIndex() const {
    for (size_t i = 0; i < loraTypes.size(); i++) {
        if (loraTypes[i].ais) return (int) i;
    }
    return -1;
}

int Configuration::aisBeaconIndex() const {
    for (size_t i = 0; i < beacons.size(); i++) {
        if (beacons[i].aisProfile) return (int) i;
    }
    return -1;
}

bool Configuration::ensureAisProfiles() {
    bool changed = false;
    if (aisBeaconIndex() < 0) {
        Beacon beacon;
        beacon.callsign             = "NOCALL-5";
        beacon.symbol               = "Y";
        beacon.overlay              = "/";
        beacon.micE                 = "";
        beacon.comment              = "";
        beacon.status               = "";
        beacon.smartBeaconActive    = true;
        beacon.smartBeaconSetting   = 5;
        beacon.gpsEcoMode           = false;
        beacon.profileLabel         = "AIS";
        beacon.aisProfile           = true;
        beacon.aisVesselType        = 0;
        enforceAisBeacon(beacon);
        beacons.push_back(beacon);
        changed = true;
    }
    int index = aisLoraIndex();
    if (index < 0) {
        LoraType loraType;
        loraType.frequency          = 433775000;            // la frecuencia se puede cambiar en la WebUI; SF y CR no
        loraType.spreadingFactor    = 9;
        loraType.signalBandwidth    = 125000;
        loraType.codingRate4        = 5;
        loraType.power              = 20;
        loraType.ais                = true;
        loraTypes.push_back(loraType);
        changed = true;
    } else if (index != (int) loraTypes.size() - 1) {       // el perfil AIS va siempre el ultimo (los demas rotan con el menu)
        LoraType aisType = loraTypes[index];
        loraTypes.erase(loraTypes.begin() + index);
        loraTypes.push_back(aisType);
        changed = true;
    }
    return changed;
}

void Configuration::setDapnetDefaults() {
    dapnet.freqHz        = 439987500;           // frecuencia de DAPNET en la mayor parte de Europa (hampager.de, «Common frequencies for DAPNET»)
    dapnet.baud          = 1200;
    dapnet.devTenthsKHz  = 45;                  // POCSAG: +-4,5 kHz (valor estandar; a confirmar con la documentacion de DAPNET)
    dapnet.rxBwTenthsKHz = 146;                 // 14,6 kHz: cabe la senal (2 x 4,5 + 1,2 kHz) con margen para el error del oscilador
    dapnet.rics          = "";
    dapnet.monitorAll    = false;
    dapnet.timeSync      = false;
    dapnet.charset       = 1;                   // la red es alemana: ISO 646 aleman por defecto (los demas caracteres son ASCII)
}

void Configuration::setFtDefaults() {
    ft.ft8FreqHz   = 432174000;             // dial FT8 en 70 cm segun la lista de frecuencias de WSJT-X (valido en las tres regiones IARU)
    ft.ft4FreqHz   = 0;                     // ni WSJT-X ni los planes IARU fijan un canal de FT4 en 70 cm: se rellena a mano
    ft.ft8Region   = 1;
    ft.ft4Region   = 1;
    ft.fstFreqHz   = 0;                     // FST4W: tampoco hay canal definido en 70 cm
    ft.fstRegion   = 1;
    ft.fstSeconds  = 120;
    ft.fstEvery    = 4;                     // una emision cada 4 periodos (8 minutos con FST4W-120)
    ft.corrHz      = 0;
    ft.powerDbm    = 17;
    ft.everyCycles = 4;                     // una emision por minuto en FT8 (4 ciclos de 15 s)
    ft.locator     = "";
    ft.text        = "";
    ft.screenOff   = true;
}

void Configuration::setLrfhssDefaults() {
    lrfhss.enabled     = false;                 // desactivada por defecto: puede requerir autorizacion especial (Orden IET/1311/2013, art. 17.2)
    lrfhss.freqHz      = 434000000;             // IARU R1 (v10.02): centro de experimentos digitales, nota (p): datos tipo LoRa hasta 125 kHz
    lrfhss.bwIdx       = 0;                     // 39,06 kHz: ocupa unos 433,98-434,02 MHz
    lrfhss.crIdx       = 3;                     // 1/3
    lrfhss.narrowGrid  = true;
    lrfhss.powerDbm    = 10;
    lrfhss.intervalMin = 10;
    lrfhss.locator     = "";
    lrfhss.locatorLen  = 6;
}

void Configuration::setGpsDefaults() {
    // Valores de fabrica del M10 en la Supreme (los GPS_USE_* de board_pinout.h, si existen, prevalecen)
    #ifndef GPS_USE_GPS
        #define GPS_USE_GPS         1
    #endif
    #ifndef GPS_USE_GALILEO
        #define GPS_USE_GALILEO     1
    #endif
    #ifndef GPS_USE_BEIDOU
        #define GPS_USE_BEIDOU      0
    #endif
    #ifndef GPS_USE_GLONASS
        #define GPS_USE_GLONASS     0
    #endif
    #ifndef GPS_USE_QZSS
        #define GPS_USE_QZSS        0
    #endif
    #ifndef GPS_USE_SBAS
        #define GPS_USE_SBAS        1
    #endif
    #ifndef GPS_USE_ASSISTNOW_AUTONOMOUS
        #define GPS_USE_ASSISTNOW_AUTONOMOUS    1
    #endif
    #ifndef GPS_STATIC_HOLD_CMS
        #define GPS_STATIC_HOLD_CMS 0
    #endif
    #ifndef GPS_STATIC_HOLD_DIST_M
        #define GPS_STATIC_HOLD_DIST_M 0
    #endif
    gps.gnssMask        = (GPS_USE_GPS ? 1 : 0) | (GPS_USE_GALILEO ? 2 : 0) | (GPS_USE_BEIDOU ? 4 : 0) | (GPS_USE_GLONASS ? 8 : 0) | (GPS_USE_QZSS ? 16 : 0) | (GPS_USE_SBAS ? 32 : 0);
    gps.assistNow       = GPS_USE_ASSISTNOW_AUTONOMOUS;
    gps.dynModel        = 0;                    // portatil (el del receptor)
    gps.rateMs          = 1000;
    gps.minElev         = 5;                    // el del receptor
    gps.staticHoldCms   = GPS_STATIC_HOLD_CMS;
    gps.staticHoldDistM = GPS_STATIC_HOLD_DIST_M;
    gps.sbasDiffCorr    = true;                 // valores del receptor
    gps.sbasRanging     = true;
    gps.sbasIntegrity   = false;
    gps.ppsEnabled      = true;                 // el M10 trae el pulso activado: 1 Hz, 100 ms, flanco de subida
    gps.ppsFreqHz       = 1;
    gps.ppsLenMs        = 100;
    gps.ppsRising       = true;
    gps.ppsCableDelayNs = 50;
    gps.ppsUserDelayNs  = 0;
    gps.cnMask          = 5;                    // L76K: GPS + BeiDou (valor de fabrica del modulo)
    gps.nmeaLean        = true;
}

void Configuration::setWsprDefaults() {
    wspr.freqHz = 432301500;
    wspr.corrHz = 0;
    wspr.useGlobalPpm = false;
    wspr.powerDbm = 10;
    wspr.intervalMin = 10;
    wspr.locator = "";
    wspr.dither = true;
    wspr.screenOff = false;
    wspr.driftTenths = 0;
    wspr.perPower = false;
    for (int i = 0; i < 7; i++) { wspr.ppCorrHz[i] = 0; wspr.ppDriftTenths[i] = 0; }
}

void Configuration::setCwDefaults() {
    cw.wpm = 15;
    cw.keyMode = 0;
    cw.keyPin = -1;
    cw.dahPin = -1;
    cw.swapPaddles = false;
    cw.keypadPin = -1;
    for (int i = 0; i < CW_MESSAGE_COUNT; i++) cw.keypadMv[i] = 0;
    cw.keyTx = true;
    cw.sidetone = true;
    cw.trainerLevel = 4;
    cw.trainerGapWpm = 10;
    cw.beaconMin = 0;
    cw.beaconFreq = 432050000; cw.beaconPower = 17; cw.beaconRegion = 1;
    cw.keyFreq    = 432050000; cw.keyPower    = 17; cw.keyRegion    = 1;
    cw.beaconText = "DE {CALL}/B {LOC} V{VBAT}";
    cw.messages[0] = "CQ CQ DE {CALL} {CALL} K";
    cw.messages[1] = "{CALL} 73 SK";
    cw.messages[2] = "TEST DE {CALL}";
    cw.messages[3] = "VVV DE {CALL}";
    cw.messages[4] = "QRZ DE {CALL} K";
}

void Configuration::setDefaultValues() {
    wifiAP.active                   = true;
    wifiAP.password                 = "1234567890";

    fwUpdate.ssid                   = "";
    fwUpdate.password               = "";
    fwUpdate.autoCheck              = false;
    fwUpdate.autoInstall            = false;

    kissNet.enabled                 = false;
    kissNet.tcpPort                 = 8001;
    kissNet.udpPort                 = 0;
    kissNet.allowed                 = "";
    nmeaUart.txPin                  = NMEA_UART_TX_DEFAULT;
    nmeaUart.baud                   = NMEA_UART_BAUD_DEFAULT;
    cat.enabled                     = false;
    cat.usb                         = false;
    cat.port                        = 4532;
    cat.allowed                     = "";
    cat.pttBeacon                   = false;
    cat.tuneKHz                     = 0;
    team.watchActive                = false;
    team.watchCalls                 = "";
    team.timeoutMin                 = 10;
    team.maxKm                      = 0;
    team.pollSec                    = 0;
    team.eaiActive                  = false;
    team.eaiCalls                   = "";
    team.eaiCode                    = "";
    team.eaiMinutes                 = 30;

    for (int i = 0; i < 3; i++) {
        Beacon beacon;
        beacon.callsign             = "NOCALL-7";
        beacon.symbol               = "[";
        beacon.overlay              = "/";
        beacon.micE                 = "";
        beacon.comment              = "";
        beacon.smartBeaconActive    = true;
        beacon.smartBeaconSetting   = 0;
        beacon.gpsEcoMode           = false;
        beacon.profileLabel         = "";
        beacon.status               = "";
        beacons.push_back(beacon);
    }

    display.ecoMode                 = false;
    display.timeout                 = 4;
    display.turn180                 = false;
    display.showSymbol              = true;

    setCwDefaults();
    setWsprDefaults();
    setLrfhssDefaults();
    setFtDefaults();
    setDapnetDefaults();
    setGpsDefaults();
    radioPpm1000                    = 0;
    operatingMode                   = MODE_APRS_LORA;
    savedMode                       = MODE_APRS_LORA;
    bluetooth.active                = false;
    bluetooth.deviceName            = "LoRaTracker";
    #ifdef HAS_BT_CLASSIC
        bluetooth.useBLE            = false;
        bluetooth.useKISS           = false;
    #else
        bluetooth.useBLE            = true;    // fixed as BLE
        bluetooth.useKISS           = true;
    #endif

    for (int j = 0; j < 4; j++) {
        LoraType loraType;
        switch (j) {
            case 0:
                loraType.frequency           = 433775000;
                loraType.spreadingFactor     = 12;
                loraType.codingRate4         = 5;
                break;
            case 1:
                loraType.frequency           = 434855000;
                loraType.spreadingFactor     = 9;
                loraType.codingRate4         = 7;
                break;
            case 2:
                loraType.frequency           = 439912500;
                loraType.spreadingFactor     = 12;
                loraType.codingRate4         = 5;
                break;
            case 3:
                loraType.frequency           = 915000000;
                loraType.spreadingFactor     = 12;
                loraType.codingRate4         = 5;
                break;
        }
        loraType.signalBandwidth    = 125000;
        loraType.power              = 20;
        loraTypes.push_back(loraType);
    }
    ensureAisProfiles();                                    // 4.o perfil de baliza (nautico AIS) y 5.o perfil LoRa (fijo SF9 / CR 4/5)

    battery.sendVoltage             = false;
    battery.voltageAsTelemetry      = false;
    battery.sendVoltageAlways       = false;
    battery.monitorVoltage          = false;
    battery.sleepVoltage            = 2.9;

    telemetry.active                 = false;
    telemetry.sendTelemetry          = false;
    telemetry.temperatureCorrection  = 0.0;

    winlink.password                = "NOPASS";

    notification.ledTx              = false;
    notification.ledTxPin           = defaultPin(NOTIF_DEF_LED_TX);
    notification.ledMessage         = false;
    notification.ledRxBeacon        = false;
    notification.flashEmergency     = false;
    notification.ledMessagePin      = defaultPin(NOTIF_DEF_LED_MESSAGE);
    notification.buzzerActive       = false;
    notification.buzzerPinTone      = defaultPin(NOTIF_DEF_BUZZER_TONE);
    notification.buzzerPinVcc       = defaultPin(NOTIF_DEF_BUZZER_VCC);
    notification.bootUpBeep         = false;
    notification.txBeep             = false;
    notification.messageRxBeep      = false;
    notification.stationBeep        = false;
    notification.lowBatteryBeep     = false;
    notification.shutDownBeep       = false;
    notification.bellRings          = true;
    notification.ledFlashlight      = false;
    notification.ledFlashlightPin   = defaultPin(NOTIF_DEF_FLASHLIGHT);

    ptt.active                      = false;
    ptt.reverse                     = false;
    ptt.preDelay                    = 0;
    ptt.postDelay                   = 0;
    ptt.io_pin                      = 4;

    simplifiedTrackerMode           = false;
    sendCommentAfterXBeacons        = 10;
    path                            = "WIDE1-1";
    nonSmartBeaconRate              = 15;
    rememberStationTime             = 30;
    standingUpdateTime              = 15;
    sendAltitude                    = true;
    disableGPS                      = false;
    cadActive                       = true;
    cadWaitSec                      = 10;
    cadFskRssi                      = -95;
    email                           = "";

    Serial.println("New Data Created... All is Written!");
}

Configuration::Configuration() {
    // Vacio a proposito: SPIFFS (montar/formatear/escribir flash) NO puede ejecutarse en un constructor global,
    // porque el planificador FreeRTOS aun no existe (assert xTaskGetIdleTaskHandleForCore). Ver init().
}

void Configuration::init() {
    if (!SPIFFS.begin(false)) {
        Serial.println("SPIFFS Mount Failed, formatting...");

        if (!SPIFFS.begin(true)) {
            Serial.println("SPIFFS Format Failed");
            return;
        }
    }
    Serial.println("SPIFFS Ready");

    if (!SPIFFS.exists("/tracker_conf.json")) {
        Serial.println("Config not found, creating default...");
        setDefaultValues();
        writeFile();                                // sin reiniciar (ver readFile): la configuracion por defecto ya esta en memoria
        return;
    }

    readFile();
}