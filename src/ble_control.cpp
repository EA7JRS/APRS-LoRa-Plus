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
        void num(const char* k, long v) { key(k); s += v; }
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
        if (qnh > 0.0f) j.flt("qnh", qnh);
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

    // ---- Modo SAT para la app: estado, lista, transmisores y subida del archivo de datos ----
    void sendSat() {
        #ifdef HAS_SAT
            const SatCore::Block* b = SAT_Utils::block();
            JsonDocument doc;
            doc["e"]   = "sat";
            doc["ok"]  = SAT_Utils::hasData() && b != nullptr;
            if (b) {
                doc["n"]   = b->name;
                doc["id"]  = b->noradId;
                doc["i"]   = SAT_Utils::selected();
                doc["geo"] = SAT_Utils::geo();
                doc["tx"]  = b->txCount;
                doc["tle"] = serialized(String(SAT_Utils::tleAgeDays(), 1));
            }
            doc["pos"] = SAT_Utils::haveQth() ? SAT_Utils::qthSource() : "";
            SatCore::Look lk;
            if (b && SAT_Utils::live(lk)) {
                doc["az"]  = serialized(String(lk.azDeg, 1));
                doc["el"]  = serialized(String(lk.elDeg, 1));
                doc["rng"] = (int)lround(lk.rangeKm);
                doc["rr"]  = serialized(String(lk.rangeRateKmS, 3));
                SatCore::Pass ps; bool searching = false;
                if (!SAT_Utils::geo() && SAT_Utils::nextPass(ps, searching)) {
                    doc["aos"] = (long)ps.aosUnix; doc["los"] = (long)ps.losUnix; doc["max"] = (int)lround(ps.maxElDeg);
                }
            }
            sendDoc(doc);
        #endif
    }

    void checkSat() {
        #ifdef HAS_SAT
            if (!connected || !authed || Config.operatingMode != MODE_SAT) return;
            static uint32_t lastMs = 0;
            const uint32_t now = millis();
            if (now - lastMs < 2000) return;
            lastMs = now;
            sendSat();
        #endif
    }

    void sendSatList(int from, int cnt) {
        #ifdef HAS_SAT
            JsonDocument doc;
            doc["e"]   = "satlist";
            doc["tot"] = SAT_Utils::count();
            doc["sel"] = SAT_Utils::selected();
            doc["from"] = from;
            JsonArray items = doc["items"].to<JsonArray>();
            for (int i = from; i < SAT_Utils::count() && (int)items.size() < cnt; i++) {
                const SAT_Utils::Entry* e = SAT_Utils::at(i);
                if (!e) break;
                JsonObject o = items.add<JsonObject>();
                o["i"] = i; o["id"] = e->noradId; o["n"] = e->name; o["c"] = String(e->cat); o["t"] = e->txCount;
            }
            sendDoc(doc);
        #else
            (void)from; (void)cnt;
        #endif
    }

    void sendSatTx() {
        #ifdef HAS_SAT
            const SatCore::Block* b = SAT_Utils::block();
            JsonDocument doc;
            doc["e"] = "sattxlist";
            JsonArray items = doc["items"].to<JsonArray>();
            if (b) {
                for (int i = 0; i < b->txCount; i++) {
                    const SatCore::Transmitter& t = b->tx[i];
                    JsonObject o = items.add<JsonObject>();
                    o["i"] = i; o["m"] = t.mode; o["b"] = t.baud; o["d"] = t.desc;
                    o["up"] = (long long)SatCore::txUpCenter(t); o["dn"] = (long long)SatCore::txDownCenter(t);
                    o["rx"] = SATRX_Utils::kindText(SATRX_Utils::classify(t));
                    o["tx"] = SATRX_Utils::kindText(SATTX_Utils::classifyUp(t));
                    double rx = 0, tx = 0;
                    SAT_Utils::doppler(t, rx, tx);
                    if (rx > 0) o["rxd"] = (long long)llround(rx);
                    if (tx > 0) o["txd"] = (long long)llround(tx);
                }
            }
            sendDoc(doc);
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
            const int sc = doc["screen"] | -1;
            if (sc >= 0) screenEveryMs = sc == 0 ? 0 : (uint32_t)max(sc, 1) * 1000UL;
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
        else if (strcmp(cmd, "sat") == 0) { sendSat(); }
        else if (strcmp(cmd, "satlist") == 0) { sendSatList(max(0, (int)(doc["from"] | 0)), constrain((int)(doc["cnt"] | 20), 1, 40)); }
        else if (strcmp(cmd, "sattxlist") == 0) { sendSatTx(); }
        else if (strcmp(cmd, "satsel") == 0) {                    // elige el satelite: i = indice de la lista, o id = numero NORAD
            #ifdef HAS_SAT
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
        screenEveryMs  = 0;
        cfgAbort();
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
        checkSat();
        checkMsgs();
        if (!connected) return;
        const uint32_t now = millis();
        if (sensorsEveryMs && now - sensorsLastMs >= sensorsEveryMs) { sensorsLastMs = now; sendSensors(); }
        if (gnssEveryMs && now - gnssLastMs >= gnssEveryMs)          { gnssLastMs = now; sendGnss(); }
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

    int takeBootMode() {
        if (bootModeMagic != BOOT_MODE_MAGIC) return -1;
        bootModeMagic = 0;
        if (bootModeValue < MODE_APRS_LORA || bootModeValue > MODE_FST4W) return -1;
        return (int)bootModeValue;
    }

}
