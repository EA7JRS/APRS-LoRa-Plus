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

#include "kiss_net.h"

#if KISS_NET_SUPPORTED

#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>
#include "configuration.h"
#include "lora_utils.h"
#include "kiss_utils.h"
#include "kiss_stream.h"
#include "display.h"
#include "logger.h"

extern Configuration    Config;
extern logging::Logger  logger;

namespace {

    constexpr uint8_t   MAX_CLIENTS         = 3;
    constexpr uint8_t   MAX_FRAMES_PER_POLL = 2;            // each frame blocks the loop for its time on air
    constexpr uint32_t  RETRY_MS            = 20000UL;
    constexpr const char* KISS_HOST         = "aprs-lora-plus";

    struct Client {
        WiFiClient                  socket;
        KissNetUtil::FrameSplitter  splitter;
    };

    Client          clients[MAX_CLIENTS];
    WiFiServer*     server          = nullptr;
    WiFiUDP         udp;
    bool            udpOn           = false;
    IPAddress       udpPeer;
    uint16_t        udpPeerPort     = 0;
    bool            running         = false;                // enabled and started
    bool            linkUp          = false;
    uint32_t        lastRetryMs     = 0;

    bool modeOk() {
        return (Config.operatingMode == MODE_APRS_LORA || Config.operatingMode == MODE_APRS_FSK) && !Config.wifiAP.active;      // FSK: la misma cabecera y el mismo envio que LoRa (lora_utils)
    }

    // KISS data frame received from a client -> radio
    void toRadio(const std::string& raw, const char* origin) {
        String frame;
        frame.reserve(raw.size());
        for (size_t i = 0; i < raw.size(); i++) frame += raw[i];
        bool isData = false;
        const String tnc2 = KISS_Utils::decodeKISS(frame, isData);
        if (!isData || tnc2.isEmpty()) return;               // KISS settings commands (TXDELAY, persistence...) are accepted and ignored
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "KISS WiFi", "%s Tx %s", origin, tnc2.c_str());
        displayShow("WiFi Tx >>", "", tnc2, 1000);
        LoRa_Utils::sendNewPacket(tnc2);
    }

    void startSockets() {
        if (server) { server->close(); delete server; server = nullptr; }
        server = new WiFiServer((uint16_t)Config.kissNet.tcpPort);
        server->begin();
        server->setNoDelay(true);
        if (Config.kissNet.udpPort > 0) udpOn = udp.begin((uint16_t)Config.kissNet.udpPort) != 0;
        MDNS.end();
        if (MDNS.begin(KISS_HOST)) MDNS.addService("kiss", "tcp", (uint16_t)Config.kissNet.tcpPort);
        const String ip = WiFi.localIP().toString();
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "KISS WiFi", "TCP %s:%d%s", ip.c_str(), Config.kissNet.tcpPort, udpOn ? " + UDP" : "");
        displayShow("KISS WiFi", "", "IP " + ip, "TCP " + String(Config.kissNet.tcpPort) + (udpOn ? "  UDP " + String(Config.kissNet.udpPort) : String("")), "", KISS_HOST, 2500);
    }

    void stopSockets() {
        for (uint8_t i = 0; i < MAX_CLIENTS; i++) { clients[i].socket.stop(); clients[i].splitter.clear(); }
        if (server) { server->close(); delete server; server = nullptr; }
        if (udpOn) { udp.stop(); udpOn = false; }
        MDNS.end();
    }

    void sendFrame(const String& kiss) {
        const uint8_t* data = (const uint8_t*)kiss.c_str();
        const size_t length = kiss.length();
        for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].socket && clients[i].socket.connected()) {
                if (clients[i].socket.write(data, length) == 0) { clients[i].socket.stop(); clients[i].splitter.clear(); }
            }
        }
        if (udpOn && udpPeerPort != 0) {                     // UDP: answers the last address that sent us a datagram
            udp.beginPacket(udpPeer, udpPeerPort);
            udp.write(data, length);
            udp.endPacket();
        }
    }

    void acceptClients() {
        WiFiClient incoming = server->accept();
        if (!incoming) return;
        const String ip = incoming.remoteIP().toString();
        if (!KissNetUtil::ipAllowed(ip.c_str(), Config.kissNet.allowed.c_str())) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "KISS WiFi", "Refused %s", ip.c_str());
            incoming.stop();
            return;
        }
        for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i].socket || !clients[i].socket.connected()) {
                clients[i].socket = incoming;
                clients[i].socket.setNoDelay(true);
                clients[i].splitter.clear();
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "KISS WiFi", "Client %s", ip.c_str());
                return;
            }
        }
        incoming.stop();                                    // no free slot
    }

    void readClients(uint8_t& budget) {
        uint8_t buffer[128];
        for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
            Client& c = clients[i];
            if (!c.socket) continue;
            if (!c.socket.connected() && c.socket.available() == 0) { c.socket.stop(); c.splitter.clear(); continue; }
            int guard = 8;                                  // bounded work per pass
            while (c.socket.available() > 0 && guard-- > 0) {
                const int n = c.socket.read(buffer, sizeof(buffer));
                if (n <= 0) break;
                c.splitter.feed(buffer, (size_t)n);
            }
            std::string raw;
            while (budget > 0 && c.splitter.next(raw)) { toRadio(raw, "TCP"); budget--; }
        }
    }

    void readUdp(uint8_t& budget) {
        if (!udpOn) return;
        int size = udp.parsePacket();
        while (size > 0 && budget > 0) {
            const String ip = udp.remoteIP().toString();
            uint8_t buffer[512];
            const int n = udp.read(buffer, sizeof(buffer));
            if (n > 0 && KissNetUtil::ipAllowed(ip.c_str(), Config.kissNet.allowed.c_str())) {
                udpPeer     = udp.remoteIP();
                udpPeerPort = udp.remotePort();
                KissNetUtil::FrameSplitter splitter;        // each datagram carries whole frames
                splitter.feed(buffer, (size_t)n);
                std::string raw;
                while (budget > 0 && splitter.next(raw)) { toRadio(raw, "UDP"); budget--; }
            }
            size = (budget > 0) ? udp.parsePacket() : 0;
        }
    }

}

namespace KISS_NET {

    bool active() {
        return running;
    }

    void setup() {
        running = false;
        if (!Config.kissNet.enabled || !modeOk()) return;
        if (Config.fwUpdate.ssid.isEmpty()) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "KISS WiFi", "No WiFi network set (Administration > Firmware update)");
            return;
        }
        WiFi.setHostname(KISS_HOST);    // antes de crear la interfaz STA (Arduino-ESP32 3.x); si no, el router ve "espressif" / "esp32s3-xxxx"
        WiFi.mode(WIFI_STA);
        WiFi.setHostname(KISS_HOST);    // por si la interfaz ya existia (cambio de modo)
        WiFi.setAutoReconnect(true);
        WiFi.begin(Config.fwUpdate.ssid.c_str(), Config.fwUpdate.password.c_str());
        running     = true;
        linkUp      = false;
        lastRetryMs = millis();
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "KISS WiFi", "Joining %s", Config.fwUpdate.ssid.c_str());
    }

    void poll() {
        if (!running) return;
        if (WiFi.status() != WL_CONNECTED) {
            if (linkUp) {                                    // lost: close everything, sockets are rebuilt on the next connection
                linkUp = false;
                stopSockets();
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "KISS WiFi", "WiFi lost");
            }
            if (millis() - lastRetryMs >= RETRY_MS) {
                lastRetryMs = millis();
                WiFi.begin(Config.fwUpdate.ssid.c_str(), Config.fwUpdate.password.c_str());
            }
            return;
        }
        if (!linkUp) {
            linkUp = true;
            startSockets();
        }
        if (server == nullptr) return;
        acceptClients();
        uint8_t budget = MAX_FRAMES_PER_POLL;
        readClients(budget);
        readUdp(budget);
    }

    void stop() {
        if (!running) return;
        stopSockets();
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        running = false;
        linkUp  = false;
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "KISS WiFi", "Stopped");
    }

    uint8_t state() {
        if (!running) return 0;
        return linkUp ? 2 : 1;
    }

    String localIp() {
        return linkUp ? WiFi.localIP().toString() : String("");
    }

    uint8_t clientCount() {
        uint8_t n = 0;
        for (uint8_t i = 0; i < MAX_CLIENTS; i++) if (clients[i].socket && clients[i].socket.connected()) n++;
        return n;
    }

    void onRx(const String& tnc2Frame) {
        if (!running || !linkUp || tnc2Frame.isEmpty()) return;
        sendFrame(KISS_Utils::encodeKISS(tnc2Frame));
    }

    void onTx(const String& tnc2Frame) {
        onRx(tnc2Frame);
    }

}

#else   // classic ESP32, T-Deck or KISS_NET_DISABLE: empty functions

namespace KISS_NET {
    void setup() {}
    void poll() {}
    void onRx(const String&) {}
    void onTx(const String&) {}
    bool active() { return false; }
    void stop() {}
    uint8_t state() { return 0; }
    String localIp() { return String(""); }
    uint8_t clientCount() { return 0; }
}

#endif
