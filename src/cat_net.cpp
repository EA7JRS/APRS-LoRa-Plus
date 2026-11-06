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

#include "cat_net.h"

#include <Arduino.h>
#include <string>
#include "configuration.h"
#include "lora_utils.h"
#include "cat_proto.h"
#include "sat_config.h"
#include "sat_rx.h"
#include "sat_tx.h"
#include "sleep_utils.h"
#include "sat_utils.h"
#include "fw_update.h"
#include "logger.h"
#if KISS_NET_SUPPORTED
    #include <WiFi.h>
    #include <ESPmDNS.h>
    #include "kiss_stream.h"
#endif
#ifdef HAS_APRS_PAGES
    #include "pages_utils.h"
#endif

extern Configuration    Config;
extern logging::Logger  logger;
extern LoraType         *currentLoRaType;
extern bool             sendUpdate;
extern bool             gpsIsActive;

namespace {

    // La radio del tracker vista por el protocolo (include/cat_proto.h)
    class TrackerRig : public CatProto::Rig {
    public:
        int rssi = -127;

        uint32_t splitFreq = 0;

        // Modo SAT (GPredict): la frecuencia es la de la bajada del satelite que se escucha; GPredict manda la ya corregida por Doppler
        static bool satMode() {
            #ifdef HAS_SAT
                return Config.operatingMode == MODE_SAT;
            #else
                return false;
            #endif
        }

        uint32_t freqHz() override {
            #ifdef HAS_SAT
                if (satMode()) return SATRX_Utils::currentDownHz();
            #endif
            return (uint32_t)LoRa_Utils::liveFreqHz();
        }

        bool setSplitHz(uint32_t hz) override {
            if (!satMode()) return false;
            if (hz < 400000000UL || hz > 510000000UL) return false;
            splitFreq = hz;                                     // subida del satelite que calcula GPredict: informativa (el tracker precompensa la suya)
            return true;
        }
        uint32_t splitHz() override { return satMode() ? splitFreq : 0; }

        bool setFreqHz(uint32_t hz) override {
            #ifdef HAS_SAT
                if (satMode()) return SATRX_Utils::setExternalHz(hz);
            #endif
            const long base = LoRa_Utils::profileFreqHz();
            const long win  = (long)Config.cat.tuneKHz * 1000L;
            if ((long)hz < base - win || (long)hz > base + win) return false;
            if ((long)hz == (long)freqHz()) return true;
            return LoRa_Utils::setLiveFreqHz((long)hz);
        }

        bool isFsk() override { return Config.operatingMode == MODE_APRS_FSK; }
        bool setFsk(bool fsk) override { return fsk == isFsk(); }       // el modem se elige al arrancar (modo APRS LoRa / APRS FSK): no cambia en caliente

        int passbandHz() override {
            if (isFsk()) return (int)(currentLoRaType->fskRxBandwidth * 1000.0f);
            return (int)currentLoRaType->signalBandwidth;
        }

        int rssiDbm() override {
            #ifdef HAS_SAT
                if (satMode()) return (int)lroundf(SATRX_Utils::rssi());
            #endif
            return rssi;
        }
        int powerDbm() override { return LoRa_Utils::getPower(); }

        bool setPowerDbm(int dbm) override {
            if (dbm < LoRa_Utils::powerMin() || dbm > LoRa_Utils::powerMax()) return false;
            if (dbm == LoRa_Utils::getPower()) return true;             // setProfilePower guarda en flash: solo si cambia
            LoRa_Utils::setProfilePower(dbm);
            return true;
        }

        int  powerMinDbm() override { return LoRa_Utils::powerMin(); }
        int  powerMaxDbm() override { return LoRa_Utils::powerMax(); }
        bool pttEnabled() override { return Config.cat.pttBeacon; }

        bool triggerBeacon() override {
            #ifdef HAS_SAT
                if (satMode()) {                                // PTT = emitir el mensaje configurado hacia el satelite (con todas las seguridades del modo SAT)
                    const SatCore::Block* b = SAT_Utils::block();
                    if (!b) return false;
                    uint8_t idx = SATRX_Utils::active() ? SATRX_Utils::txIndex() : 0;
                    for (uint8_t k = 0; k < b->txCount; k++) {
                        const uint8_t i = (uint8_t)((idx + k) % b->txCount);
                        if (SATTX_Utils::classifyUp(b->tx[i]) != SATRX_Utils::K_NO) { idx = i; break; }
                    }
                    return SATTX_Utils::send(idx, Config.sat.txText.c_str());
                }
            #endif
            #ifdef HAS_APRS_PAGES
                PAGES_Utils::requestBeacon();
            #endif
            sendUpdate = true;
            if (!gpsIsActive) SLEEP_Utils::gpsWakeUp();
            return true;
        }

        size_t ranges(CatProto::Range* out, size_t max) override {
            if (max == 0) return 0;
            if (satMode()) { out[0].lo = 400000000UL; out[0].hi = 510000000UL; return 1; }
            const long base = LoRa_Utils::profileFreqHz();
            const long win  = (long)Config.cat.tuneKHz * 1000L;
            out[0].lo = (uint32_t)(base - win);
            out[0].hi = (uint32_t)(base + win);
            return 1;
        }

        const char* info() override { return "APRS LoRa+ tracker (EA7JRS)"; }
    };

    TrackerRig              rig;
    CatProto::Session       usbSession;
    char                    usbLine[24];                        // detecta "APRS-INFO?" sin esperar al final de linea del protocolo
    uint8_t                 usbLineLen = 0;

}

#if KISS_NET_SUPPORTED

namespace {

    constexpr uint8_t MAX_CLIENTS = 2;

    struct CatClient {
        WiFiClient          socket;
        CatProto::Session   session;
    };

    CatClient       clients[MAX_CLIENTS];
    WiFiServer*     server = nullptr;

    void closeClient(CatClient& c) {
        c.socket.stop();
        c.session.reset();
    }

    void serve(CatClient& c) {
        uint8_t buffer[96];
        int guard = 4;                                          // trabajo acotado por pasada
        while (c.socket.available() > 0 && guard-- > 0) {
            const int n = c.socket.read(buffer, sizeof(buffer));
            if (n <= 0) break;
            std::string out;
            const bool close = c.session.feed((const char*)buffer, (size_t)n, rig, out);
            if (!out.empty()) c.socket.write((const uint8_t*)out.data(), out.size());
            if (close) { closeClient(c); return; }
        }
    }

}

namespace CAT_NET {

    void begin() {
        end();
        if (!Config.cat.enabled) return;
        server = new WiFiServer((uint16_t)Config.cat.port);
        server->begin();
        server->setNoDelay(true);
        MDNS.addService("rigctl", "tcp", (uint16_t)Config.cat.port);       // mDNS lo inicia kiss_net (mismo nombre aprs-lora-plus.local)
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "CAT", "TCP %s:%d", WiFi.localIP().toString().c_str(), Config.cat.port);
    }

    void end() {
        for (uint8_t i = 0; i < MAX_CLIENTS; i++) closeClient(clients[i]);
        if (server) { server->close(); delete server; server = nullptr; }
    }

    void poll() {
        if (server == nullptr) return;
        WiFiClient incoming = server->accept();
        if (incoming) {
            const String ip = incoming.remoteIP().toString();
            bool placed = false;
            if (KissNetUtil::ipAllowed(ip.c_str(), Config.cat.allowed.c_str())) {
                for (uint8_t i = 0; i < MAX_CLIENTS && !placed; i++) {
                    if (!clients[i].socket || !clients[i].socket.connected()) {
                        clients[i].socket = incoming;
                        clients[i].socket.setNoDelay(true);
                        clients[i].session.reset();
                        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "CAT", "Client %s", ip.c_str());
                        placed = true;
                    }
                }
            } else {
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "CAT", "Refused %s", ip.c_str());
            }
            if (!placed) incoming.stop();
        }
        for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
            CatClient& c = clients[i];
            if (!c.socket) continue;
            if (!c.socket.connected() && c.socket.available() == 0) { closeClient(c); continue; }
            serve(c);
        }
    }

    uint8_t clientCount() {
        uint8_t n = 0;
        for (uint8_t i = 0; i < MAX_CLIENTS; i++) if (clients[i].socket && clients[i].socket.connected()) n++;
        return n;
    }

}

#else

namespace CAT_NET {
    void begin() {}
    void end() {}
    void poll() {}
    uint8_t clientCount() { return 0; }
}

#endif

namespace CAT_NET {

    void onRx(int rssiDbm) {
        rig.rssi = rssiDbm;
    }

    bool usbActive() {
        return Config.cat.usb;
    }

    // CAT por USB: el mismo protocolo de texto que el servidor TCP, por el puerto serie. Las lineas "APRS-INFO?" (instalador web) se siguen contestando
    void usbPoll() {
        if (!Config.cat.usb) return;
        int guard = 128;                                            // trabajo acotado por pasada
        while (Serial.available() > 0 && guard-- > 0) {
            const int c = Serial.read();
            if (c < 0) break;
            if (c == '\n' || c == '\r') {
                usbLine[usbLineLen] = 0;
                if (usbLineLen > 0 && strcmp(usbLine, "APRS-INFO?") == 0) {
                    FW_Update::printBanner();
                    usbSession.reset();
                    usbLineLen = 0;
                    continue;
                }
                usbLineLen = 0;
            } else if (usbLineLen < sizeof(usbLine) - 1) {
                usbLine[usbLineLen++] = (char)c;
            }
            const char b = (char)c;
            std::string out;
            usbSession.feed(&b, 1, rig, out);                       // "q" en el puerto serie no cierra nada
            if (!out.empty()) Serial.write((const uint8_t*)out.data(), out.size());
        }
    }

}
