/*
 * Control por Bluetooth LE para la app APRS LoRa+ de Android. Ver include/ble_control.h y docs/BLE_CONTROL.md
 *
 * Las ordenes llegan en la tarea de NimBLE (onWrite): solo se trocean en lineas y se dejan en una cola. Se ejecutan en el bucle
 * principal (loop), igual que el resto del firmware, asi que no hay carreras con la radio, la configuracion ni la pantalla.
 *
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include <TinyGPS++.h>
#include <esp_attr.h>
#include <logger.h>
#include "ble_control.h"
#include "configuration.h"
#include "board_pinout.h"
#include "display.h"
#include "battery_utils.h"
#include "station_utils.h"
#include "dapnet_utils.h"
#include "sat_config.h"
#include "sat_utils.h"
#include "rotor_utils.h"
#include "sat_rx.h"
#include "sat_tx.h"
#include "baro_utils.h"
#include "lora_utils.h"
#include "gps_utils.h"
#include "imu_utils.h"
#include "mag_utils.h"
#include "wx_utils.h"
#include "fw_update.h"
#include "freertos/stream_buffer.h"
#ifdef FAN_CTRL_PIN
    #include "thermal_utils.h"
#endif
#ifdef HAS_APRS_PAGES
    #include "pages_utils.h"
#endif

#define CONTROL_SERVICE_UUID    "4c6f5261-2b00-4ea7-a000-000000000001"
#define CONTROL_CMD_UUID        "4c6f5261-2b00-4ea7-a000-000000000002"
#define CONTROL_EVT_UUID        "4c6f5261-2b00-4ea7-a000-000000000003"
#define CONTROL_NMEA_UUID       "4c6f5261-2b00-4ea7-a000-000000000004"
#define CONTROL_OTA_UUID        "4c6f5261-2b00-4ea7-a000-000000000005"

#define CONTROL_PROTOCOL        1
#define CONTROL_LINE_MAX        256
#define CONTROL_QUEUE_LEN       4
#define KEY_QUEUE_LEN           64
#define KEY_DELAY_DEFAULT_MS    150
#define KEY_DELAY_MAX_MS        1000
#define OTA_BUFFER              16384           // bytes received from the phone and not yet in the flash
#define OTA_BLOCK               4096            // progress report every 4 KB: the phone keeps at most 2 blocks unconfirmed
#define OTA_TIMEOUT_MS          20000

extern Configuration    Config;
extern logging::Logger  logger;
extern TinyGPSPlus      gps;
extern String           versionNumber;
extern uint8_t          myBeaconsIndex;
extern int              myBeaconsSize;
extern Beacon*          currentBeacon;
extern uint8_t          loraIndex;
extern int              loraIndexSize;
extern bool             statusUpdate;
extern uint32_t         statusTime;
extern bool             sendUpdate;
extern bool             sendStartTelemetry;
extern bool             winlinkCommentState;
extern bool             digipeaterActive;
extern bool             smartBeaconActive;
extern bool             sosActive;
extern bool             batteryConnected;

// Modo pedido para el siguiente arranque: sobrevive a ESP.restart() (no al apagado) sin escribir la flash
RTC_NOINIT_ATTR uint32_t    bootModeMagic;
RTC_NOINIT_ATTR int32_t     bootModeValue;
static const uint32_t       BOOT_MODE_MAGIC = 0x4C6F5261;     // "LoRa"

namespace {

    struct Line { char text[CONTROL_LINE_MAX]; };

    NimBLECharacteristic*   cmdChar         = nullptr;
    NimBLECharacteristic*   evtChar         = nullptr;
    NimBLECharacteristic*   nmeaChar        = nullptr;
    NimBLECharacteristic*   otaChar         = nullptr;
    StreamBufferHandle_t    otaStream       = nullptr;
    volatile bool           otaActive       = false;
    volatile bool           otaOverflow     = false;
    size_t                  otaSize         = 0;
    size_t                  otaReported     = 0;
    uint32_t                otaLastMs       = 0;
    int                     otaLastPct      = -10;
    QueueHandle_t           queue           = nullptr;
    QueueHandle_t           keyQueue        = nullptr;
    volatile uint16_t       keyDelay        = KEY_DELAY_DEFAULT_MS;
    volatile bool           keyRejected     = false;           // llegaron eventos de manipulador fuera del modo MORSE
    volatile bool           connected       = false;
    volatile bool           authed          = false;           // enlace cifrado y emparejado con PIN
    volatile bool           nmeaWanted      = false;
    volatile uint16_t       mtu             = 23;

    // Montaje de lineas (solo en la tarea de NimBLE)
    char                    rxBuf[CONTROL_LINE_MAX];
    size_t                  rxLen           = 0;
    bool                    rxOverflow      = false;

    // Envios periodicos y tareas aplazadas (solo en el bucle principal)
    uint32_t                sensorsEveryMs  = 0, sensorsLastMs = 0;
    uint32_t                gnssEveryMs     = 0, gnssLastMs    = 0;
    uint32_t                restartAtMs     = 0;
    uint32_t                dapnetSeen      = 0;
    int                     cwIndex         = -1;
    char                    cwText[64]      = "";

    bool aprsMode(int mode) { return mode == MODE_APRS_LORA || mode == MODE_APRS_FSK; }

    // ---------------------------------------------------------------- envio al movil

    size_t chunkSize() {
        size_t c = mtu > 3 ? (size_t)(mtu - 3) : 20;
        return c > 240 ? 240 : c;
    }

    void sendLine(const String& line) {
        if (!connected || !authed || evtChar == nullptr) return;
        const uint8_t* data = (const uint8_t*)line.c_str();
        const size_t length = line.length();
        const size_t chunk  = chunkSize();
        for (size_t i = 0; i < length; i += chunk) {
            const size_t n = (length - i < chunk) ? (length - i) : chunk;
            evtChar->setValue(data + i, n);
            evtChar->notify();
            if (i + chunk < length) delay(20);
        }
    }

    void sendDoc(JsonDocument& doc) {
        String out;
        serializeJson(doc, out);
        out += '\n';
        sendLine(out);
    }

    void reply(const char* cmd, int id, const char* error = nullptr) {
        JsonDocument doc;
        doc["e"] = error ? "err" : "ok";
        doc["c"] = cmd;
        if (id >= 0) doc["id"] = id;
        if (error) doc["msg"] = error;
        sendDoc(doc);
    }

    // ---------------------------------------------------------------- modos

    struct ModeOption { uint8_t mode; const char* name; };

    // Los mismos que ofrece el menu de arranque (PAGES_Utils::selectMode); sin paginas solo hay modos APRS
    uint8_t listModes(ModeOption* out) {
        uint8_t n = 0;
        out[n++] = ModeOption{ MODE_APRS_LORA, "APRS LoRa" };
        if (LoRa_Utils::fskAvailable()) out[n++] = ModeOption{ MODE_APRS_FSK, "APRS FSK 9600" };
        #ifdef HAS_APRS_PAGES
            if (LoRa_Utils::cwAvailable()) {
                out[n++] = ModeOption{ MODE_MORSE,  "MORSE / CW" };
                out[n++] = ModeOption{ MODE_WSPR,   "WSPR beacon" };
                out[n++] = ModeOption{ MODE_FT8,    "FT8 beacon" };
                out[n++] = ModeOption{ MODE_FT4,    "FT4 beacon" };
                out[n++] = ModeOption{ MODE_FST4W,  "FST4W beacon" };
            }
            if (Config.lrfhss.enabled && LoRa_Utils::lrfhssAvailable()) out[n++] = ModeOption{ MODE_LRFHSS, "LR-FHSS beacon" };
            if (LoRa_Utils::dapnetAvailable()) out[n++] = ModeOption{ MODE_DAPNET, "DAPNET pager" };
            #ifdef HAS_SAT
            out[n++] = ModeOption{ MODE_SAT, "Satellites 70cm" };
            #endif
        #endif
        return n;
    }

    bool modeAvailable(int mode) {
        ModeOption options[12];
        const uint8_t n = listModes(options);
        for (uint8_t i = 0; i < n; i++) if (options[i].mode == mode) return true;
        return false;
    }

    // ---------------------------------------------------------------- eventos

    void sendInfo() {
        JsonDocument doc;
        doc["e"]     = "info";
        doc["v"]     = CONTROL_PROTOCOL;
        doc["fw"]    = versionNumber;
        doc["board"] = APRS_BOARD_ID;
        JsonObject caps = doc["caps"].to<JsonObject>();
        #ifdef NMEA_OUTPUT
            caps["nmea"] = true;
        #else
            caps["nmea"] = false;
        #endif
        #ifdef HAS_APRS_PAGES
            caps["dapnet"] = LoRa_Utils::dapnetAvailable();
            caps["cw"]     = LoRa_Utils::cwAvailable();
            caps["modes"]  = true;
            caps["rkey"]   = LoRa_Utils::cwAvailable();          // manipulador remoto (orden "k")
            caps["ota"]    = FW_Update::slotAvailable();         // actualizacion por BLE (orden "ota")
            caps["wifiUpd"] = FW_UPDATE_SUPPORTED && !Config.fwUpdate.ssid.isEmpty();
        #else
            caps["rkey"]   = false;
            caps["ota"]    = FW_Update::slotAvailable();
            caps["wifiUpd"] = FW_UPDATE_SUPPORTED && !Config.fwUpdate.ssid.isEmpty();
            caps["dapnet"] = false;
            caps["cw"]     = false;
            caps["modes"]  = false;
        #endif
        doc["mode"] = Config.operatingMode;
        JsonArray modes = doc["modes"].to<JsonArray>();
        ModeOption options[12];
        const uint8_t n = listModes(options);
        for (uint8_t i = 0; i < n; i++) {
            JsonObject m = modes.add<JsonObject>();
            m["m"] = options[i].mode;
            m["n"] = options[i].name;
        }
        doc["profile"] = myBeaconsIndex;
        JsonArray profiles = doc["profiles"].to<JsonArray>();
        for (int i = 0; i < myBeaconsSize; i++) {
            JsonObject p = profiles.add<JsonObject>();
            p["call"]  = Config.beacons[i].callsign;
            p["label"] = Config.beacons[i].profileLabel;
        }
        doc["lora"] = loraIndex;
        JsonArray loras = doc["loras"].to<JsonArray>();
        for (int i = 0; i < loraIndexSize; i++) {
            JsonObject l = loras.add<JsonObject>();
            l["f"]   = Config.loraTypes[i].frequency;
            l["sf"]  = Config.loraTypes[i].spreadingFactor;
            l["cr"]  = Config.loraTypes[i].codingRate4;
            l["fsk"] = Config.loraTypes[i].fsk;
        }
        JsonArray cw = doc["cwMsgs"].to<JsonArray>();
        for (int i = 0; i < CW_MESSAGE_COUNT; i++) cw.add(Config.cw.messages[i]);
        sendDoc(doc);
    }

    float batteryVolts() {
        if (!batteryConnected) return 0.0f;
        return BATTERY_Utils::getBatteryInfoVoltage().toFloat();
    }

    void sendStatus() {
        JsonDocument doc;
        doc["e"]       = "status";
        doc["mode"]    = Config.operatingMode;
        doc["profile"] = myBeaconsIndex;
        doc["lora"]    = loraIndex;
        doc["digi"]    = digipeaterActive;
        doc["smart"]   = smartBeaconActive;
        doc["sos"]     = sosActive;
        const float v  = batteryVolts();
        if (v > 0.0f) doc["batt"] = v;
        doc["up"]      = millis() / 1000UL;
        doc["bleKiss"] = Config.bluetooth.useKISS;
        sendDoc(doc);
    }

    void sendSensors() {
        JsonDocument doc;
        doc["e"] = "sensors";
        const float v = batteryVolts();
        if (v > 0.0f) doc["batt"] = v;
        float temperature = 0.0f, humidity = 0.0f, pressure = 0.0f;
        bool hasTemperature = false, hasHumidity = false, hasPressure = false;
        if (WX_Utils::getEnvironment(temperature, hasTemperature, humidity, hasHumidity, pressure, hasPressure)) {
            if (hasTemperature) doc["temp"]  = temperature;
            if (hasHumidity)    doc["hum"]   = humidity;
            if (hasPressure)    doc["press"] = pressure;
        }
        const float qnh = BARO_Utils::getQnh();
        if (qnh > 0.0f) doc["qnh"] = qnh;
        if (BARO_Utils::altitudeValid()) doc["baroAlt"] = BARO_Utils::getAltitude();
        if (MAG_Utils::isHeadingValid()) doc["heading"] = MAG_Utils::heading();
        if (IMU_Utils::isReady()) {
            doc["moving"]  = IMU_Utils::movingNow();
            doc["imuTemp"] = IMU_Utils::getTemperatureC();
        }
        #ifdef FAN_CTRL_PIN
            doc["boardTemp"] = THERMAL_Utils::getTemperature();
        #endif
        sendDoc(doc);
    }

    void sendGnss() {
        JsonDocument doc;
        doc["e"] = "gnss";
        const bool fix = gps.location.isValid() && gps.location.age() < 5000;
        doc["fix"] = fix;
        if (gps.location.isValid()) {
            doc["lat"] = gps.location.lat();
            doc["lon"] = gps.location.lng();
            doc["age"] = gps.location.age();
        }
        if (gps.altitude.isValid()) doc["alt"] = gps.altitude.meters();
        doc["spd"] = GPS_Utils::getSpeedKmph();
        doc["crs"] = GPS_Utils::getCourseDeg();
        if (gps.satellites.isValid()) doc["sats"] = gps.satellites.value();
        if (gps.hdop.isValid()) doc["hdop"] = gps.hdop.hdop();
        if (gps.date.isValid() && gps.time.isValid() && gps.date.year() >= 2024) {
            char utc[24];
            snprintf(utc, sizeof(utc), "%04u-%02u-%02uT%02u:%02u:%02uZ", (unsigned)gps.date.year(), (unsigned)gps.date.month(), (unsigned)gps.date.day(),
                     (unsigned)gps.time.hour(), (unsigned)gps.time.minute(), (unsigned)gps.time.second());
            doc["utc"] = utc;
        }
        sendDoc(doc);
    }

    void fillPage(JsonObject o, const DAPNET_Utils::Entry* entry) {
        o["ric"]  = entry->ric;
        o["f"]    = entry->func;
        o["own"]  = entry->own;
        o["bad"]  = entry->corrupt;
        o["age"]  = (millis() - entry->atMs) / 1000UL;
        o["rssi"] = entry->rssi;
        o["t"]    = entry->text;
    }

    void sendDapnet() {
        JsonDocument doc;
        doc["e"] = "dapnet";
        JsonArray items = doc["items"].to<JsonArray>();
        for (uint8_t i = 0; i < DAPNET_Utils::count(); i++) {
            const DAPNET_Utils::Entry* entry = DAPNET_Utils::at(i);
            if (entry) fillPage(items.add<JsonObject>(), entry);
        }
        sendDoc(doc);
    }

    void checkDapnet() {
        const uint32_t received = DAPNET_Utils::received();
        if (received == dapnetSeen) return;
        const bool grew = received > dapnetSeen;
        dapnetSeen = received;
        if (!grew || Config.operatingMode != MODE_DAPNET) return;
        const DAPNET_Utils::Entry* entry = DAPNET_Utils::at(0);
        if (!entry) return;
        JsonDocument doc;
        doc["e"] = "page";
        fillPage(doc.as<JsonObject>(), entry);
        sendDoc(doc);
    }

    // Rotor (modo SAT): el movil recibe azimut / elevacion cuando cambian y los retransmite al rotor
    void checkRotor() {
        if (!connected || !authed || !Config.rotor.bleEvents || ROTOR_Utils::source() < 0) return;
        static uint32_t seen = 0;
        const uint32_t s = ROTOR_Utils::targetSeq();
        if (s == seen) return;
        seen = s;
        float az, el; bool tracking;
        if (!ROTOR_Utils::target(az, el, tracking)) return;
        JsonDocument doc;
        doc["e"]  = "rot";
        doc["az"] = serialized(String(az, 1));
        doc["el"] = serialized(String(el, 1));
        doc["trk"] = tracking;
        doc["src"] = ROTOR_Utils::source();
        float pit, rol;
        if (ROTOR_Utils::tiltInfo(pit, rol)) { doc["pit"] = serialized(String(pit, 1)); doc["rol"] = serialized(String(rol, 1)); }
        float absAz, hdg;
        if (ROTOR_Utils::mobileInfo(absAz, hdg)) { doc["mob"] = true; doc["abs"] = serialized(String(absAz, 1)); doc["hdg"] = serialized(String(hdg, 1)); }
        if (ROTOR_Utils::targetName()[0]) doc["name"] = ROTOR_Utils::targetName();
        if (ROTOR_Utils::targetDistKm() > 0.0) doc["km"] = (int)ROTOR_Utils::targetDistKm();
        sendDoc(doc);
    }

    // Recepcion de satelites (modo SAT, fase 2): cada linea nueva (trama, paquete LoRa o texto CW) y cada 5 s mientras se escucha
    void sendSatRx(bool withText) {
        #ifdef HAS_SAT
            JsonDocument doc;
            doc["e"]    = "satrx";
            doc["on"]   = SATRX_Utils::active();
            if (SATRX_Utils::active()) {
                doc["k"]    = SATRX_Utils::kindText(SATRX_Utils::kind());
                doc["f"]    = (long)llround(SATRX_Utils::rxHz());
                doc["dop"]  = SATRX_Utils::dopplerHz();
                doc["fine"] = SATRX_Utils::fineHz();
                doc["rssi"] = (int)lroundf(SATRX_Utils::rssi());
                doc["n"]    = SATRX_Utils::frames();
                doc["tx"]   = SATRX_Utils::txIndex();
            }
            char line[112];
            if (withText && SATRX_Utils::logLine(0, line, sizeof(line))) doc["txt"] = line;
            sendDoc(doc);
        #else
            (void)withText;
        #endif
    }

    void checkSatRx() {
        #ifdef HAS_SAT
            if (!connected || !authed) return;
            static uint32_t seen = 0, lastMs = 0, lastLogged = 0;
            static bool wasOn = false;
            const bool on = SATRX_Utils::active();
            const uint32_t now = millis();
            const uint32_t s = SATRX_Utils::eventSeq();
            if (s != seen && now - lastMs >= 300) {
                seen = s; lastMs = now;
                const uint32_t n = SATRX_Utils::frames();
                sendSatRx(n != lastLogged);
                lastLogged = n;
            } else if (on != wasOn || (on && now - lastMs >= 5000)) {
                lastMs = now;
                sendSatRx(false);
            }
            wasOn = on;
        #endif
    }

    void sendTargets() {
        JsonDocument doc;
        doc["e"]   = "targets";
        doc["sel"] = Config.rotor.targetIndex;
        doc["src"] = Config.rotor.source;
        JsonArray items = doc["items"].to<JsonArray>();
        for (int i = 0; i < RotorConfig::TARGETS; i++) {
            const RotorConfig::Target& t = Config.rotor.targets[i];
            if (t.name.isEmpty()) continue;
            JsonObject o = items.add<JsonObject>();
            o["i"] = i; o["n"] = t.name; o["lat"] = t.lat; o["lon"] = t.lon; o["alt"] = t.alt; o["f"] = t.freqHz;
        }
        sendDoc(doc);
    }

    // ---------------------------------------------------------------- actualizacion del firmware desde la app

    void otaStop() {
        otaActive = false;
        if (otaStream) { vStreamBufferDelete(otaStream); otaStream = nullptr; }
    }

    void otaFail(const String& error) {
        FW_Update::bleAbort();
        otaStop();
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "BLE Ctrl", "Firmware update failed: %s", error.c_str());
        displayPopup("Firmware update", "Failed", error.substring(0, 21), -1, 2000);
        reply("ota", -1, error.c_str());
    }

    void otaBegin(JsonDocument& doc, int id) {
        const size_t size   = doc["size"] | 0;
        const char*  sha    = doc["sha"] | "";
        String error;
        if (otaActive) { FW_Update::bleAbort(); otaStop(); }
        if (!FW_Update::bleBegin(size, sha, error)) { reply("ota", id, error.c_str()); return; }
        otaStream = xStreamBufferCreate(OTA_BUFFER, 1);
        if (otaStream == nullptr) { FW_Update::bleAbort(); reply("ota", id, "Not enough memory"); return; }
        otaSize = size; otaReported = 0; otaOverflow = false; otaLastMs = millis(); otaLastPct = -10;
        otaActive = true;
        displayPopup("Firmware update", "From the app", "Do not power off", 0);
        JsonDocument ok;
        ok["e"] = "ok"; ok["c"] = "ota"; if (id >= 0) ok["id"] = id;
        ok["block"] = OTA_BLOCK; ok["buffer"] = OTA_BUFFER;
        sendDoc(ok);
    }

    void otaPoll() {
        static uint8_t buf[2048];
        const uint32_t now = millis();
        String error;
        size_t n;
        while (otaActive && (n = xStreamBufferReceive(otaStream, buf, sizeof(buf), 0)) > 0) {
            if (!FW_Update::bleWrite(buf, n, error)) { otaFail(error); return; }
            otaLastMs = now;
        }
        if (!otaActive) return;
        if (otaOverflow) { otaFail("Data lost: the phone sent too fast"); return; }
        const size_t written = FW_Update::bleWritten();
        if (written - otaReported >= OTA_BLOCK || (written == otaSize && otaReported != written)) {
            otaReported = written;
            JsonDocument doc;
            doc["e"] = "ota"; doc["off"] = (uint32_t)written;
            sendDoc(doc);
            const int pct = (int)((written * 100ULL) / otaSize);
            if (pct >= otaLastPct + 5) { otaLastPct = pct; displayPopup("Firmware update", "From the app " + String(pct) + "%", "Do not power off", pct); }
        }
        if (written == otaSize) {
            if (!FW_Update::bleFinish(error)) { otaStop(); reply("ota", -1, error.c_str()); displayPopup("Firmware update", "Failed", error.substring(0, 21), -1, 2000); return; }
            otaStop();
            JsonDocument doc;
            doc["e"] = "ota"; doc["off"] = (uint32_t)written; doc["done"] = true;
            sendDoc(doc);
            displayPopup("Firmware update", "Installed", "Restarting...", 100);
            restartAtMs = millis() + 1500;
            return;
        }
        if (now - otaLastMs > OTA_TIMEOUT_MS) otaFail("No data from the phone");
    }

    void sendFwStatus() {
        JsonDocument doc;
        if (deserializeJson(doc, FW_Update::onlineStatus()) != DeserializationError::Ok) doc.clear();
        doc["e"] = "fw";
        sendDoc(doc);
    }

    // ---------------------------------------------------------------- ordenes

    void setMode(int mode, const char* cmd, int id) {
        if (!modeAvailable(mode)) { reply(cmd, id, "mode not available"); return; }
        if (aprsMode(mode) && mode != Config.savedMode) {
            Config.savedMode = mode;                                    // los modos APRS quedan como modo de arranque, como con el boton
            Config.writeFile();
        }
        bootModeValue = mode;                                           // el menu de arranque lo usa sin preguntar
        bootModeMagic = BOOT_MODE_MAGIC;
        reply(cmd, id);
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "BLE Ctrl", "Mode %d requested: restarting", mode);
        restartAtMs = millis() + 500;                                   // deja salir la respuesta
    }

    void setProfile(int index, const char* cmd, int id) {
        if (index < 0 || index >= myBeaconsSize) { reply(cmd, id, "no such profile"); return; }
        myBeaconsIndex      = (uint8_t)index;                           // lo mismo que el cambio de indicativo del menu
        currentBeacon       = &Config.beacons[myBeaconsIndex];
        statusUpdate        = true;
        statusTime          = millis();
        winlinkCommentState = false;
        sendStartTelemetry  = true;
        STATION_Utils::saveIndex(0, myBeaconsIndex);
        displayShow("   INFO", "", "  CHANGING CALLSIGN!", "-----> " + currentBeacon->callsign, "", "", 1000);
        reply(cmd, id);
    }

    void setLora(int index, const char* cmd, int id) {
        if (!aprsMode(Config.operatingMode)) { reply(cmd, id, "only in APRS modes"); return; }
        if (index < 0 || index >= loraIndexSize) { reply(cmd, id, "no such LoRa profile"); return; }
        if (!LoRa_Utils::selectLoraIndex((uint8_t)index)) { reply(cmd, id, "fixed by the AIS profile"); return; }
        STATION_Utils::saveIndex(1, loraIndex);
        reply(cmd, id);
    }

    void handle(const char* text) {
        JsonDocument doc;
        if (deserializeJson(doc, text) != DeserializationError::Ok) { reply("?", -1, "bad JSON"); return; }
        const char* cmd = doc["c"] | "";
        const int id    = doc["id"] | -1;

        if      (strcmp(cmd, "info") == 0)    sendInfo();
        else if (strcmp(cmd, "status") == 0)  sendStatus();
        else if (strcmp(cmd, "sensors") == 0) sendSensors();
        else if (strcmp(cmd, "gnss") == 0)    sendGnss();
        else if (strcmp(cmd, "dapnet") == 0)  sendDapnet();
        else if (strcmp(cmd, "sub") == 0) {
            const int s = doc["sensors"] | -1;
            const int g = doc["gnss"] | -1;
            if (s >= 0) sensorsEveryMs = s == 0 ? 0 : (uint32_t)max(s, 2) * 1000UL;
            if (g >= 0) gnssEveryMs    = g == 0 ? 0 : (uint32_t)max(g, 1) * 1000UL;
            reply(cmd, id);
        }
        else if (strcmp(cmd, "att") == 0) {                       // inclinometro del soporte del rotor: p = cabeceo (+ morro arriba), r = alabeo (+ lado derecho abajo), grados; repetir cada <2 s
            if (doc["p"].isNull() || doc["r"].isNull()) { reply(cmd, id, "p and r needed"); return; }
            ROTOR_Utils::setAttitude(doc["p"] | 0.0f, doc["r"] | 0.0f);
        }
        else if (strcmp(cmd, "satrx") == 0) {                     // receptor de satelites (modo SAT): on = true/false, tx = indice del transmisor (opcional), fine = ajuste en Hz (opcional)
            #ifdef HAS_SAT
                if (Config.operatingMode != MODE_SAT) { reply(cmd, id, "not in SAT mode"); return; }
                if (!doc["fine"].isNull()) SATRX_Utils::fine((int)(doc["fine"] | 0));
                if (!doc["on"].isNull()) {
                    if (doc["on"] | false) {
                        if (!SATRX_Utils::start((uint8_t)(doc["tx"] | 0))) { reply(cmd, id, SATRX_Utils::message()); return; }
                    } else {
                        SATRX_Utils::stop();
                    }
                }
                reply(cmd, id);
                sendSatRx(false);
            #else
                reply(cmd, id, "not available");
            #endif
        }
        else if (strcmp(cmd, "sattx") == 0) {                     // emision hacia el satelite (modo SAT): tx = indice del transmisor, txt = mensaje (si falta, el de la WebUI). Bloquea mientras emite
            #ifdef HAS_SAT
                if (Config.operatingMode != MODE_SAT) { reply(cmd, id, "not in SAT mode"); return; }
                String text = doc["txt"] | "";
                text.trim();
                if (text.isEmpty()) text = Config.sat.txText;
                if (text.length() > 64) text = text.substring(0, 64);
                const uint8_t txi = (uint8_t)(doc["tx"] | 0);
                if (!SATTX_Utils::send(txi, text.c_str())) { reply(cmd, id, SATTX_Utils::message()); return; }
                reply(cmd, id);
                JsonDocument ev;
                ev["e"]  = "sattx";
                ev["ok"] = true;
                ev["f"]  = (long)llround(SATTX_Utils::lastHz());
                ev["n"]  = SATTX_Utils::count();
                sendDoc(ev);
            #else
                reply(cmd, id, "not available");
            #endif
        }
        else if (strcmp(cmd, "tgtlist") == 0) sendTargets();
        else if (strcmp(cmd, "tgt") == 0) {                       // objetivo fijo para el rotor (repetidor, baliza...): i, n, lat, lon, alt, f | del | sel | src
            #ifdef HAS_SAT
                bool changed = false;
                if (!doc["del"].isNull()) {
                    const int d = doc["del"] | -1;
                    if (d < 0 || d >= RotorConfig::TARGETS) { reply(cmd, id, "bad slot"); return; }
                    Config.rotor.targets[d] = RotorConfig::Target{ "", 0.0, 0.0, 0, 0 };
                    changed = true;
                }
                if (!doc["n"].isNull()) {
                    if (doc["lat"].isNull() || doc["lon"].isNull()) { reply(cmd, id, "lat and lon needed"); return; }
                    const double lat = doc["lat"] | 0.0, lon = doc["lon"] | 0.0;
                    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) { reply(cmd, id, "bad position"); return; }
                    int slot = doc["i"] | -1;
                    if (slot < 0) {                                                           // sin hueco pedido: el primero libre; si no hay, el elegido
                        slot = Config.rotor.targetIndex;
                        for (int k = 0; k < RotorConfig::TARGETS; k++) if (Config.rotor.targets[k].name.isEmpty()) { slot = k; break; }
                    }
                    if (slot >= RotorConfig::TARGETS) { reply(cmd, id, "bad slot"); return; }
                    String name = doc["n"] | "";
                    name.trim();
                    if (name.length() > 20) name = name.substring(0, 20);
                    if (name.isEmpty()) { reply(cmd, id, "name needed"); return; }
                    Config.rotor.targets[slot] = RotorConfig::Target{ name, lat, lon, doc["alt"] | 0, (long)(doc["f"] | 0L) };
                    if (doc["sel"] | false) { Config.rotor.targetIndex = slot; Config.rotor.source = 2; }
                    changed = true;
                } else if (!doc["sel"].isNull()) {
                    const int s = doc["sel"] | -1;
                    if (s < 0 || s >= RotorConfig::TARGETS || Config.rotor.targets[s].name.isEmpty()) { reply(cmd, id, "bad slot"); return; }
                    Config.rotor.targetIndex = s; Config.rotor.source = 2;
                    changed = true;
                }
                if (!doc["src"].isNull()) { Config.rotor.source = constrain((int)(doc["src"] | 0), 0, 2); changed = true; }
                if (changed) Config.writeFile();
                reply(cmd, id);
                sendTargets();
            #else
                reply(cmd, id, "not available");
            #endif
        }
        else if (strcmp(cmd, "pos") == 0) {                       // posicion del movil para el modo SAT: lat, lon, alt (m), acc (m)
            if (doc["lat"].isNull() || doc["lon"].isNull()) { reply(cmd, id, "lat and lon needed"); return; }
            SAT_Utils::setPhonePosition(doc["lat"] | 0.0, doc["lon"] | 0.0, doc["alt"] | 0.0, doc["acc"] | 0.0f);
            reply(cmd, id);
        }
        else if (strcmp(cmd, "mode") == 0)    setMode(doc["m"] | -1, cmd, id);
        else if (strcmp(cmd, "profile") == 0) setProfile(doc["i"] | -1, cmd, id);
        else if (strcmp(cmd, "lora") == 0)    setLora(doc["i"] | -1, cmd, id);
        else if (strcmp(cmd, "beacon") == 0) {
            if (!aprsMode(Config.operatingMode)) { reply(cmd, id, "only in APRS modes"); return; }
            #ifdef HAS_APRS_PAGES
                PAGES_Utils::requestBeacon();                           // tambien en modo de baliza manual
            #endif
            sendUpdate = true;                                          // sale con la siguiente posicion del GPS
            reply(cmd, id);
        }
        else if (strcmp(cmd, "digi") == 0) {
            digipeaterActive = doc["on"] | false;
            reply(cmd, id);
        }
        else if (strcmp(cmd, "cw") == 0) {
            #ifdef HAS_APRS_PAGES
                if (Config.operatingMode != MODE_MORSE) { reply(cmd, id, "only in MORSE mode"); return; }
                const int index = doc["i"] | -1;
                const char* freeText = doc["t"] | "";
                if (index >= 0 && index < CW_MESSAGE_COUNT) cwIndex = index;
                else if (strlen(freeText) > 0 && strlen(freeText) <= 60) snprintf(cwText, sizeof(cwText), "%s", freeText);
                else { reply(cmd, id, "bad message"); return; }
                reply(cmd, id);                                         // se emite en la vuelta siguiente: la emision bloquea el bucle
            #else
                reply(cmd, id, "not available");
            #endif
        }
        else if (strcmp(cmd, "ota") == 0)       otaBegin(doc, id);
        else if (strcmp(cmd, "ota_abort") == 0) { if (otaActive) { FW_Update::bleAbort(); otaStop(); } reply(cmd, id); }
        else if (strcmp(cmd, "fwstatus") == 0)  sendFwStatus();
        else if (strcmp(cmd, "fwcheck") == 0 || strcmp(cmd, "fwinstall") == 0) {
            if (FW_Update::onlineStart(strcmp(cmd, "fwinstall") == 0)) reply(cmd, id);
            else reply(cmd, id, Config.fwUpdate.ssid.isEmpty() ? "Set the tracker's WiFi (WebUI: Firmware update) first" : "Busy or not available on this board");
        }
        else if (strcmp(cmd, "kcfg") == 0) {
            const int d = doc["delay"] | -1;
            if (d < 0 || d > KEY_DELAY_MAX_MS) { reply(cmd, id, "delay 0-1000 ms"); return; }
            keyDelay = (uint16_t)d;
            reply(cmd, id);
        }
        else if (strcmp(cmd, "reboot") == 0) {
            reply(cmd, id);
            restartAtMs = millis() + 500;
        }
        else reply(cmd, id, "unknown command");
    }

    // ---------------------------------------------------------------- callbacks de NimBLE

    // Evento de manipulador {"c":"k","t":<ms>,"d":0|1}: va directo a su cola, sin pasar por el bucle principal, que esta
    // bloqueado en la sesion de manipulacion mientras se transmite. Devuelve false si la linea no es un evento de manipulador
    bool keyFastPath(const char* line) {
        if (strncmp(line, "{\"c\":\"k\",", 9) != 0) return false;
        const char* t = strstr(line, "\"t\":");
        const char* d = strstr(line, "\"d\":");
        if (t == nullptr || d == nullptr || keyQueue == nullptr) return true;
        BLE_Control::KeyEvent event;
        event.t         = (uint32_t)strtoul(t + 4, nullptr, 10);
        event.down      = d[4] == '1';
        event.arrivalMs = millis();
        xQueueSend(keyQueue, &event, 0);                                // cola llena: se pierde (el tracker suelta la portadora solo)
        return true;
    }

    class CommandCallbacks : public NimBLECharacteristicCallbacks {
        void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo) override {
            NimBLEAttValue value = characteristic->getValue();
            const uint8_t* data   = value.data();
            const size_t   length = value.length();                     // uint16_t: se copia a size_t (-Wsign-compare)
            for (size_t i = 0; i < length; i++) {
                const char c = (char)data[i];
                if (c == '\n') {
                    if (!rxOverflow && rxLen > 0 && queue != nullptr) {
                        Line line;
                        memcpy(line.text, rxBuf, rxLen);
                        line.text[rxLen] = '\0';
                        if (!keyFastPath(line.text)) xQueueSend(queue, &line, 0);   // cola llena: se pierde la orden (el movil reintenta)
                    }
                    rxLen = 0;
                    rxOverflow = false;
                } else if (c != '\r') {
                    if (rxLen < CONTROL_LINE_MAX - 1) rxBuf[rxLen++] = c;
                    else rxOverflow = true;
                }
            }
        }
    };

    // Datos de la imagen: van a un buffer y el bucle principal los escribe en la flash. El movil espera la confirmacion de
    // cada bloque de 4 KB y nunca deja mas de 2 sin confirmar, asi que el buffer de 16 KB no se llena
    class OtaCallbacks : public NimBLECharacteristicCallbacks {
        void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo) override {
            if (!otaActive || otaStream == nullptr) return;
            NimBLEAttValue value = characteristic->getValue();
            const size_t length = value.length();
            if (xStreamBufferSend(otaStream, value.data(), length, 0) != length) otaOverflow = true;
        }
    };

    class NmeaCallbacks : public NimBLECharacteristicCallbacks {
        void onSubscribe(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo, uint16_t subValue) override {
            nmeaWanted = subValue != 0;
        }
    };

}

namespace BLE_Control {

    void setup(NimBLEServer* server) {
        if (server == nullptr) return;
        queue    = xQueueCreate(CONTROL_QUEUE_LEN, sizeof(Line));
        keyQueue = xQueueCreate(KEY_QUEUE_LEN, sizeof(BLE_Control::KeyEvent));
        NimBLEService* service = server->createService(CONTROL_SERVICE_UUID);
        cmdChar  = service->createCharacteristic(CONTROL_CMD_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN);
        // Seguridad (EA7JRS): escribir ordenes / OTA exige enlace cifrado y emparejado con PIN; leer "eventos" sirve a la app para
        // provocar el emparejamiento. Las notificaciones ademas se retienen hasta completarse (ver authed)
        evtChar  = service->createCharacteristic(CONTROL_EVT_UUID, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN);
        nmeaChar = service->createCharacteristic(CONTROL_NMEA_UUID, NIMBLE_PROPERTY::NOTIFY);
        otaChar  = service->createCharacteristic(CONTROL_OTA_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN);
        otaChar->setCallbacks(new OtaCallbacks());
        cmdChar->setCallbacks(new CommandCallbacks());
        nmeaChar->setCallbacks(new NmeaCallbacks());
        dapnetSeen = DAPNET_Utils::received();
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "BLE Ctrl", "Control service ready (protocol %d)", CONTROL_PROTOCOL);
    }

    void onConnect() {
        connected = true;
        authed    = false;
        mtu       = 23;
    }

    void onSecurity(bool authenticated) {
        authed = authenticated;
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "BLE Ctrl", "Pairing %s", authenticated ? "OK" : "not authenticated");
    }

    bool authenticated() { return authed; }

    void onMtu(uint16_t newMtu) {
        mtu = newMtu;
    }

    void onDisconnect() {
        connected      = false;
        authed         = false;
        if (otaActive) { FW_Update::bleAbort(); otaStop(); displayPopup("Firmware update", "Cancelled", "Link lost", -1, 1500); }
        keyDelay       = KEY_DELAY_DEFAULT_MS;
        nmeaWanted     = false;
        sensorsEveryMs = 0;
        gnssEveryMs    = 0;
    }

    void loop() {
        if (restartAtMs != 0 && (int32_t)(millis() - restartAtMs) >= 0) ESP.restart();
        if (queue == nullptr) return;
        Line line;
        while (xQueueReceive(queue, &line, 0) == pdTRUE) handle(line.text);
        if (keyEventWaiting()) {
            #ifdef HAS_APRS_PAGES
                if (Config.operatingMode == MODE_MORSE && LoRa_Utils::cwAvailable()) {
                    if (!Config.cw.keyTx) reply("k", -1, "key TX is off in the WebUI (CW): sidetone only");
                    PAGES_Utils::cwRemoteSession();                     // bloquea mientras dure la manipulacion
                } else {
                    xQueueReset(keyQueue);
                    if (!keyRejected) reply("k", -1, "remote key only in MORSE mode");
                    keyRejected = true;
                }
            #else
                xQueueReset(keyQueue);
                if (!keyRejected) reply("k", -1, "not available");
                keyRejected = true;
            #endif
        } else {
            keyRejected = false;
        }
        #ifdef HAS_APRS_PAGES
            if (cwIndex >= 0) { const uint8_t index = (uint8_t)cwIndex; cwIndex = -1; PAGES_Utils::sendCw(index); }
            if (cwText[0])    { String text = cwText; cwText[0] = '\0'; PAGES_Utils::cwSendText(text.c_str()); }
        #endif
        if (otaActive) { otaPoll(); return; }                         // durante la actualizacion no se envia nada mas
        checkDapnet();
        checkRotor();
        checkSatRx();
        if (!connected) return;
        const uint32_t now = millis();
        if (sensorsEveryMs && now - sensorsLastMs >= sensorsEveryMs) { sensorsLastMs = now; sendSensors(); }
        if (gnssEveryMs && now - gnssLastMs >= gnssEveryMs)          { gnssLastMs = now; sendGnss(); }
    }

    void nmea(const char* text, size_t length) {
        if (!connected || !authed || !nmeaWanted || nmeaChar == nullptr || length == 0) return;
        const size_t chunk = chunkSize();
        for (size_t i = 0; i < length; i += chunk) {
            const size_t n = (length - i < chunk) ? (length - i) : chunk;
            nmeaChar->setValue((const uint8_t*)text + i, n);
            nmeaChar->notify();
        }
    }

    bool takeKeyEvent(KeyEvent& event) {
        return keyQueue != nullptr && xQueueReceive(keyQueue, &event, 0) == pdTRUE;
    }

    bool keyEventWaiting() {
        return keyQueue != nullptr && uxQueueMessagesWaiting(keyQueue) > 0;
    }

    uint16_t keyDelayMs() { return keyDelay; }

    bool linkUp() { return connected; }

    int takeBootMode() {
        if (bootModeMagic != BOOT_MODE_MAGIC) return -1;
        bootModeMagic = 0;
        if (bootModeValue < MODE_APRS_LORA || bootModeValue > MODE_FST4W) return -1;
        return (int)bootModeValue;
    }

}
