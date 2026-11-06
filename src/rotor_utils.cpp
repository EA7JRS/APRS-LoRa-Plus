/*
 * Control de rotor (modo SAT). Ver include/rotor_utils.h
 *
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#include "sat_config.h"
#include "sat_utils.h"
#include "rotor_utils.h"

#ifdef HAS_SAT

#include <Arduino.h>
#include <WiFi.h>
#include <TimeLib.h>
#include <logger.h>
#include "rotor_utils.h"
#include "sat_utils.h"
#include "configuration.h"
#include "board_pinout.h"

#ifdef HAS_BT_CLASSIC
    #include <BluetoothSerial.h>
    extern BluetoothSerial SerialBT;
    extern bool            bluetoothActive;
#endif

extern Configuration    Config;
extern logging::Logger  logger;

namespace ROTOR_Utils {

    namespace {

        portMUX_TYPE   mux = portMUX_INITIALIZER_UNLOCKED;
        float          tgtAz = 0, tgtEl = 0;
        bool           tgtTracking = false;
        bool           tgtValid = false;
        volatile uint32_t seq = 0;                  // objetivo nuevo para la tarea
        volatile State st = ST_OFF;
        char           msg[32] = "OFF";
        float          actAz = 0, actEl = 0;
        volatile bool  actOk = false;
        volatile bool  stopTask = false;
        TaskHandle_t   task = nullptr;
        uint32_t       lastCalcMs = 0;
        uint32_t       lastSentSeq = 0;

        // ultimo objetivo enviado (para no repetir ordenes por cambios menores que el paso)
        float          sentAz = -999, sentEl = -999;
        uint32_t       sentMs = 0;
        bool           parked = false;

        void setState(State s, const char* m) { st = s; snprintf(msg, sizeof(msg), "%s", m); }

        float wrap360(float a) { while (a < 0) a += 360.0f; while (a >= 360.0f) a -= 360.0f; return a; }

        // ---- TCP: rotctld ----
        // Lee una linea (hasta \n) con tiempo maximo; false si no llega
        bool readLine(WiFiClient& c, char* out, size_t n, uint32_t timeoutMs) {
            size_t i = 0;
            const uint32_t t0 = millis();
            while (millis() - t0 < timeoutMs) {
                while (c.available()) {
                    const int ch = c.read();
                    if (ch == '\n') { out[i] = '\0'; return true; }
                    if (ch != '\r' && i < n - 1) out[i++] = (char)ch;
                }
                if (!c.connected()) break;
                delay(10);
            }
            out[i] = '\0';
            return false;
        }

        bool ensureWifi() {
            if (WiFi.status() == WL_CONNECTED) return true;
            if (Config.fwUpdate.ssid.isEmpty()) { setState(ST_ERROR, "Set WiFi in WebUI"); return false; }
            setState(ST_WAIT, "WiFi...");
            const wifi_mode_t m = WiFi.getMode();
            WiFi.mode((m == WIFI_MODE_AP || m == WIFI_MODE_APSTA) ? WIFI_MODE_APSTA : WIFI_MODE_STA);
            WiFi.begin(Config.fwUpdate.ssid.c_str(), Config.fwUpdate.password.c_str());
            const uint32_t t0 = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000 && !stopTask) delay(100);
            if (WiFi.status() != WL_CONNECTED) { setState(ST_ERROR, "WiFi: not joined"); return false; }
            return true;
        }

        void tcpLoop() {
            WiFiClient client;
            uint32_t lastPoll = 0;
            while (!stopTask) {
                if (!client.connected()) {
                    client.stop();
                    actOk = false;
                    if (!ensureWifi()) { for (int i = 0; i < 50 && !stopTask; i++) delay(100); continue; }
                    setState(ST_CONNECTING, "Connecting");
                    if (Config.rotor.host.isEmpty() || !client.connect(Config.rotor.host.c_str(), Config.rotor.port, 3000)) {
                        setState(ST_ERROR, "No rotctld");
                        client.stop();
                        for (int i = 0; i < 50 && !stopTask; i++) delay(100);
                        continue;
                    }
                    client.setNoDelay(true);
                    sentAz = sentEl = -999;
                    lastSentSeq = 0;
                    setState(ST_OK, "OK");
                }
                // nuevo objetivo
                uint32_t s = seq;
                if (s != lastSentSeq) {
                    float az, el;
                    portENTER_CRITICAL(&mux); az = tgtAz; el = tgtEl; portEXIT_CRITICAL(&mux);
                    lastSentSeq = s;
                    char line[40], reply[40];
                    snprintf(line, sizeof(line), "P %.1f %.1f\n", az, el);
                    while (client.available()) client.read();
                    client.print(line);
                    if (readLine(client, reply, sizeof(reply), 2000) && strncmp(reply, "RPRT 0", 6) == 0) {
                        setState(ST_OK, "OK");
                    } else if (reply[0]) {
                        setState(ST_ERROR, reply[0] == 'R' ? reply : "Bad reply");
                    } else {
                        setState(ST_ERROR, "No answer");
                        client.stop();
                    }
                }
                // posicion real cada 3 s
                if (client.connected() && millis() - lastPoll > 3000) {
                    lastPoll = millis();
                    char l1[24], l2[24];
                    while (client.available()) client.read();
                    client.print("p\n");
                    if (readLine(client, l1, sizeof(l1), 1500) && readLine(client, l2, sizeof(l2), 1500)) {
                        actAz = atof(l1); actEl = atof(l2); actOk = true;
                    } else actOk = false;
                }
                delay(100);
            }
            client.stop();
        }

        // ---- Bluetooth SPP: Yaesu GS-232 ----
        void btLoop() {
        #ifdef HAS_BT_CLASSIC
            if (bluetoothActive) { setState(ST_ERROR, "BT in use (phone)"); while (!stopTask) delay(200); return; }
            if (Config.rotor.btName.isEmpty()) { setState(ST_ERROR, "Set BT name"); while (!stopTask) delay(200); return; }
            bool master = false;
            while (!stopTask) {
                if (!master) {
                    setState(ST_CONNECTING, "BT...");
                    if (!SerialBT.begin("APRSLoRa-rotor", true)) { setState(ST_ERROR, "BT start failed"); for (int i = 0; i < 50 && !stopTask; i++) delay(100); continue; }
                    master = true;
                }
                if (!SerialBT.connected()) {
                    setState(ST_CONNECTING, "BT connecting");
                    if (!SerialBT.connect(Config.rotor.btName)) { setState(ST_ERROR, "BT no link"); for (int i = 0; i < 80 && !stopTask; i++) delay(100); continue; }
                    sentAz = sentEl = -999;
                    lastSentSeq = 0;
                    setState(ST_OK, "OK");
                }
                uint32_t s = seq;
                if (s != lastSentSeq) {
                    float az, el;
                    portENTER_CRITICAL(&mux); az = tgtAz; el = tgtEl; portEXIT_CRITICAL(&mux);
                    lastSentSeq = s;
                    char line[24];
                    snprintf(line, sizeof(line), "W%03d %03d\r", (int)lroundf(az) % 360, constrain((int)lroundf(el), 0, 180));
                    SerialBT.print(line);
                }
                while (SerialBT.available()) SerialBT.read();               // respuestas del controlador: se descartan
                delay(100);
            }
            if (master) { SerialBT.disconnect(); SerialBT.end(); }
        #else
            setState(ST_ERROR, "No BT classic here");
            while (!stopTask) delay(200);
        #endif
        }

        void commTask(void*) {
            if (Config.rotor.transport == 1) btLoop(); else tcpLoop();
            setState(ST_OFF, "OFF");
            task = nullptr;
            vTaskDelete(nullptr);
        }

        void publish(float az, float el, bool tracking) {
            portENTER_CRITICAL(&mux);
            tgtAz = az; tgtEl = el; tgtTracking = tracking; tgtValid = true;
            portEXIT_CRITICAL(&mux);
            seq = seq + 1;
            sentAz = az; sentEl = el; sentMs = millis();
        }
    }

    bool enabled() { return Config.rotor.enabled; }
    State state() { return st; }
    const char* stateText() { return msg; }
    bool actual(float& az, float& el) { if (!actOk) return false; az = actAz; el = actEl; return true; }
    uint32_t targetSeq() { return seq; }

    bool target(float& az, float& el, bool& tracking) {
        bool v;
        portENTER_CRITICAL(&mux); az = tgtAz; el = tgtEl; tracking = tgtTracking; v = tgtValid; portEXIT_CRITICAL(&mux);
        return v;
    }

    void begin() {
        stopTask = true;                                    // por si habia una tarea de una entrada anterior
        for (int i = 0; i < 100 && task != nullptr; i++) delay(50);
        stopTask = false;
        tgtValid = false; parked = false; actOk = false; sentAz = sentEl = -999;
        if (!Config.rotor.enabled) { setState(ST_OFF, "OFF"); return; }          // sin transporte propio: solo el evento BLE para la app
        setState(ST_WAIT, "Starting");
        if (xTaskCreate(commTask, "rotor", 6144, nullptr, 1, &task) != pdPASS) { task = nullptr; setState(ST_ERROR, "Out of memory"); }
    }

    void loop() {
        if (!Config.rotor.enabled && !Config.rotor.bleEvents) return;
        const uint32_t ms = millis();
        if (ms - lastCalcMs < 1000) return;
        lastCalcMs = ms;

        const float step = Config.rotor.stepTenths / 10.0f;
        float az = 0, el = 0;
        bool tracking = false, have = false;

        SatCore::Look lk;
        if (SAT_Utils::live(lk) && lk.elDeg >= 0.0) {
            az = (float)lk.azDeg; el = (float)lk.elDeg; tracking = true; have = true; parked = false;
        } else {
            SatCore::Pass ps; bool searching = false;
            if (SAT_Utils::nextPass(ps, searching)) {
                const double dt = ps.aosUnix - (double)now();
                if (dt > 0 && dt <= Config.rotor.preSec) { az = (float)ps.aosAzDeg; el = 0; have = true; parked = false; }     // se coloca antes de que salga
            }
            if (!have && Config.rotor.park && !parked && SAT_Utils::haveTime()) {                    // tras el paso: posicion de reposo
                az = (float)Config.rotor.parkAz; el = (float)Config.rotor.parkEl; have = true; parked = true;
            }
        }
        if (!have) return;
        az = wrap360(az + Config.rotor.azOffset);
        el = constrain(el + Config.rotor.elOffset, 0.0f, 90.0f);

        float da = fabsf(az - sentAz); if (da > 180.0f) da = 360.0f - da;
        const bool moved = sentAz < -900 || da >= step || fabsf(el - sentEl) >= step;
        if (moved || ms - sentMs > 10000) publish(az, el, tracking);
    }

}

#else   // sin HAS_SAT: funciones vacias

namespace ROTOR_Utils {
    void begin() {}
    void loop() {}
    bool target(float&, float&, bool& tracking) { tracking = false; return false; }
    State state() { return ST_OFF; }
    const char* stateText() { return "OFF"; }
    bool actual(float&, float&) { return false; }
    bool enabled() { return false; }
    uint32_t targetSeq() { return 0; }
}

#endif  // HAS_SAT
