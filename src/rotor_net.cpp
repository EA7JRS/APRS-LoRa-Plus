/* Copyright (C) 2026 Manuel - EA7JRS
 * Servidor de rotor por WiFi (rotctld de Hamlib): ver include/rotor_net.h y include/rot_proto.h
 */
#include <Arduino.h>
#include "rotor_net.h"
#include "configuration.h"
#include "rotor_utils.h"
#include "rot_proto.h"
#include "kiss_net.h"
#include "logger.h"
#if KISS_NET_SUPPORTED
    #include <WiFi.h>
    #include <ESPmDNS.h>
    #include "kiss_stream.h"
#endif

extern Configuration    Config;
extern logging::Logger  logger;

#if KISS_NET_SUPPORTED

namespace {

    class TrackerRot : public RotProto::Rot {
    public:
        bool setPos(float az, float el) override {
            ROTOR_Utils::setExternal(az, el);
            return true;
        }
        bool getPos(float& az, float& el) override {
            if (ROTOR_Utils::actual(az, el)) return true;                 // posicion real del rotor
            return ROTOR_Utils::externalPos(az, el);                      // si no se conoce, la ultima pedida
        }
        bool park() override {
            ROTOR_Utils::setExternal((float)Config.rotor.parkAz, (float)Config.rotor.parkEl);
            return true;
        }
        const char* info() override { return "APRS LoRa+ rotor proxy (EA7JRS)"; }
    };

    constexpr uint8_t MAX_CLIENTS = 2;

    struct RotClient {
        WiFiClient          socket;
        RotProto::Session   session;
    };

    RotClient       clients[MAX_CLIENTS];
    WiFiServer*     server = nullptr;
    TrackerRot      rot;

    void closeClient(RotClient& c) {
        c.socket.stop();
        c.session.reset();
    }

    void serve(RotClient& c) {
        uint8_t buffer[96];
        int guard = 4;
        while (c.socket.available() > 0 && guard-- > 0) {
            const int n = c.socket.read(buffer, sizeof(buffer));
            if (n <= 0) break;
            std::string out;
            const bool close = c.session.feed((const char*)buffer, (size_t)n, rot, out);
            if (!out.empty()) c.socket.write((const uint8_t*)out.data(), out.size());
            if (close) { closeClient(c); return; }
        }
    }

}

namespace ROTOR_NET {

    void begin() {
        end();
        if (!Config.rotor.serverEnabled) return;
        server = new WiFiServer((uint16_t)Config.rotor.serverPort);
        server->begin();
        server->setNoDelay(true);
        MDNS.addService("rotctl", "tcp", (uint16_t)Config.rotor.serverPort);
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "ROTOR", "rotctl TCP %s:%d", WiFi.localIP().toString().c_str(), Config.rotor.serverPort);
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
            if (KissNetUtil::ipAllowed(ip.c_str(), Config.cat.allowed.c_str())) {       // la misma lista de direcciones permitidas que el CAT
                for (uint8_t i = 0; i < MAX_CLIENTS && !placed; i++) {
                    if (!clients[i].socket || !clients[i].socket.connected()) {
                        clients[i].socket = incoming;
                        clients[i].socket.setNoDelay(true);
                        clients[i].session.reset();
                        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "ROTOR", "Client %s", ip.c_str());
                        placed = true;
                    }
                }
            } else {
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "ROTOR", "Refused %s", ip.c_str());
            }
            if (!placed) incoming.stop();
        }
        for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
            RotClient& c = clients[i];
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

namespace ROTOR_NET {
    void begin() {}
    void end() {}
    void poll() {}
    uint8_t clientCount() { return 0; }
}

#endif
