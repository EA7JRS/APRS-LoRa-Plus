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

#include "product.h"
#include <logger.h>
#include <WiFi.h>
#include "configuration.h"
#include "board_pinout.h"
#include "web_utils.h"
#include "display.h"
#include "pages_utils.h"

extern      Configuration       Config;
extern      logging::Logger     logger;

uint32_t    noClientsTime        = 0;
static const uint32_t WEBCONF_SKIP_MAGIC = 0x57434F46;                 // "WCOF"
RTC_NOINIT_ATTR static uint32_t skipWebConfOnce;                        // sobrevive a ESP.restart()


namespace WIFI_Utils {

    void startAutoAP() {
        WiFi.mode(WIFI_MODE_NULL);
        WiFi.mode(WIFI_AP);
        WiFi.softAP(PRODUCT_AP_SSID, Config.wifiAP.password);
        #ifdef RPC_MicroTracker
            digitalWrite(15, LOW);    // Led WiFI
        #endif
    }

    void checkIfWiFiAP() {
        const bool skip = skipWebConfOnce == WEBCONF_SKIP_MAGIC;     // se salio con el boton: este arranque, normal
        skipWebConfOnce = 0;
        if (!skip && (Config.wifiAP.active || Config.callsignMissing())){
            #ifdef HAS_APRS_PAGES
                PAGES_Utils::showWifiAp(PRODUCT_AP_SSID, Config.wifiAP.password.c_str(), "192.168.4.1", 0, 120);
            #else
                displayShow(" LoRa APRS", "    ** WEB-CONF **","", "WiFiAP:" PRODUCT_AP_SSID, "IP    :   192.168.4.1","");
            #endif
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "Main", "WebConfiguration Started! (%s)",
                       Config.callsignMissing() ? "callsign still NOCALL: set it in the WebUI" : "WiFi AP turned on");
            startAutoAP();
            WEB_Utils::setup();
            #ifdef BUTTON_PIN
                pinMode(BUTTON_PIN, INPUT_PULLUP);
                uint32_t pressedAt = 0;
            #endif
            while (true) {
                #ifdef BUTTON_PIN
                    // Mantener el boton 2 s: sale del punto de acceso y arranca normal (antes solo salia al guardar o a los 2 min sin
                    // nadie conectado, y el boton no hacia nada) (EA7JRS)
                    if (digitalRead(BUTTON_PIN) == LOW) {
                        if (pressedAt == 0) pressedAt = millis();
                        else if (millis() - pressedAt >= 2000) {
                            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "Main", "WebConfiguration stopped with the button");
                            #ifdef HAS_APRS_PAGES
                                displayPopup("APRS LoRa+", "Leaving WiFi AP", Config.callsignMissing() ? "Callsign: NOCALL!" : "Restarting...", -1, 1500);
                            #endif
                            Config.wifiAP.active = false;
                            Config.writeFile();
                            skipWebConfOnce = WEBCONF_SKIP_MAGIC;
                            WiFi.softAPdisconnect(true);
                            ESP.restart();
                        }
                    } else {
                        pressedAt = 0;
                    }
                #endif
                #ifdef HAS_APRS_PAGES
                    static uint32_t lastDraw = 0;
                    if (millis() - lastDraw >= 1000) {
                        lastDraw = millis();
                        const int cli = WiFi.softAPgetStationNum();
                        const int left = (noClientsTime == 0) ? 120 : (int)(120 - (millis() - noClientsTime) / 1000);
                        PAGES_Utils::showWifiAp(PRODUCT_AP_SSID, Config.wifiAP.password.c_str(), WiFi.softAPIP().toString().c_str(), cli, left < 0 ? 0 : left);
                    }
                #endif
                if (WiFi.softAPgetStationNum() > 0) {
                    noClientsTime = 0;
                } else {
                    if (noClientsTime == 0) {
                        noClientsTime = millis();
                    } else if ((millis() - noClientsTime) > 2 * 60 * 1000) {
                        logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "Main", "WebConfiguration Stopped!");
                        #ifdef HAS_APRS_PAGES
                            displayPopup("APRS LoRa+", "Stopping WiFi AP", "Restarting...", -1, 2000);
                        #else
                            displayShow("", "", "  STOPPING WiFi AP", 2000);
                        #endif
                        #ifdef RPC_MicroTracker
                            digitalWrite(15, HIGH);    // Led WiFI
                        #endif
                        Config.wifiAP.active = false;
                        Config.writeFile();
                        WiFi.softAPdisconnect(true);
                        ESP.restart();
                    }
                }
            }
        } else {
            WiFi.mode(WIFI_OFF);
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "Main", "WiFi controller stopped");
        }
    }
}