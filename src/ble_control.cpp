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
#include "gnss_sky.h"
#include "configuration.h"
#include "board_pinout.h"
#include "display.h"
#include "battery_utils.h"
#include "station_utils.h"
#include "dapnet_utils.h"
#include "sat_config.h"
#include "sat_utils.h"
#include "net_wx.h"
#include "ais_utils.h"
#ifdef HAS_SA868
#include "tx_unlock.h"
#endif
#include "rotor_utils.h"
#include "rotor_follow.h"
#include "sat_rx.h"
#include "sat_tx.h"
#include "baro_utils.h"
#include "lora_utils.h"
#include "bcast_radio.h"
#include "notification_utils.h"
#include "msg_utils.h"
#ifdef HAS_SA868
    #include "sa868_radio.h"
    #include "fm_utils.h"
    #include "fm_core.h"
#endif
#include "gps_utils.h"
#include "imu_utils.h"
#include "mag_utils.h"
#include "wx_utils.h"
#include "fw_update.h"
#include "power_utils.h"
#include "sleep_utils.h"
#include <SPIFFS.h>
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
extern bool             displayState;

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
    uint8_t                 ledSent         = 0xFF;            // luces de la app: ultimo estado enviado (0xFF = enviar en la proxima vuelta)
    volatile bool           cwRxStop        = false;           // la app pide terminar la escucha CW
    volatile bool           nmeaWanted      = false;
    volatile uint16_t       mtu             = 23;

    // Montaje de lineas (solo en la tarea de NimBLE)
    char                    rxBuf[CONTROL_LINE_MAX];
    size_t                  rxLen           = 0;
    bool                    rxOverflow      = false;

    // Envios periodicos y tareas aplazadas (solo en el bucle principal)
    uint32_t                sensorsEveryMs  = 0, sensorsLastMs = 0;
    uint32_t                gnssEveryMs     = 0, gnssLastMs    = 0;
    uint32_t                skyEveryMs      = 0, skyLastMs     = 0;
    uint32_t                restartAtMs     = 0;
    uint32_t                dapnetSeen      = 0;
    int                     cwIndex         = -1;
    char                    cwText[64]      = "";

    bool aprsMode(int mode) { return isAprsMode(mode); }

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

    // Escritor JSON minimo para los eventos grandes (la plantilla de ArduinoJson ocupa mucha mas flash por cada campo) (Manuel - EA7JRS)
    struct JW {
        String s;
        bool   first = true;
        JW(const char* event) { s = "{\"e\":\""; s += event; s += '"'; }
        void key(const char* k) {
            const char last = s[s.length() - 1];
            if (last != '{' && last != '[') s += ',';
            s += '"'; s += k; s += "\":";
        }
        void esc(const char* v) {
            s += '"';
            for (; *v; v++) {
                const unsigned char c = (unsigned char)*v;
                if (c == '"' || c == '\\') { s += '\\'; s += (char)c; }
                else if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); s += b; }
                else s += (char)c;
            }
            s += '"';
        }
        void text(const char* k, const char* v) { key(k); esc(v); }
        void itemText(const char* v) { sep(); esc(v); }
        void num(const char* k, long long v) { key(k); char b[24]; snprintf(b, sizeof(b), "%lld", v); s += b; }
        void unum(const char* k, uint32_t v) { key(k); s += (unsigned long)v; }
        void flt(const char* k, double v, int decimals = 2) {
            if (isnan(v) || isinf(v)) return;
            key(k);
            char b[24];
            snprintf(b, sizeof(b), "%.*f", decimals, v);
            s += b;
        }
        void flag(const char* k, bool v) { key(k); s += v ? "true" : "false"; }
        void raw(const char* k, const char* v) { key(k); s += v; }
        void arr(const char* k) { key(k); s += '['; first = true; }
        void sep() { if (!first) s += ','; first = false; }
        void endArr() { s += ']'; }
        void obj() { sep(); s += '{'; }                        // objeto dentro de una matriz
        void objKey(const char* k) { key(k); s += '{'; }       // objeto con nombre
        void endObj() { s += '}'; }
        void item(double v, int decimals = 3) { sep(); char b[24]; snprintf(b, sizeof(b), "%.*f", decimals, v); s += b; }
        void send(void (*out)(const String&)) { s += "}\n"; out(s); }
    };

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
        out[n++] = ModeOption{ MODE_APRS_LORA, APRS_MAIN_MODE_NAME };
        if (LoRa_Utils::fskAvailable()) out[n++] = ModeOption{ MODE_APRS_FSK, "APRS FSK 9600" };
        if (LoRa_Utils::m17Available()) out[n++] = ModeOption{ MODE_M17, "M17" };
        #ifdef HAS_APRS_PAGES
            if (LoRa_Utils::cwAvailable()) out[n++] = ModeOption{ MODE_MORSE,  "MORSE / CW" };
            if (LoRa_Utils::wsprAvailable()) {
                out[n++] = ModeOption{ MODE_WSPR,   "WSPR beacon" };
                out[n++] = ModeOption{ MODE_FT8,    "FT8 beacon" };
                out[n++] = ModeOption{ MODE_FT4,    "FT4 beacon" };
                out[n++] = ModeOption{ MODE_FST4W,  "FST4W beacon" };
            }
            if (Config.lrfhss.enabled && LoRa_Utils::lrfhssAvailable()) out[n++] = ModeOption{ MODE_LRFHSS, "LR-FHSS beacon" };
            if (LoRa_Utils::dapnetAvailable()) out[n++] = ModeOption{ MODE_DAPNET, "DAPNET pager" };
            if (LoRa_Utils::aisAvailable()) out[n++] = ModeOption{ MODE_AIS, "AIS receiver" };
            #ifdef HAS_SAT
            out[n++] = ModeOption{ MODE_SAT, "Satellites" };
            #endif
        #endif
        return n;
    }

    bool modeAvailable(int mode) {
        ModeOption options[16];
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
            caps["gpsaid"] = GPS_Utils::gpsModule() == 1;        // A-GPS desde la app (orden "gpsaid", receptor u-blox)
            caps["rotor"]  = ROTOR_Utils::enabled();             // rotor activado en la WebUI (ordenes "rotext", "att", "tgt")
            caps["ota"]    = FW_Update::slotAvailable();         // actualizacion por BLE (orden "ota")
        #else
            caps["rkey"]   = false;
            caps["gpsaid"] = GPS_Utils::gpsModule() == 1;
            caps["rotor"]  = ROTOR_Utils::enabled();
            caps["ota"]    = FW_Update::slotAvailable();
            caps["dapnet"] = false;
            caps["cw"]     = false;
            caps["modes"]  = false;
        #endif
        #ifdef HAS_SA868                                         // T-TWR: radio FM SA868 (APRS AFSK 1200); ajustes en el grupo "sa868" (cfgget/cfgset)
            caps["radio"] = "sa868";
            JsonObject sa = doc["sa868"].to<JsonObject>();
            sa["rev"]  = SA868::revision();
            sa["fw"]   = SA868::firmwareName();
            sa["band"] = SA868::uhf() ? "UHF" : "VHF";
            sa["ok"]   = SA868::ready();
            sa["modem"] = SA868::modemName();
            sa["rx"]   = SA868::rxFreqHz();
            sa["tx"]   = SA868::txFreqHz();
            sa["m17"]  = SA868::m17FreqHz();
            caps["m17"] = LoRa_Utils::m17Available();             // orden "m17sms" (en el modo M17)
            caps["fm"]  = LoRa_Utils::fmAvailable();              // modo FM de voz: ordenes "fm*", "qsy", "carrier"
            sa["dapnet"] = SA868::dapnetFreqHz();
            sa["spkEsp"] = SA868::fmSpeakerFromEsp();
            sa["txul"]   = TX_Unlock::unlocked();                 // rango de emision desbloqueado (codigo del equipo)
            sa["txulav"] = TX_Unlock::available();
            sa["mac"]    = TX_Unlock::deviceId();
        #else
            caps["radio"] = "lora";
            caps["m17"]   = false;
            caps["fm"]    = false;
        #endif
        #ifdef HAS_SAT
            caps["satMax"] = (long)SAT_Utils::maxDataBytes();       // bytes del archivo de satelites que caben (ordenes "satdata"; la app filtra por grupos)
            caps["satFav"] = true;                                   // favoritos y categorias ("satfav", "satcat")
            #ifdef HAS_SA868
                caps["satFm"] = true;                                // el VFO de FM sigue al satelite (FM / APRS AFSK, ruta sat.aprsPath)
            #endif
        #endif
        caps["ais"] = true;                                      // receptor AIS: eventos "ais", ordenes "aislist", "aisin", "aisset" (modo 12 en la T-TWR, modulo por UART)
        caps["netreq"] = true;                                   // el tracker puede pedir a la app que baje el tiempo o los satelites (evento "netreq")
        caps["wxnet"] = true;                                    // tiempo de internet de la app para TELEMETRY (orden "wxnet")
        caps["bcast"] = BCAST::chipName();                       // radio comercial por I2C: "none", "RDA5807M", "Si4703", "Si4735"
        doc["mode"] = Config.operatingMode;
        JsonArray modes = doc["modes"].to<JsonArray>();
        ModeOption options[16];
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

    // Frecuencia (Hz) y modulacion de la radio en el modo en curso, para el dial de la app: en MORSE el perfil CW Beacon o Telegrafia,
    // en DAPNET su frecuencia, en WSPR / FT8 / FT4 / FST4W el dial de su perfil, en SAT la del receptor de satelites (0 = sin datos)
    void radioState(unsigned long& hz, const char*& mod) {
        hz = 0; mod = "";
        const long live = LoRa_Utils::liveFreqHz();
        switch (Config.operatingMode) {
            #ifdef HAS_SA868
            case MODE_APRS_LORA: hz = live; mod = "AFSK"; break;
            #else
            case MODE_APRS_LORA: hz = live; mod = "LoRa"; break;
            #endif
            case MODE_APRS_FSK:  hz = live; mod = "FSK"; break;
            case MODE_MORSE:     hz = live; mod = "CW"; break;
            case MODE_DAPNET:    hz = live; mod = "POCSAG"; break;
            case MODE_M17:       hz = live; mod = "M17"; break;
            case MODE_FM:        hz = live; mod = "FM"; break;
            case MODE_WSPR:      hz = Config.wspr.freqHz > 1500 ? Config.wspr.freqHz - 1500 : 0; mod = "WSPR"; break;    // dial (la central es dial + 1500 Hz)
            case MODE_FT8:       hz = Config.ft.ft8FreqHz; mod = "FT8"; break;
            case MODE_FT4:       hz = Config.ft.ft4FreqHz; mod = "FT4"; break;
            case MODE_FST4W:     hz = Config.ft.fstFreqHz; mod = "FST4W"; break;
            case MODE_LRFHSS:    hz = Config.lrfhss.freqHz; mod = "LR-FHSS"; break;
            case MODE_SAT:       hz = SATRX_Utils::active() ? (unsigned long)SATRX_Utils::rxHz() : 0; mod = "SAT"; break;
            case MODE_AIS:       hz = 161975000UL; mod = "AIS"; break;
            default: break;
        }
    }

    void sendStatus() {
        JW j("status");
        j.num("mode", Config.operatingMode);
        j.num("profile", myBeaconsIndex);
        j.num("lora", loraIndex);
        j.flag("digi", digipeaterActive);
        j.flag("smart", smartBeaconActive);
        j.flag("sos", sosActive);
        const float v = batteryVolts();
        if (v > 0.0f) j.flt("batt", v);
        j.unum("up", millis() / 1000UL);
        j.flag("bleKiss", Config.bluetooth.useKISS);
        unsigned long hz; const char* mod;
        radioState(hz, mod);
        if (hz > 0) j.unum("f", hz);
        if (mod[0]) j.text("mod", mod);
        j.send(sendLine);
    }

    void sendSensors() {
        JW j("sensors");
        const float v = batteryVolts();
        if (v > 0.0f) j.flt("batt", v);
        float temperature = 0.0f, humidity = 0.0f, pressure = 0.0f;
        bool hasTemperature = false, hasHumidity = false, hasPressure = false;
        if (WX_Utils::getEnvironment(temperature, hasTemperature, humidity, hasHumidity, pressure, hasPressure)) {
            if (hasTemperature) j.flt("temp", temperature);
            if (hasHumidity)    j.flt("hum", humidity);
            if (hasPressure)    j.flt("press", pressure);
        }
        const float qnh = BARO_Utils::getQnh();
        if (qnh > 0.0f) { j.flt("qnh", qnh); j.text("qs", BARO_Utils::isManualQnh() ? "man" : BARO_Utils::isNetQnh() ? "net" : "gps"); }
        {   // tiempo de internet que mando la app (lo ven tambien la pantalla APRS LoRa+ Display y otras apps conectadas)
            NET_WX::Data nd;
            if (NET_WX::get(nd)) {
                j.objKey("net");
                j.text("src", nd.src);
                if (nd.place[0]) j.text("pl", nd.place);
                if (nd.desc[0]) j.text("ds", nd.desc);
                if (nd.hasT) j.flt("t", nd.tempC, 1);
                if (nd.hasH) j.flt("h", nd.humidity, 0);
                if (nd.hasP) j.flt("p", nd.pressureHpa, 1);
                if (nd.hasWind) j.flt("ws", nd.windMs, 1);
                if (nd.hasGust) j.flt("wg", nd.gustMs, 1);
                if (nd.windDeg >= 0) j.num("wd", nd.windDeg);
                if (nd.hasRain) j.flt("r", nd.rainMmH, 1);
                if (nd.hasClouds) j.num("cl", nd.clouds);
                if (nd.hasPm25) j.flt("pm25", nd.pm25, 1);
                if (nd.hasPm10) j.flt("pm10", nd.pm10, 1);
                if (nd.aqiOwm > 0) j.num("aq", nd.aqiOwm);
                if (nd.aqiUs >= 0) j.num("aqus", nd.aqiUs);
                if (nd.pollutant[0]) j.text("mp", nd.pollutant);
                j.unum("age", NET_WX::ageS());
                char fc[20]; float d3 = NAN;
                if (NET_WX::forecast(fc, sizeof(fc), &d3)) { j.text("fc", fc); if (d3 == d3) j.flt("d3", d3, 1); }   // pronostico y tendencia 3 h
                j.endObj();
            }
        }
        if (BARO_Utils::altitudeValid()) j.flt("baroAlt", BARO_Utils::getAltitude(), 1);
        if (MAG_Utils::isHeadingValid()) j.flt("heading", MAG_Utils::heading(), 1);
        if (IMU_Utils::isReady()) {
            j.flag("moving", IMU_Utils::movingNow());
            j.flt("imuTemp", IMU_Utils::getTemperatureC(), 1);
        }
        #ifdef FAN_CTRL_PIN
            j.flt("boardTemp", THERMAL_Utils::getTemperature(), 1);
        #endif
        // Resto de sensores y de la alimentacion (la app los muestra todos) (Manuel - EA7JRS)
        WX_Utils::Reading reading;
        if (WX_Utils::getReading(reading) && reading.valid) {
            j.text("wxSensor", WX_Utils::sensorName());
            if (reading.hasGas) j.flt("gas", reading.gasKohm, 1);
        }
        #ifdef HAS_APRS_PAGES
            float iaqIndex = 0;
            const char* iaqLabel = "";
            if (PAGES_Utils::iaqValue(iaqIndex, &iaqLabel)) { j.flt("iaq", iaqIndex, 0); j.text("iaqLabel", iaqLabel); }
        #endif
        if (IMU_Utils::isReady()) {
            float x = 0, y = 0, z = 0;
            IMU_Utils::getAccelG(x, y, z);
            j.arr("accel"); j.item(x); j.item(y); j.item(z); j.endArr();
            IMU_Utils::getGyroDps(x, y, z);
            j.arr("gyro"); j.item(x, 1); j.item(y, 1); j.item(z, 1); j.endArr();
            j.flag("flat", IMU_Utils::isFlat());
        }
        if (MAG_Utils::isReady()) {
            j.flag("magCal", MAG_Utils::isCalibrated());
            j.flag("magAlign", MAG_Utils::isAligned());
            if (MAG_Utils::isAligned()) j.flt("magOff", MAG_Utils::alignmentOffset(), 1);
        }
        j.flag("charging", POWER_Utils::isCharging());
        const float vbus = POWER_Utils::getVbusVoltage();
        if (vbus > 0.0f) j.flt("vbus", vbus);
        j.flt("pmuTemp", POWER_Utils::getPmuTemperature(), 1);
        #if defined(HAS_AXP192) || defined(HAS_AXP2101)
            j.flt("battMa", POWER_Utils::getBatteryChargeDischargeCurrent(), 0);
        #endif
        if (batteryConnected) {
            #ifdef HAS_AXP2101
                String pct = POWER_Utils::getBatteryInfoCurrent();
            #else
                String pct = BATTERY_Utils::getPercentVoltageBattery(batteryVolts());
            #endif
            pct.trim();
            j.num("battPct", pct.toInt());
        }
        j.unum("heap", ESP.getFreeHeap());
        j.send(sendLine);
    }

    void sendGnss() {
        JW j("gnss");
        j.flag("fix", gps.location.isValid() && gps.location.age() < 5000);
        if (gps.location.isValid()) {
            j.flt("lat", gps.location.lat(), 6);
            j.flt("lon", gps.location.lng(), 6);
            j.unum("age", gps.location.age());
        }
        if (gps.altitude.isValid()) j.flt("alt", gps.altitude.meters(), 1);
        j.flt("spd", GPS_Utils::getSpeedKmph(), 1);
        j.flt("crs", GPS_Utils::getCourseDeg(), 1);
        if (gps.satellites.isValid()) j.num("sats", gps.satellites.value());
        if (gps.hdop.isValid()) j.flt("hdop", gps.hdop.hdop(), 1);
        if (gps.date.isValid() && gps.time.isValid() && gps.date.year() >= 2024) {
            char utc[24];
            snprintf(utc, sizeof(utc), "%04u-%02u-%02uT%02u:%02u:%02uZ", (unsigned)gps.date.year(), (unsigned)gps.date.month(), (unsigned)gps.date.day(),
                     (unsigned)gps.time.hour(), (unsigned)gps.time.minute(), (unsigned)gps.time.second());
            j.text("utc", utc);
        }
        j.send(sendLine);
    }

    // Cielo del receptor (GNSS_Sky): satelites a la vista con elevacion, azimut, SNR y si se usan; DOP y error de GST
    void sendSky() {
        JW j("sky");
        j.flt("pd", GNSS_Sky::pdop(), 1);
        j.flt("hd", GNSS_Sky::hdop(), 1);
        j.flt("vd", GNSS_Sky::vdop(), 1);
        j.flt("err", GNSS_Sky::errorM(), 1);
        if (gps.satellites.isValid()) j.num("used", gps.satellites.value());
        j.arr("s");
        const uint8_t n = GNSS_Sky::count();
        for (uint8_t i = 0; i < n; i++) {
            const GNSS_Sky::Sat* p = GNSS_Sky::sat(i);
            if (!p) break;
            char b[40];
            snprintf(b, sizeof(b), "[\"%c\",%u,%d,%d,%d,%d]", p->sys, (unsigned)p->svid, (int)p->elev, (int)p->azim, (int)p->snr, p->used ? 1 : 0);
            j.sep();
            j.s += b;
        }
        j.endArr();
        j.send(sendLine);
    }

    // ---------------------------------------------------------------- app: acciones, pantalla, mensajes, estaciones, configuracion

    uint32_t screenEveryMs = 0, screenLastMs = 0;
    uint32_t msgSeen = 0;

    void sendScreen() {
        #ifdef HAS_APRS_PAGES
            JW j("screen");
            j.num("p", PAGES_Utils::screenPage());
            j.num("cur", PAGES_Utils::currentPage());
            j.text("t", PAGES_Utils::screenTitle());
            j.text("r", PAGES_Utils::screenRight());
            j.unum("age", PAGES_Utils::screenAgeMs());
            j.flag("on", displayState);
            j.arr("rows");
            for (uint8_t i = 0; i < PAGES_Utils::screenRows(); i++) j.itemText(PAGES_Utils::screenRow(i));
            j.endArr();
            j.send(sendLine);
        #endif
    }

    #ifdef HAS_APRS_PAGES
    struct ActCtx { JW* j; int page; };

    void actCallback(uint8_t page, const char* pageTitle, const char* name, const char* label, bool allowed, void* ctx) {
        ActCtx* c = (ActCtx*)ctx;
        if ((int)page != c->page) {
            if (c->j) { c->j->endArr(); c->j->send(sendLine); delete c->j; }
            c->j = new JW("actlist");
            c->page = page;
            c->j->num("p", page);
            c->j->text("pt", pageTitle);
            c->j->arr("items");
        }
        c->j->obj();
        c->j->text("n", name);
        c->j->text("l", label);
        if (!allowed) c->j->flag("ok", false);
        c->j->endObj();
    }
    #endif

    void sendActList() {
        #ifdef HAS_APRS_PAGES
            ActCtx ctx{nullptr, -1};
            PAGES_Utils::forEachAction(actCallback, &ctx);
            if (ctx.j) { ctx.j->endArr(); ctx.j->send(sendLine); delete ctx.j; }
            JW end("actlist");
            end.flag("end", true);
            end.num("pages", PAGES_Utils::pageCount());
            end.send(sendLine);
        #endif
    }

    void putMsg(JW& j, uint8_t position) {
        #ifdef HAS_APRS_PAGES
            const PAGES_Core::MsgEntry* m = PAGES_Utils::msgAt(position);
            if (!m) return;
            const char dir[2] = { m->dir, '\0' };
            j.num("i", position);
            j.text("d", dir);
            j.unum("ep", m->epoch);
            j.num("st", PAGES_Utils::msgStateAt(position));       // 0 sin leer, 1 leido, 2 enviado, 3 esperando ack, 4 con ack, 5 fallido
            if (m->ackId) j.num("ack", m->ackId);
            j.text("c", m->call);
            j.text("t", m->text);
        #else
            (void)j; (void)position;
        #endif
    }

    void sendMsgs(int from, int cnt) {
        #ifdef HAS_APRS_PAGES
            JW j("msgs");
            j.num("tot", PAGES_Utils::msgCount());
            j.num("unread", PAGES_Utils::msgUnread());
            j.num("from", from);
            j.arr("items");
            int n = 0;
            for (int i = from; i < PAGES_Utils::msgCount() && n < cnt; i++, n++) { j.obj(); putMsg(j, (uint8_t)i); j.endObj(); }
            j.endArr();
            j.send(sendLine);
        #else
            (void)from; (void)cnt;
        #endif
    }

    // Cambio en el registro de mensajes (llega, se envia, confirmacion, leido): se avisa con el mas reciente
    void checkMsgs() {
        #ifdef HAS_APRS_PAGES
            const uint32_t seq = PAGES_Utils::msgSeq();
            if (seq == msgSeen) return;
            msgSeen = seq;
            if (!connected || !authed) return;
            JW j("msg");
            j.num("tot", PAGES_Utils::msgCount());
            j.num("unread", PAGES_Utils::msgUnread());
            if (PAGES_Utils::msgCount() > 0) putMsg(j, 0);
            j.send(sendLine);
        #endif
    }

    void sendHeard(int from, int cnt) {
        #ifdef HAS_APRS_PAGES
            JW j("heard");
            j.num("tot", PAGES_Utils::heardCount());
            j.unum("pkts", PAGES_Utils::heardPackets());
            j.num("from", from);
            j.arr("items");
            const uint32_t now = millis();
            int n = 0;
            for (int i = from; i < PAGES_Utils::heardCount() && n < cnt; i++, n++) {
                const PAGES_Core::Station* st = PAGES_Utils::heardAt((uint8_t)i);
                if (!st) break;
                j.obj();
                j.text("c", st->call);
                const char sym[3] = { st->symbol, st->table, '\0' };
                j.text("sym", sym);
                j.text("k", PAGES_Core::kindName(st->kind));
                if (st->hasPosition) { j.flt("lat", st->lat, 5); j.flt("lon", st->lon, 5); }
                j.num("alt", st->altitude);
                j.num("spd", st->speed);
                j.num("crs", st->course);
                j.num("rssi", st->rssi);
                j.flt("snr", st->snr, 1);
                j.unum("age", (now - st->lastMs) / 1000UL);
                if (st->hasPosition && st->posMs) j.unum("pa", (now - st->posMs) / 1000UL);     // antiguedad de la posicion
                j.num("n", st->packets);
                if (st->device[0])  j.text("dev", st->device);
                if (st->path[0])    j.text("path", st->path);
                if (st->comment[0]) j.text("cmt", st->comment);
                if (st->status[0])  j.text("sts", st->status);
                if (st->wx.valid) {
                    j.objKey("wx");
                    if (st->wx.tempC != PAGES_Core::WX_NA)       j.num("t", st->wx.tempC);
                    if (st->wx.humidity != PAGES_Core::WX_NA)    j.num("h", st->wx.humidity);
                    if (st->wx.pressureHpa != PAGES_Core::WX_NA) j.num("p", st->wx.pressureHpa);
                    if (st->wx.windDir != PAGES_Core::WX_NA)     j.num("wd", st->wx.windDir);
                    if (st->wx.windKmh != PAGES_Core::WX_NA)     j.num("ws", st->wx.windKmh);
                    if (st->wx.gustKmh != PAGES_Core::WX_NA)     j.num("wg", st->wx.gustKmh);
                    if (st->wx.rainMm10 != PAGES_Core::WX_NA)    j.num("r", st->wx.rainMm10);
                    j.endObj();
                }
                j.endObj();
            }
            j.endArr();
            j.send(sendLine);
        #else
            (void)from; (void)cnt;
        #endif
    }

    // Configuracion completa: el fichero /tracker_conf.json se lee y se escribe en bruto (la app lo interpreta y lo mezcla; el tracker
    // no analiza JSON aqui, lo que ahorra mucha flash). Lectura por trozos (cfgget con o = desplazamiento); escritura de todo el fichero
    // por trozos (cfgset). Al arrancar, Configuration::readFile() lo valida y pone valores por defecto a lo que falte o este fuera de rango.
    void sendCfg(uint32_t offset, int id) {
        File f = SPIFFS.open("/tracker_conf.json", "r");
        if (!f) { reply("cfgget", id, "no config file"); return; }
        const uint32_t total = f.size();
        char buf[321];
        size_t len = 0;
        if (offset < total && f.seek(offset)) len = f.read((uint8_t*)buf, 320);
        f.close();
        if (offset + len < total && len > 0) {                      // no se corta un caracter UTF-8 por la mitad
            size_t i = len - 1;
            while (i > 0 && (buf[i] & 0xC0) == 0x80) i--;
            const uint8_t lead = (uint8_t)buf[i];
            const size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
            if (i + need > len) len = i;
        }
        buf[len] = '\0';
        JsonDocument doc;
        doc["e"] = "cfg";
        if (id >= 0) doc["id"] = id;
        doc["o"] = offset;
        doc["n"] = total;
        doc["b"] = (uint32_t)len;
        doc["d"] = (const char*)buf;
        sendDoc(doc);
    }

    File     cfgUp;
    uint32_t cfgExpect = 0, cfgGot = 0;
    int      cfgDepth = 0;
    bool     cfgInString = false, cfgEscape = false, cfgBroken = false, cfgStarted = false;
    bool     cfgReboot = true;

    void cfgAbort() {
        if (cfgUp) cfgUp.close();
        SPIFFS.remove("/tracker_conf.up");
        cfgExpect = cfgGot = 0;
    }

    // Comprobacion de forma mientras llega: llaves y corchetes equilibrados y un unico objeto raiz
    void cfgFeed(const char* text) {
        for (const char* p = text; *p; p++) {
            const char c = *p;
            if (cfgInString) {
                if (cfgEscape) cfgEscape = false;
                else if (c == '\\') cfgEscape = true;
                else if (c == '"') cfgInString = false;
            } else if (c == '"') cfgInString = true;
            else if (c == '{' || c == '[') { if (!cfgStarted && c != '{') cfgBroken = true; cfgStarted = true; cfgDepth++; }
            else if (c == '}' || c == ']') { if (--cfgDepth < 0) cfgBroken = true; }
            else if (!cfgStarted && c > ' ') cfgBroken = true;
        }
    }

    void putPage(JW& j, const DAPNET_Utils::Entry* entry) {
        j.unum("ric", entry->ric);
        j.num("f", entry->func);
        j.flag("own", entry->own);
        j.flag("bad", entry->corrupt);
        j.unum("age", (millis() - entry->atMs) / 1000UL);
        j.num("rssi", entry->rssi);
        j.text("t", entry->text);
    }

    void sendDapnet() {
        JW j("dapnet");
        j.arr("items");
        for (uint8_t i = 0; i < DAPNET_Utils::count(); i++) {
            const DAPNET_Utils::Entry* entry = DAPNET_Utils::at(i);
            if (entry) { j.obj(); putPage(j, entry); j.endObj(); }
        }
        j.endArr();
        j.send(sendLine);
    }

    void checkDapnet() {
        const uint32_t received = DAPNET_Utils::received();
        if (received == dapnetSeen) return;
        const bool grew = received > dapnetSeen;
        dapnetSeen = received;
        if (!grew || Config.operatingMode != MODE_DAPNET) return;
        const DAPNET_Utils::Entry* entry = DAPNET_Utils::at(0);
        if (!entry) return;
        JW j("page");
        putPage(j, entry);
        j.send(sendLine);
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
        JW j("rot");
        j.flt("az", az, 1);
        j.flt("el", el, 1);
        j.flag("trk", tracking);
        j.num("src", ROTOR_Utils::source());
        float pit, rol;
        if (ROTOR_Utils::tiltInfo(pit, rol)) { j.flt("pit", pit, 1); j.flt("rol", rol, 1); }
        float absAz, hdg;
        if (ROTOR_Utils::mobileInfo(absAz, hdg)) { j.flag("mob", true); j.flt("abs", absAz, 1); j.flt("hdg", hdg, 1); }
        if (ROTOR_Utils::targetName()[0]) j.text("name", ROTOR_Utils::targetName());
        if (ROTOR_Utils::targetDistKm() > 0.0) j.num("km", (int)ROTOR_Utils::targetDistKm());
        float aa, ae;
        if (ROTOR_Utils::actual(aa, ae)) { j.flt("aaz", aa, 1); j.flt("ael", ae, 1); }   // posicion que devuelve el rotor
        j.send(sendLine);
    }

    // T-TWR en FM: la lista de satelites se carga la primera vez que la pide la app (en el modo SAT ya esta)
    static void satEnsureLoaded() {
        #if defined(HAS_SAT) && defined(HAS_SA868)
            if (Config.operatingMode == MODE_FM && SAT_Utils::count() == 0) SAT_Utils::begin();
        #endif
    }

    static void vesselJson(JW& j, const AisCore::Vessel& v, const AisCore::Own& o, bool full) {
        j.num("m", v.mmsi);
        const char cl[2] = { v.cls, 0 }; j.text("cl", cl);
        if (v.name[0]) j.text("n", v.name);
        if (v.call[0]) j.text("c", v.call);
        if (v.shipType) j.num("t", v.shipType);
        if (v.status != 15) j.num("st", v.status);
        if (v.hasPos) { j.flt("lat", v.lat, 5); j.flt("lon", v.lon, 5); j.num("pa", (millis() - v.posMs) / 1000); }
        if (v.hasSog) j.flt("sog", v.sogKn, 1);
        if (v.hasCog) j.flt("cog", v.cogDeg, 1);
        if (v.hdg != 511) j.num("hdg", v.hdg);
        j.num("age", (millis() - v.lastMs) / 1000);
        if (v.hasPos && o.valid) {
            double nm, brg, c = 0, t = 0;
            AisCore::rangeBearing(o, v.lat, v.lon, nm, brg);
            j.flt("nm", nm, 2); j.num("brg", lround(brg));
            if (AisCore::cpa(o, v, c, t)) { j.flt("cpa", c, 2); j.flt("tcpa", t, 1); }
            j.flag("dng", AIS_Utils::dangerous(v));
        }
        if (!full) return;
        if (v.dest[0]) j.text("ds", v.dest);
        if (v.length) j.num("len", v.length);
        if (v.beam) j.num("bm", v.beam);
        if (v.draughtM > 0) j.flt("dr", v.draughtM, 1);
        if (v.etaMonth) { char e[16]; snprintf(e, sizeof(e), "%02u-%02u %02u:%02u", v.etaMonth, v.etaDay, v.etaHour, v.etaMinute); j.text("eta", e); }
        if (v.imo) j.num("imo", v.imo);
        if (v.cls == AisCore::CL_SAR && v.altM) j.num("alt", v.altM);
        if (v.lastText[0]) j.text("txt", v.lastText);
        if (v.channel) { const char ch[2] = { v.channel, 0 }; j.text("ch", ch); }
        j.num("msgs", v.msgs);
    }

    static void sendAisList(int from, int cnt) {
        AisCore::Own o; AIS_Utils::own(o);
        static int idx[AisCore::Table::CAP];
        const int m = AIS_Utils::sorted(idx, AisCore::Table::CAP);
        JW j("aislist");
        j.num("tot", m);
        j.num("from", from);
        j.num("msgs", AIS_Utils::messages());
        j.text("src", AIS_Utils::sourceName());
        const char ch = AIS_Utils::channel();
        if (ch) { const char c[2] = { ch, 0 }; j.text("ch", c); }
        j.arr("items");
        for (int k = from; k < m && k < from + cnt; k++) {
            const AisCore::Vessel* v = AIS_Utils::table().at(idx[k]);
            if (!v) continue;
            j.obj(); vesselJson(j, *v, o, false); j.endObj();
        }
        j.endArr();
        j.send(sendLine);
    }

    // satrx / sattx: modo SAT; en la T-TWR tambien en FM (el VFO sigue al satelite)
    __attribute__((unused)) static bool satModeOk() {
        if (Config.operatingMode == MODE_SAT) return true;
        #ifdef HAS_SA868
            return Config.operatingMode == MODE_FM;
        #else
            return false;
        #endif
    }

    // Recepcion de satelites (modo SAT, fase 2): cada linea nueva (trama, paquete LoRa o texto CW) y cada 5 s mientras se escucha
    void sendSatRx(bool withText) {
        #ifdef HAS_SAT
            JW j("satrx");
            j.flag("on", SATRX_Utils::active());
            if (SATRX_Utils::active()) {
                j.text("k", SATRX_Utils::kindText(SATRX_Utils::kind()));
                j.num("f", llround(SATRX_Utils::rxHz()));
                j.num("dop", SATRX_Utils::dopplerHz());
                j.num("fine", SATRX_Utils::fineHz());
                j.num("rssi", lroundf(SATRX_Utils::rssi()));
                j.num("n", SATRX_Utils::frames());
                j.num("tx", SATRX_Utils::txIndex());
            }
            char line[112];
            if (withText && SATRX_Utils::logLine(0, line, sizeof(line))) j.text("txt", line);
            j.send(sendLine);
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

    // ---- Modo SAT para la app: estado, lista, transmisores y subida del archivo de datos ----
    void sendSat() {
        #ifdef HAS_SAT
            const SatCore::Block* b = SAT_Utils::block();
            JW j("sat");
            j.flag("ok", SAT_Utils::hasData() && b != nullptr);
            if (b) {
                j.text("n", b->name);
                j.num("id", b->noradId);
                j.num("i", SAT_Utils::selected());
                j.flag("geo", SAT_Utils::geo());
                j.num("tx", b->txCount);
                j.flt("tle", SAT_Utils::tleAgeDays(), 1);
                j.flag("fav", SAT_Utils::isFavorite(b->noradId));
            }
            j.num("cat", Config.sat.category);
            j.num("tot", SAT_Utils::count());
            j.num("nfav", SAT_Utils::favoriteCount());
            j.text("stamp", SAT_Utils::dataStamp());
            j.text("pos", SAT_Utils::haveQth() ? SAT_Utils::qthSource() : "");
            SatCore::Look lk;
            if (b && SAT_Utils::live(lk)) {
                j.flt("az", lk.azDeg, 1);
                j.flt("el", lk.elDeg, 1);
                j.num("rng", lround(lk.rangeKm));
                j.flt("rr", lk.rangeRateKmS, 3);
                SatCore::Pass ps; bool searching = false;
                if (!SAT_Utils::geo() && SAT_Utils::nextPass(ps, searching)) {
                    j.num("aos", (long)ps.aosUnix); j.num("los", (long)ps.losUnix); j.num("max", lround(ps.maxElDeg));
                }
            }
            j.send(sendLine);
        #endif
    }

    void checkSat() {
        #ifdef HAS_SAT
            bool want = Config.operatingMode == MODE_SAT;
            #ifdef HAS_SA868
                want = want || FM_Utils::satOn();                    // FM siguiendo a un satelite
            #endif
            if (!connected || !authed || !want) return;
            static uint32_t lastMs = 0;
            const uint32_t now = millis();
            if (now - lastMs < 2000) return;
            lastMs = now;
            sendSat();
        #endif
    }

    void sendSatList(int from, int cnt) {
        #ifdef HAS_SAT
            JW j("satlist");
            j.num("tot", SAT_Utils::count());
            j.num("sel", SAT_Utils::selected());
            j.num("from", from);
            j.arr("items");
            int n = 0;
            for (int i = from; i < SAT_Utils::count() && n < cnt; i++, n++) {
                const SAT_Utils::Entry* e = SAT_Utils::at(i);
                if (!e) break;
                const char cat[2] = { (char)e->cat, '\0' };
                j.obj();
                j.num("i", i); j.num("id", e->noradId); j.text("n", e->name); j.text("c", cat); j.num("t", e->txCount);
                j.num("b", e->bands);
                if (SAT_Utils::isFavorite(e->noradId)) j.flag("fav", true);
                j.flag("in", SAT_Utils::inCategory(*e));                // en la categoria elegida
                j.endObj();
            }
            j.endArr();
            j.send(sendLine);
        #else
            (void)from; (void)cnt;
        #endif
    }

    void sendSatTx() {
        #ifdef HAS_SAT
            const SatCore::Block* b = SAT_Utils::block();
            JW j("sattxlist");
            j.arr("items");
            if (b) {
                for (int i = 0; i < b->txCount; i++) {
                    const SatCore::Transmitter& t = b->tx[i];
                    j.obj();
                    j.num("i", i); j.text("m", t.mode); j.num("b", t.baud); j.text("d", t.desc);
                    j.num("up", (long long)SatCore::txUpCenter(t)); j.num("dn", (long long)SatCore::txDownCenter(t));
                    j.text("rx", SATRX_Utils::kindText(SATRX_Utils::classify(t)));
                    j.text("tx", SATRX_Utils::kindText(SATTX_Utils::classifyUp(t)));
                    if (t.tone) j.flt("tone", t.tone / 10.0, 1);
                    if (SatCore::aprsDigi(t)) j.flag("aprs", true);
                    double rx = 0, tx = 0;
                    SAT_Utils::doppler(t, rx, tx);
                    if (rx > 0) j.num("rxd", llround(rx));
                    if (tx > 0) j.num("txd", llround(tx));
                    j.endObj();
                }
            }
            j.endArr();
            j.send(sendLine);
        #endif
    }

    void sendTargets() {
        JW j("targets");
        j.num("sel", Config.rotor.targetIndex);
        j.num("src", Config.rotor.source);
        j.text("ff", Config.rotor.followFilter.c_str());
        j.num("fm", Config.rotor.followMaxMin);
        j.flag("pr", Config.rotor.predict);
        j.num("pm", Config.rotor.predictMaxS);
        j.arr("items");
        for (int i = 0; i < RotorConfig::TARGETS; i++) {
            const RotorConfig::Target& t = Config.rotor.targets[i];
            if (t.name.isEmpty()) continue;
            j.obj();
            j.num("i", i); j.text("n", t.name.c_str()); j.flt("lat", t.lat, 6); j.flt("lon", t.lon, 6); j.num("alt", t.alt); j.num("f", t.freqHz);
            j.endObj();
        }
        j.endArr();
        j.send(sendLine);
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

    // ---------------------------------------------------------------- A-GPS desde la app (EA7JRS)

    bool     gpsAidActive = false;
    uint32_t gpsAidBytes  = 0;

    // Base64 estandar (con o sin relleno '='): devuelve los bytes escritos en out, o -1 si el texto no es valido o no cabe
    int base64Decode(const char* in, uint8_t* out, size_t outMax) {
        uint32_t acc = 0;
        int bits = 0;
        size_t n = 0;
        for (const char* p = in; *p; p++) {
            const char c = *p;
            int v;
            if (c >= 'A' && c <= 'Z') v = c - 'A';
            else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
            else if (c >= '0' && c <= '9') v = c - '0' + 52;
            else if (c == '+') v = 62;
            else if (c == '/') v = 63;
            else if (c == '=') break;
            else return -1;
            acc = (acc << 6) | (uint32_t)v;
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                if (n >= outMax) return -1;
                out[n++] = (uint8_t)(acc >> bits);
            }
        }
        return (int)n;
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

    // ---------------------------------------------------------------- radio comercial (I2C)

    void sendBcast() {
        JsonDocument doc;
        doc["e"]    = "bcast";
        doc["chip"] = BCAST::chipName();
        doc["on"]   = BCAST::isOn();
        doc["band"] = BCAST::band();
        doc["f"]    = BCAST::freqKHz();
        doc["vol"]  = BCAST::volume();
        doc["mute"] = BCAST::muted();
        doc["mono"] = Config.bcast.mono;
        doc["rssi"] = BCAST::rssi();
        doc["snr"]  = BCAST::snr();
        doc["st"]   = BCAST::stereo();
        doc["seek"] = BCAST::seeking();
        doc["ps"]   = BCAST::stationName();
        char rt[70]; BCAST::radioText(rt, sizeof(rt));
        doc["rt"]   = rt;
        doc["pi"]   = BCAST::rdsPi();
        int y, mo, d, h, mi, off;
        if (BCAST::rdsClock(y, mo, d, h, mi, off)) { char ct[24]; snprintf(ct, sizeof(ct), "%04d-%02d-%02d %02d:%02d", y, mo, d, h, mi); doc["ct"] = ct; doc["ctOff"] = off; }
        JsonArray bands = doc["bands"].to<JsonArray>();
        for (uint8_t b = 0; b < BCAST::BAND_COUNT; b++) if (BCAST::bandAvailable(b)) bands.add(b);
        JsonArray pr = doc["presets"].to<JsonArray>();
        for (int i = 0; i < BcastConfig::PRESETS; i++) pr.add(Config.bcast.presets[i]);
        sendDoc(doc);
    }

    // mientras la app lo pida (sub bcast) o cuando cambia la frecuencia / el RDS: como mucho cada 500 ms
    static uint32_t bcastEveryMs = 0;
    void checkBcast() {
        if (!connected || !authed || BCAST::chip() == BCAST::CHIP_NONE) return;
        static uint32_t lastMs = 0, lastF = 0;
        static char lastPs[9] = "";
        const uint32_t now = millis();
        const bool changed = BCAST::freqKHz() != lastF || strcmp(lastPs, BCAST::stationName()) != 0;
        if ((changed && now - lastMs >= 500) || (bcastEveryMs && now - lastMs >= bcastEveryMs)) {
            lastMs = now; lastF = BCAST::freqKHz(); snprintf(lastPs, sizeof(lastPs), "%s", BCAST::stationName());
            sendBcast();
        }
    }

    // ---------------------------------------------------------------- FM (T-TWR)

    #ifdef HAS_SA868
    static uint32_t carrierUntil = 0;

    static void fmChannelJson(JsonObject o, const FmCore::Channel& c) {
        o["f"] = c.rxHz; o["tx"] = FmCore::txHz(c); o["sh"] = c.shift; o["ofs"] = c.offsetHz; o["tm"] = c.toneMode; o["t"] = c.tone;
        o["d"] = c.dcs; o["inv"] = c.dcsInv; o["w"] = c.wide; o["hp"] = c.high;
        if (c.name[0]) o["n"] = c.name;
    }

    void sendFm() {
        if (Config.operatingMode != MODE_FM) return;
        JsonDocument doc;
        doc["e"] = "fm";
        fmChannelJson(doc.as<JsonObject>(), FM_Utils::channel());
        doc["m"]    = FM_Utils::memMode();
        doc["i"]    = FM_Utils::memIndex();
        doc["step"] = FM_Utils::stepHz();
        doc["sq"]   = FM_Utils::squelch();
        doc["sqm"]  = Config.sa868.squelch;
        doc["sqa"]  = Config.fm.squelchAuto;
        doc["nr"]   = Config.fm.nrLevel;
        doc["edit"] = FM_Utils::edit();
        doc["busy"] = FM_Utils::busy();
        doc["ptt"]  = FM_Utils::ptt();
        doc["txs"]  = FM_Utils::txSeconds();
        doc["rssi"] = FM_Utils::rssi();
        doc["scan"] = FM_Utils::scanning() ? (FM_Utils::scanPaused() ? 2 : 1) : 0;
        doc["arts"] = FM_Utils::artsOn() ? FM_Utils::artsState() + 1 : 0;      // 0 apagado, 1 buscando, 2 en alcance, 3 fuera
        doc["eai"]  = FM_Utils::eaiOn() ? FM_Utils::eaiRemainingS() : 0;
        doc["aprs"] = Config.fm.aprsBeacon;
        doc["qsyb"] = Config.fm.qsyInBeacon;
        doc["tok"]  = SA868::fmLastToneOk();
        doc["spk"]  = SA868::fmSpeakerFromEsp();
        #ifdef HAS_SAT
        if (FM_Utils::satOn()) {                                    // el VFO sigue a un satelite
            JsonObject so = doc["sat"].to<JsonObject>();
            const SatCore::Block* b = SAT_Utils::block();
            if (b) { so["n"] = b->name; so["id"] = b->noradId; }
            so["tx"]   = FM_Utils::satTxIndex();
            so["f"]    = FM_Utils::satRxHz();
            so["up"]   = FM_Utils::satUpHz();
            so["dop"]  = FM_Utils::satDopplerHz();
            so["fine"] = FM_Utils::satFineHz();
            so["dok"]  = FM_Utils::satDopplerOk();
            so["xup"]  = FM_Utils::satListenUplink();
            so["aprs"] = FM_Utils::satAprs();
            so["tone"] = FM_Utils::satTone() / 10.0;
            so["sent"] = FM_Utils::satSent();
            so["heard"] = FM_Utils::satHeard();
            SatCore::Look lk;
            if (SAT_Utils::live(lk)) { so["az"] = lround(lk.azDeg * 10) / 10.0; so["el"] = lround(lk.elDeg * 10) / 10.0; }
            char why[28];
            so["txok"] = FM_Utils::satCanTx(why, sizeof(why));
            if (why[0]) so["why"] = why;
        }
        #endif
        const char* n = FM_Utils::notice();
        if (n[0]) doc["note"] = n;
        sendDoc(doc);
    }

    void sendFmMems() {
        JsonDocument doc;
        doc["e"] = "fmmems";
        doc["max"] = FmConfig::MEMORIES;
        JsonArray a = doc["mem"].to<JsonArray>();
        for (int i = 0; i < FmConfig::MEMORIES; i++) {
            if (!FM_Utils::memUsed(i)) continue;
            const FmMemory& m = Config.fm.mem[i];
            JsonObject o = a.add<JsonObject>();
            o["i"] = i; o["n"] = m.name; o["f"] = m.rxHz; o["sh"] = m.shift; o["ofs"] = m.offsetHz; o["tm"] = m.toneMode; o["t"] = m.tone;
            o["d"] = m.dcs; o["inv"] = m.dcsInv; o["w"] = m.wide; o["hp"] = m.high;
        }
        sendDoc(doc);
    }

    // el estado cambia (frecuencia, ocupado, PTT, escaneo...): evento como mucho 4 veces por segundo; si no, cada 5 s
    void checkFm() {
        if (carrierUntil && (int32_t)(millis() - carrierUntil) >= 0) { carrierUntil = 0; SA868::carrier(false); reply("carrier", -1, "stopped after 30 s"); }
        if (!connected || !authed || Config.operatingMode != MODE_FM) return;
        static uint32_t lastMs = 0, lastSig = 0;
        const FmCore::Channel& c = FM_Utils::channel();
        const uint32_t sig = (uint32_t)c.rxHz ^ ((uint32_t)c.toneMode << 28) ^ ((uint32_t)c.tone << 12) ^ (FM_Utils::busy() ? 1u : 0) ^ (FM_Utils::ptt() ? 2u : 0) ^
                             ((uint32_t)FM_Utils::scanning() << 2) ^ ((uint32_t)FM_Utils::artsState() << 3) ^ ((uint32_t)FM_Utils::memIndex() << 5) ^
                             ((uint32_t)c.shift << 26) ^ ((uint32_t)FM_Utils::squelch() << 20) ^ (c.high ? 0x40000000u : 0) ^ (c.wide ? 0x80000000u : 0) ^
                             ((uint32_t)FM_Utils::eaiOn() << 4) ^ (uint32_t)(FM_Utils::notice()[0]) ^ ((uint32_t)FM_Utils::satOn() << 1) ^ (uint32_t)(FM_Utils::satFineHz() * 7);
        const uint32_t now = millis();
        if ((sig != lastSig && now - lastMs >= 250) || now - lastMs >= 5000) { lastSig = sig; lastMs = now; sendFm(); }
    }

    // canal del VFO a partir de la orden (los campos que falten se quedan como estan)
    static FmCore::Channel channelFrom(JsonDocument& doc) {
        FmCore::Channel c = FM_Utils::channel();
        c.name[0] = 0;
        if (!doc["f"].isNull())   c.rxHz = doc["f"] | c.rxHz;
        if (!doc["sh"].isNull())  c.shift = (int8_t)constrain((int)(doc["sh"] | 0), -1, 1);
        if (!doc["ofs"].isNull()) c.offsetHz = constrain((long)(doc["ofs"] | 0L), 0L, 99900000L);
        if (!doc["tm"].isNull())  c.toneMode = (uint8_t)constrain((int)(doc["tm"] | 0), 0, FmCore::TONE_MODES - 1);
        if (!doc["t"].isNull())   { const uint16_t t = doc["t"] | 885; if (FmCore::ctcssIndex(t) >= 0) c.tone = t; }
        if (!doc["d"].isNull())   { const uint16_t d = doc["d"] | 23; if (FmCore::validDcs(d)) c.dcs = d; }
        if (!doc["inv"].isNull()) c.dcsInv = doc["inv"] | false;
        if (!doc["w"].isNull())   c.wide = doc["w"] | false;
        if (!doc["hp"].isNull())  c.high = doc["hp"] | false;
        return c;
    }
    #endif

    void handle(const char* text) {
        JsonDocument doc;
        if (deserializeJson(doc, text) != DeserializationError::Ok) { reply("?", -1, "bad JSON"); return; }
        const char* cmd = doc["c"] | "";
        const int id    = doc["id"] | -1;

        if      (strcmp(cmd, "info") == 0)    sendInfo();
        else if (strcmp(cmd, "status") == 0)  sendStatus();
        else if (strcmp(cmd, "sensors") == 0) sendSensors();
        else if (strcmp(cmd, "gnss") == 0)    sendGnss();
        else if (strcmp(cmd, "sky") == 0)     sendSky();
        else if (strcmp(cmd, "dapnet") == 0)  sendDapnet();
        else if (strcmp(cmd, "sub") == 0) {
            const int s = doc["sensors"] | -1;
            const int g = doc["gnss"] | -1;
            const int sc = doc["screen"] | -1;
            const int sk = doc["sky"] | -1;
            if (sk >= 0) skyEveryMs = sk == 0 ? 0 : (uint32_t)max(sk, 2) * 1000UL;
            if (sc >= 0) screenEveryMs = sc == 0 ? 0 : (uint32_t)max(sc, 1) * 1000UL;
            if (s >= 0) sensorsEveryMs = s == 0 ? 0 : (uint32_t)max(s, 2) * 1000UL;
            if (g >= 0) gnssEveryMs    = g == 0 ? 0 : (uint32_t)max(g, 1) * 1000UL;
            reply(cmd, id);
        }
        else if (strcmp(cmd, "cwrx") == 0) {                      // escucha CW por RF: s = start | stop; texto en el evento "cwrx"
            #ifdef HAS_APRS_PAGES
                const char* st = doc["s"] | "";
                if (strcmp(st, "start") == 0) {
                    if (!LoRa_Utils::cwAvailable()) { reply(cmd, id, "CW not available on this radio"); return; }
                    reply(cmd, id);
                    cwRxStop = false;
                    PAGES_Utils::cwListenSession();                      // vuelve al pulsar el boton, con "stop" o a los 10 minutos
                    cwRxStop = false;
                    return;
                }
                if (strcmp(st, "stop") != 0) { reply(cmd, id, "bad step"); return; }
                cwRxStop = false;                                        // no habia sesion
                reply(cmd, id);
            #else
                reply(cmd, id, "not available");
            #endif
        }
        else if (strcmp(cmd, "rotext") == 0) {                    // rotctld del movil (Look4Sat, GPredict...): az, el en grados | park = true; repetir (caduca a los 15 s); solo responde si hay error
            if (!ROTOR_Utils::enabled()) { reply(cmd, id, "rotor disabled"); return; }
            if (doc["park"] | false) { ROTOR_Utils::setExternal((float)Config.rotor.parkAz, (float)Config.rotor.parkEl); return; }
            if (doc["az"].isNull() || doc["el"].isNull()) { reply(cmd, id, "az and el needed"); return; }
            float az = doc["az"] | 0.0f, el = doc["el"] | 0.0f;
            if (!isfinite(az) || !isfinite(el) || el < -90.0f || el > 180.0f) { reply(cmd, id, "bad position"); return; }
            az = fmodf(az, 360.0f); if (az < 0) az += 360.0f;
            if (el > 90.0f) el = 90.0f;
            ROTOR_Utils::setExternal(az, el);
        }
        else if (strcmp(cmd, "att") == 0) {                       // inclinometro del soporte del rotor: p = cabeceo (+ morro arriba), r = alabeo (+ lado derecho abajo), grados; repetir cada <2 s
            if (doc["p"].isNull() || doc["r"].isNull()) { reply(cmd, id, "p and r needed"); return; }
            ROTOR_Utils::setAttitude(doc["p"] | 0.0f, doc["r"] | 0.0f);
        }
        else if (strcmp(cmd, "satrx") == 0) {                     // receptor de satelites (modo SAT): on = true/false, tx = indice del transmisor (opcional), fine = ajuste en Hz (opcional)
            #ifdef HAS_SAT
                if (!satModeOk()) { reply(cmd, id, "not in SAT mode"); return; }
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
                if (!satModeOk()) { reply(cmd, id, "not in SAT mode"); return; }
                const uint8_t txp = (uint8_t)(doc["tx"] | 0);
                if (doc["pos"] | false) {                         // T-TWR: posicion APRS por el digipetidor (ISS)
                    if (!SATTX_Utils::sendPosition(txp)) { reply(cmd, id, SATTX_Utils::message()); return; }
                    reply(cmd, id);
                    JsonDocument ev;
                    ev["e"] = "sattx"; ev["ok"] = true; ev["pos"] = true;
                    ev["f"] = (long)llround(SATTX_Utils::lastHz()); ev["n"] = SATTX_Utils::count();
                    sendDoc(ev);
                    return;
                }
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
        else if (strcmp(cmd, "sat") == 0) { satEnsureLoaded(); sendSat(); }
        else if (strcmp(cmd, "txunlock") == 0) {                  // T-TWR: code = codigo de desbloqueo del rango de emision de este equipo; lock:true = volver a bandas de aficionado
            #ifdef HAS_SA868
                if (doc["lock"] | false) { TX_Unlock::lock(); reply(cmd, id); return; }
                const char* why = "";
                if (!TX_Unlock::tryUnlock(doc["code"] | "", &why)) { reply(cmd, id, why); return; }
                reply(cmd, id);
            #else
                reply(cmd, id, "not available");
            #endif
        }
        else if (strcmp(cmd, "aislist") == 0) {                   // barcos AIS por distancia: from, cnt (1..15)
            sendAisList(max(0, (int)(doc["from"] | 0)), constrain((int)(doc["cnt"] | 10), 1, 15));
        }
        else if (strcmp(cmd, "aisin") == 0) {                     // frase !AIVDM del receptor de la app (RTL-SDR): l
            const char* l = doc["l"] | "";
            AIS_Utils::feedNmea(l, AIS_Utils::SRC_APP);
        }
        else if (strcmp(cmd, "aisset") == 0) {                    // ch (0 A, 1 B, 2 alternar), cpa (decimas de milla), tcpa (min), guard (decimas), alarm, sart, clear, goto (MMSI)
            if (!doc["ch"].isNull())    { Config.ais.channel = constrain((int)(doc["ch"] | 0), 0, 2); AIS_Utils::setChannelMode(Config.ais.channel); }
            if (!doc["cpa"].isNull())   Config.ais.cpaTenthNm = constrain((int)(doc["cpa"] | 5), 1, 100);
            if (!doc["tcpa"].isNull())  Config.ais.tcpaMin = constrain((int)(doc["tcpa"] | 15), 1, 120);
            if (!doc["guard"].isNull()) Config.ais.guardTenthNm = constrain((int)(doc["guard"] | 0), 0, 200);
            if (!doc["alarm"].isNull()) Config.ais.cpaAlarm = doc["alarm"] | true;
            if (!doc["sart"].isNull())  Config.ais.distressAlarm = doc["sart"] | true;
            if (doc["clear"] | false)   AIS_Utils::clear();
            if (!doc["goto"].isNull())  AIS_Utils::goTo((uint32_t)(doc["goto"] | 0UL));
            if (!doc["ch"].isNull() || !doc["cpa"].isNull() || !doc["tcpa"].isNull() || !doc["guard"].isNull() || !doc["alarm"].isNull() || !doc["sart"].isNull()) Config.writeFile();
            reply(cmd, id);
        }
        else if (strcmp(cmd, "wxnet") == 0) {                     // tiempo de internet (la app: OpenWeatherMap / IQAir) para la pagina TELEMETRY
            NET_WX::Data d;
            if (!NET_WX::fromJson(doc, d)) { reply(cmd, id, "no data"); return; }
            NET_WX::apply(d);
            reply(cmd, id);
        }
        else if (strcmp(cmd, "satfav") == 0) {                    // favorito: id = NORAD (si falta, el elegido), on = true / false
            #ifdef HAS_SAT
                int nid = doc["id"] | 0;
                if (nid <= 0 && SAT_Utils::block()) nid = SAT_Utils::block()->noradId;
                if (nid <= 0) { reply(cmd, id, "no satellite"); return; }
                if (!SAT_Utils::setFavorite(nid, doc["on"] | true)) { reply(cmd, id, "favorites full (32)"); return; }
                reply(cmd, id);
                sendSat();
            #else
                reply(cmd, id, "not available");
            #endif
        }
        else if (strcmp(cmd, "satcat") == 0) {                    // categoria de la lista: k = 0 todos, 1 70 cm, 2 LoRa, 3 GEO, 4 favoritos, 5 2 m, 6 esta radio
            #ifdef HAS_SAT
                if (doc["k"].isNull() || !SAT_Utils::setCategory(doc["k"] | 0)) { reply(cmd, id, "bad category"); return; }
                reply(cmd, id);
                sendSat();
            #else
                reply(cmd, id, "not available");
            #endif
        }
        else if (strcmp(cmd, "satlist") == 0) { satEnsureLoaded(); sendSatList(max(0, (int)(doc["from"] | 0)), constrain((int)(doc["cnt"] | 20), 1, 40)); }
        else if (strcmp(cmd, "sattxlist") == 0) { satEnsureLoaded(); sendSatTx(); }
        else if (strcmp(cmd, "satsel") == 0) {                    // elige el satelite: i = indice de la lista, o id = numero NORAD
            #ifdef HAS_SAT
                satEnsureLoaded();
                int target = -1;
                if (!doc["i"].isNull()) target = doc["i"] | -1;
                else if (!doc["id"].isNull()) {
                    const int want = doc["id"] | 0;
                    for (int k = 0; k < SAT_Utils::count(); k++) { const SAT_Utils::Entry* e = SAT_Utils::at(k); if (e && e->noradId == want) { target = k; break; } }
                }
                if (target < 0 || !SAT_Utils::select(target)) { reply(cmd, id, "no such satellite"); return; }
                SATRX_Utils::stop();
                reply(cmd, id);
                sendSat();
            #else
                reply(cmd, id, "not available");
            #endif
        }
        else if (strcmp(cmd, "gpsaid") == 0) {                    // A-GPS: s = begin | c (d = UBX en base64, <= 180 caracteres) | end | abort
            const char* st = doc["s"] | "";
            if (GPS_Utils::gpsModule() != 1) { reply(cmd, id, "the GNSS receiver is not a u-blox"); return; }
            if (strcmp(st, "begin") == 0) {
                gpsAidBytes = 0;
                gpsAidActive = true;
            } else if (strcmp(st, "c") == 0) {
                if (!gpsAidActive) { reply(cmd, id, "no transfer"); return; }
                uint8_t bin[192];
                const int n = base64Decode(doc["d"] | "", bin, sizeof(bin));
                if (n > 0 && (size_t)n > GPS_Utils::aidRoom()) { reply(cmd, id, "busy"); return; }   // la app repite este trozo
                if (n <= 0 || !GPS_Utils::aidWrite(bin, (size_t)n)) { gpsAidActive = false; reply(cmd, id, "bad data"); return; }
                gpsAidBytes += (uint32_t)n;
            } else if (strcmp(st, "end") == 0 || strcmp(st, "abort") == 0) {
                if (gpsAidActive && st[0] == 'e')
                    logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "GPS", "A-GPS from the app: %lu bytes", (unsigned long)gpsAidBytes);
                if (st[0] == 'a') GPS_Utils::aidClear();
                gpsAidActive = false;
            } else { reply(cmd, id, "bad step"); return; }
            reply(cmd, id);
        }
        else if (strcmp(cmd, "satdata") == 0) {                   // sube sats.txt desde el movil: s = begin (n = bytes) | c (d = texto) | end | abort
            #ifdef HAS_SAT
                const char* st = doc["s"] | "";
                if (strcmp(st, "begin") == 0) {
                    if (!SAT_Utils::dataBegin((size_t)(doc["n"] | 0))) { reply(cmd, id, "cannot start (busy or too big)"); return; }
                } else if (strcmp(st, "c") == 0) {
                    const char* d = doc["d"] | "";
                    if (!SAT_Utils::dataChunk(d, strlen(d))) { reply(cmd, id, "chunk refused"); return; }
                } else if (strcmp(st, "end") == 0) {
                    if (!SAT_Utils::dataEnd()) { reply(cmd, id, SAT_Utils::updateMessage()); return; }
                    SATRX_Utils::stop();
                    reply(cmd, id);
                    sendSat();
                    return;
                } else if (strcmp(st, "abort") == 0) {
                    SAT_Utils::dataAbort();
                } else { reply(cmd, id, "bad step"); return; }
                reply(cmd, id);
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
                if (doc["ff"].is<const char*>()) {                                  // fuente 4: filtro de indicativos
                    String f = doc["ff"].as<const char*>();
                    f.trim(); f.toUpperCase();
                    Config.rotor.followFilter = f.substring(0, 80);
                    changed = true;
                }
                if (!doc["fm"].isNull()) { Config.rotor.followMaxMin = constrain((int)(doc["fm"] | 30), 1, 720); changed = true; }
                if (!doc["pr"].isNull()) { Config.rotor.predict = doc["pr"] | true; changed = true; }
                if (!doc["pm"].isNull()) { Config.rotor.predictMaxS = constrain((int)(doc["pm"] | 300), 10, 1800); changed = true; }
                if (!doc["src"].isNull()) {
                    const int src = doc["src"] | 0;
                    if (!RotorFollow::validSource(src)) { reply(cmd, id, "bad src"); return; }
                    Config.rotor.source = src;
                    changed = true;
                }
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
        else if (strcmp(cmd, "actlist") == 0) sendActList();
        else if (strcmp(cmd, "act") == 0) {                       // cualquier opcion de los menus del equipo, por nombre (ver actlist)
            #ifdef HAS_APRS_PAGES
                const char* name = doc["a"] | "";
                const char* error = "";
                const int result = PAGES_Utils::remoteAct(name, &error);
                if (result < 0) { reply(cmd, id, error); return; }
                if (result == PAGES_CMD_BEACON) {
                    PAGES_Utils::requestBeacon();
                    sendUpdate = true;
                } else if (result == PAGES_CMD_PROFILE) {
                    setProfile(myBeaconsSize > 0 ? (myBeaconsIndex + 1) % myBeaconsSize : -1, cmd, id);
                    return;
                } else if (result == PAGES_CMD_DISPLAY_OFF) {
                    displayToggle(false);
                    displayState = false;
                } else if (result == PAGES_CMD_GPS) {
                    SLEEP_Utils::toggleGpsManual();
                }
                reply(cmd, id);
            #else
                reply(cmd, id, "not available");
            #endif
        }
        else if (strcmp(cmd, "screen") == 0) {                    // espejo de la pantalla; p = pagina a mostrar (opcional), every = segundos para enviarla sola (0 = no)
            #ifdef HAS_APRS_PAGES
                const int p = doc["p"] | -1;
                if (p >= 0 && !PAGES_Utils::selectPage((uint8_t)p)) { reply(cmd, id, "page not available"); return; }
                const int every = doc["every"] | -1;
                if (every >= 0) screenEveryMs = every == 0 ? 0 : (uint32_t)max(every, 1) * 1000UL;
                if (p >= 0 || every >= 0) reply(cmd, id);
                sendScreen();
            #else
                reply(cmd, id, "not available");
            #endif
        }
        else if (strcmp(cmd, "msgs") == 0) sendMsgs(max(0, (int)(doc["from"] | 0)), constrain((int)(doc["cnt"] | 10), 1, 20));
        else if (strcmp(cmd, "msg") == 0) {                       // envia un mensaje APRS: to, t, ack (true = pide confirmacion)
            #ifdef HAS_APRS_PAGES
                const char* to = doc["to"] | "";
                const char* text = doc["t"] | "";
                if (!aprsMode(Config.operatingMode)) { reply(cmd, id, "only in APRS modes"); return; }
                if (strlen(to) < 3 || strlen(to) > 9 || strlen(text) == 0 || strlen(text) > 67) { reply(cmd, id, "bad callsign or text (max 67)"); return; }
                const int ack = PAGES_Utils::sendMessage(to, text, doc["ack"] | true);
                if (ack < 0) { reply(cmd, id, "already queued"); return; }
                JsonDocument r;
                r["e"] = "ok"; r["c"] = cmd; if (id >= 0) r["id"] = id; r["ack"] = ack;
                sendDoc(r);
            #else
                reply(cmd, id, "not available");
            #endif
        }
        else if (strcmp(cmd, "m17sms") == 0) {                    // SMS M17 (protocolo 0x05, sin reintentos) a otra radio M17: to (vacio = @ALL), t
            #ifdef HAS_SA868
                const char* to = doc["to"] | "";
                const char* text = doc["t"] | "";
                if (Config.operatingMode != MODE_M17) { reply(cmd, id, "not in M17 mode"); return; }
                if (strlen(to) > 9 || strlen(text) == 0 || strlen(text) > 800) { reply(cmd, id, "bad callsign or text (max 800)"); return; }
                for (const char* q = text; *q; q++) if ((uint8_t)*q < 0x20) { reply(cmd, id, "text must be one line"); return; }
                if (!SA868::m17Sms(to, text)) { reply(cmd, id, "not sent (callsign not valid for M17?)"); return; }
                reply(cmd, id);
            #else
                reply(cmd, id, "not available");
            #endif
        }
        else if (strcmp(cmd, "fm") == 0) {                        // estado del modo FM (evento "fm")
            #ifdef HAS_SA868
                if (Config.operatingMode != MODE_FM) { reply(cmd, id, "not in FM mode"); return; }
                sendFm();
            #else
                reply(cmd, id, "not available");
            #endif
        }
        else if (strcmp(cmd, "fmset") == 0) {                     // canal del VFO (f, sh, ofs, tm, t, d, inv, w, hp) y opciones (sq, sqa, step, nr, aprs, qsyb, edit)
            #ifdef HAS_SA868
                if (Config.operatingMode != MODE_FM) { reply(cmd, id, "not in FM mode"); return; }
                const bool chan = !doc["f"].isNull() || !doc["sh"].isNull() || !doc["ofs"].isNull() || !doc["tm"].isNull() || !doc["t"].isNull() ||
                                  !doc["d"].isNull() || !doc["inv"].isNull() || !doc["w"].isNull() || !doc["hp"].isNull();
                if (chan && !FM_Utils::setChannel(channelFrom(doc))) { reply(cmd, id, "out of band or ARTS on"); return; }
                if (!doc["sq"].isNull()) { Config.sa868.squelch = constrain((int)(doc["sq"] | 1), 0, 8); Config.fm.squelchAuto = false; }
                if (!doc["sqa"].isNull()) Config.fm.squelchAuto = doc["sqa"] | true;
                if (!doc["step"].isNull()) Config.fm.stepIndex = constrain((int)(doc["step"] | 3), 0, FmCore::STEP_COUNT - 1);
                if (!doc["nr"].isNull()) Config.fm.nrLevel = constrain((int)(doc["nr"] | 0), 0, 3);
                if (!doc["aprs"].isNull()) Config.fm.aprsBeacon = doc["aprs"] | true;
                if (!doc["qsyb"].isNull()) Config.fm.qsyInBeacon = doc["qsyb"] | true;
                if (!doc["edit"].isNull()) FM_Utils::setEdit((uint8_t)(doc["edit"] | 0));
                if (!doc["sq"].isNull() || !doc["sqa"].isNull() || !doc["step"].isNull() || !doc["nr"].isNull() || !doc["aprs"].isNull() || !doc["qsyb"].isNull()) {
                    Config.writeFile();
                    SA868::fmApply(FM_Utils::channel(), Config.sa868.squelch);
                }
                reply(cmd, id);
                sendFm();
            #else
                reply(cmd, id, "not available");
            #endif
        }
        else if (strcmp(cmd, "fmdial") == 0) {                    // como girar el encoder: d = pasos (+ / -) de lo que se este editando
            #ifdef HAS_SA868
                if (Config.operatingMode != MODE_FM) { reply(cmd, id, "not in FM mode"); return; }
                FM_Utils::dial(constrain((int)(doc["d"] | 0), -50, 50));
                reply(cmd, id);
            #else
                reply(cmd, id, "not available");
            #endif
        }
        else if (strcmp(cmd, "fmmem") == 0) {                     // memorias: op = list | recall (i) | store (i, n) | del (i) | vfo
            #ifdef HAS_SA868
                const char* op = doc["op"] | "list";
                const int i = doc["i"] | -1;
                if (strcmp(op, "list") == 0) { sendFmMems(); return; }
                if (Config.operatingMode != MODE_FM) { reply(cmd, id, "not in FM mode"); return; }
                bool good = true;
                if (strcmp(op, "recall") == 0) good = FM_Utils::recall(i);
                else if (strcmp(op, "store") == 0) good = FM_Utils::store(i, doc["n"] | "");
                else if (strcmp(op, "del") == 0) good = FM_Utils::erase(i);
                else if (strcmp(op, "vfo") == 0) { if (FM_Utils::memMode()) FM_Utils::toggleVfoMem(); }
                else { reply(cmd, id, "bad op"); return; }
                if (!good) { reply(cmd, id, "bad memory"); return; }
                reply(cmd, id);
                if (strcmp(op, "store") == 0 || strcmp(op, "del") == 0) sendFmMems();
                sendFm();
            #else
                reply(cmd, id, "not available");
            #endif
        }
        else if (strcmp(cmd, "fmscan") == 0 || strcmp(cmd, "fmarts") == 0 || strcmp(cmd, "fmeai") == 0) {     // s = start | stop
            #ifdef HAS_SA868
                if (Config.operatingMode != MODE_FM) { reply(cmd, id, "not in FM mode"); return; }
                const bool start = strcmp(doc["s"] | "start", "stop") != 0;
                if (strcmp(cmd, "fmscan") == 0) {
                    if (!doc["mem"].isNull()) Config.fm.scanMemories = doc["mem"] | true;
                    if (!doc["res"].isNull()) Config.fm.scanResume = constrain((int)(doc["res"] | 0), 0, 2);
                    if (start) FM_Utils::scanStart(); else FM_Utils::scanStop();
                } else if (strcmp(cmd, "fmarts") == 0) {
                    if (start != FM_Utils::artsOn()) FM_Utils::artsToggle();
                } else {
                    if (start) FM_Utils::eaiStart(); else FM_Utils::eaiStop();
                }
                reply(cmd, id);
                sendFm();
            #else
                reply(cmd, id, "not available");
            #endif
        }
        else if (strcmp(cmd, "qsy") == 0) {                       // QSY: c = comentario APRS ("146.940MHz T100 -060") o f, ofs (con signo), tm, t, d, w
            #ifdef HAS_SA868
                FmCore::Qsy q;
                memset(&q, 0, sizeof(q));
                const char* c = doc["c"] | "";
                if (c[0]) { if (!FmCore::parseQsy(doc["name"].as<const char*>(), c, q)) { reply(cmd, id, "no frequency in the text"); return; } }
                else {
                    q.hz = doc["f"] | 0L;
                    if (!doc["ofs"].isNull()) { q.hasOffset = true; q.offsetHz = doc["ofs"] | 0L; }
                    q.toneMode = (uint8_t)constrain((int)(doc["tm"] | 0), 0, FmCore::TONE_MODES - 1);
                    q.tone = doc["t"] | 885; q.dcs = doc["d"] | 23; q.narrow = !(doc["w"] | true);
                }
                FmCore::Channel ch;
                FmCore::qsyToChannel(q, Config.sa868.iaruRegion, ch);
                if (!SA868::validFreq(ch.rxHz) || !SA868::validFreq(FmCore::txHz(ch))) { reply(cmd, id, "not in the band of this radio"); return; }
                if (Config.operatingMode == MODE_FM) { if (!FM_Utils::qsy(q)) { reply(cmd, id, "refused (ARTS on?)"); return; } reply(cmd, id); sendFm(); return; }
                Config.fm.vfo.rxHz = (int)ch.rxHz; Config.fm.vfo.shift = ch.shift; Config.fm.vfo.offsetHz = (int)ch.offsetHz; Config.fm.vfo.toneMode = ch.toneMode;
                Config.fm.vfo.tone = ch.tone; Config.fm.vfo.dcs = ch.dcs; Config.fm.vfo.wide = ch.wide; Config.fm.vfo.name = "";
                Config.fm.memMode = false;
                Config.writeFile();
                bootModeValue = MODE_FM;                                    // arranca en FM en el canal anunciado (sin guardar el modo)
                bootModeMagic = BOOT_MODE_MAGIC;
                reply(cmd, id);
                restartAtMs = millis() + 500;
            #else
                reply(cmd, id, "not available");
            #endif
        }
        else if (strcmp(cmd, "carrier") == 0) {                   // portadora de calibracion (30 s como mucho): s = start | stop
            #ifdef HAS_SA868
                if (strcmp(doc["s"] | "start", "stop") == 0) { carrierUntil = 0; SA868::carrier(false); reply(cmd, id); return; }
                if (!SA868::carrier(true)) { reply(cmd, id, "radio busy"); return; }
                carrierUntil = millis() + 30000;
                JsonDocument r;
                r["e"] = "ok"; r["c"] = cmd; if (id >= 0) r["id"] = id; r["f"] = SA868::txFreqHz(); r["ppm"] = Config.radioPpm1000 / 1000.0;
                sendDoc(r);
            #else
                reply(cmd, id, "not available");
            #endif
        }
        else if (strcmp(cmd, "bcast") == 0) {                     // radio comercial: estado (evento "bcast"); sub = segundos entre eventos (0 = solo cambios)
            if (BCAST::chip() == BCAST::CHIP_NONE) { reply(cmd, id, "no broadcast receiver"); return; }
            if (!doc["sub"].isNull()) { const int s = doc["sub"] | 0; bcastEveryMs = s <= 0 ? 0 : (uint32_t)max(s, 1) * 1000UL; }
            sendBcast();
        }
        else if (strcmp(cmd, "bcastset") == 0) {                  // f (kHz), band, vol, mute, mono, on, step (+-n), seek (+1 / -1), preset: {op store|recall|del, i}
            if (BCAST::chip() == BCAST::CHIP_NONE) { reply(cmd, id, "no broadcast receiver"); return; }
            if (!doc["on"].isNull()) BCAST::power(doc["on"] | true);
            if (!doc["band"].isNull() && !BCAST::setBand((uint8_t)(doc["band"] | 0))) { reply(cmd, id, "band not available"); return; }
            if (!doc["f"].isNull() && !BCAST::tune((uint32_t)(doc["f"] | 0))) { reply(cmd, id, "cannot tune"); return; }
            if (!doc["step"].isNull()) BCAST::step(doc["step"] | 0);
            if (!doc["seek"].isNull()) BCAST::seek(doc["seek"] | 1);
            if (!doc["vol"].isNull()) BCAST::setVolume(doc["vol"] | 8);
            if (!doc["mute"].isNull()) BCAST::setMute(doc["mute"] | false);
            if (!doc["mono"].isNull()) BCAST::setMono(doc["mono"] | false);
            if (!doc["op"].isNull()) {
                const char* op = doc["op"] | "";
                const int i = doc["i"] | -1;
                const bool good = !strcmp(op, "store") ? BCAST::presetStore(i) : (!strcmp(op, "recall") ? BCAST::presetRecall(i) : (!strcmp(op, "del") ? BCAST::presetErase(i) : false));
                if (!good) { reply(cmd, id, "bad preset"); return; }
            }
            reply(cmd, id);
            sendBcast();
        }
        else if (strcmp(cmd, "heard") == 0) sendHeard(max(0, (int)(doc["from"] | 0)), constrain((int)(doc["cnt"] | 6), 1, 12));
        else if (strcmp(cmd, "cfgget") == 0) sendCfg((uint32_t)max(0, (int)(doc["o"] | 0)), id);
        else if (strcmp(cmd, "cfgset") == 0) {                    // sustituye /tracker_conf.json entero: s = begin (n = bytes, reboot) | c (d = texto) | end | abort
            const char* st = doc["s"] | "";
            if (strcmp(st, "begin") == 0) {
                cfgAbort();
                cfgExpect = (uint32_t)(doc["n"] | 0);
                if (cfgExpect < 2 || cfgExpect > 60000) { reply(cmd, id, "bad size"); return; }
                cfgReboot = doc["reboot"] | true;
                cfgUp = SPIFFS.open("/tracker_conf.up", "w");
                if (!cfgUp) { reply(cmd, id, "cannot write"); return; }
                cfgDepth = 0; cfgInString = cfgEscape = cfgBroken = cfgStarted = false;
            } else if (strcmp(st, "c") == 0) {
                const char* d = doc["d"] | "";
                const size_t n = strlen(d);
                if (!cfgUp || cfgGot + n > cfgExpect || cfgUp.write((const uint8_t*)d, n) != n) { cfgAbort(); reply(cmd, id, "chunk refused"); return; }
                cfgFeed(d);
                cfgGot += n;
            } else if (strcmp(st, "end") == 0) {
                const bool good = cfgUp && cfgGot == cfgExpect && !cfgBroken && cfgStarted && cfgDepth == 0 && !cfgInString;
                if (cfgUp) cfgUp.close();
                if (!good) { cfgAbort(); reply(cmd, id, "incomplete or malformed config"); return; }
                SPIFFS.remove("/tracker_conf.json");
                if (!SPIFFS.rename("/tracker_conf.up", "/tracker_conf.json")) { reply(cmd, id, "could not replace the config file"); return; }
                cfgExpect = cfgGot = 0;
                reply(cmd, id);
                if (cfgReboot) restartAtMs = millis() + 800;
                return;
            } else if (strcmp(st, "abort") == 0) {
                cfgAbort();
            } else { reply(cmd, id, "bad step"); return; }
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
                        if (!keyFastPath(line.text)) {
                            if (strstr(line.text, "\"cwrx\"") && strstr(line.text, "\"stop\"")) cwRxStop = true;   // la escucha CW bloquea el bucle: se mira aqui
                            xQueueSend(queue, &line, 0);   // cola llena: se pierde la orden (el movil reintenta)
                        }
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

extern bool messageLed;     // msg_utils.cpp: mensajes sin leer (LED de aviso)
extern bool flashlight;     // linterna manual

namespace BLE_Control {

    void sendLeds(bool force);

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
        ledSent = 0xFF;                                                 // las luces se mandan en la proxima vuelta
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
        skyEveryMs     = 0;
        screenEveryMs  = 0;
        cfgAbort();
    }

    void loop() {
        if (restartAtMs != 0 && (int32_t)(millis() - restartAtMs) >= 0) ESP.restart();
        if (queue == nullptr) return;
        Line line;
        while (xQueueReceive(queue, &line, 0) == pdTRUE) handle(line.text);
        GPS_Utils::aidPump();
        if (connected && authed) sendLeds(ledSent == 0xFF);
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
        checkSat();
        checkMsgs();
        checkBcast();
        #ifdef HAS_SA868
        checkFm();
        #endif
        if (!connected) return;
        const uint32_t now = millis();
        if (sensorsEveryMs && now - sensorsLastMs >= sensorsEveryMs) { sensorsLastMs = now; sendSensors(); }
        if (gnssEveryMs && now - gnssLastMs >= gnssEveryMs)          { gnssLastMs = now; sendGnss(); }
        if (skyEveryMs && now - skyLastMs >= skyEveryMs)             { skyLastMs = now; sendSky(); }
        if (screenEveryMs && now - screenLastMs >= screenEveryMs)    { screenLastMs = now; sendScreen(); }
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

    bool cwRxStopRequested() { return cwRxStop; }

    void aisEvent(const AisCore::Vessel& v) {
        if (!connected || !authed) return;
        AisCore::Own o; AIS_Utils::own(o);
        JW j("ais");
        vesselJson(j, v, o, true);
        j.send(sendLine);
    }

    bool netRequest(const char* what) {
        if (!connected || !authed) return false;
        JW j("netreq");
        j.text("what", what);
        j.send(sendLine);
        return true;
    }

    void cwRxEvent(bool on, char c, float dbm, float snr, int wpm) {
        if (!connected || !authed) return;
        JW j("cwrx");
        j.flag("on", on);
        if (c) { const char t[2] = { c, 0 }; j.text("t", t); }
        j.flt("dbm", dbm, 0);
        j.flt("snr", snr, 0);
        j.num("wpm", wpm);
        if (on && !c) j.num("f", LoRa_Utils::liveFreqHz());          // frecuencia de escucha (Hz): la app la usa en los spots CW
        j.send(sendLine);
    }

    // Luces de la app (banda A): TX / RX al momento; MSG (mensajes sin leer), EMG (alarma de emergencia) y FL (flash blanco) cuando cambian
    void rfEvent(char state, int rssi, float snr) {
        if (!connected || !authed) return;
        JW j("rf");
        j.text("s", state == 't' ? "tx" : state == 'r' ? "rx" : "idle");
        if (state == 'r') { j.num("rssi", rssi); j.flt("snr", snr, 1); }
        j.send(sendLine);
    }

    static uint32_t ledPollMs = 0;

    void sendLeds(bool force) {
        const uint32_t now = millis();
        if (!force && now - ledPollMs < 100) return;
        ledPollMs = now;
        bool emg = false;
        const char* call = "";
        const char* text = "";
        #ifdef HAS_APRS_PAGES
            emg = PAGES_Utils::alarmInfo(call, text);
        #endif
        const bool fl = NOTIFICATION_Utils::lampBusy() || flashlight;
        const uint8_t st = (messageLed ? 1 : 0) | (emg ? 2 : 0) | (fl ? 4 : 0);
        if (!force && st == ledSent) return;
        ledSent = st;
        JW j("led");
        j.flag("msg", messageLed);
        j.num("unread", MSG_Utils::getNumAPRSMessages());
        j.flag("emg", emg);
        if (emg) { j.text("ec", call); j.text("et", text); }
        j.flag("fl", fl);
        j.send(sendLine);
    }

    void setBootMode(int mode) {                                        // modo para el siguiente arranque, sin guardarlo (QSY a FM...)
        bootModeValue = mode;
        bootModeMagic = BOOT_MODE_MAGIC;
    }

    int takeBootMode() {
        if (bootModeMagic != BOOT_MODE_MAGIC) return -1;
        bootModeMagic = 0;
        if (bootModeValue < MODE_APRS_LORA || bootModeValue > MODE_AIS) return -1;
        return (int)bootModeValue;
    }

}
