/* Copyright (C) 2026 Manuel - EA7JRS
 * KISS TNC over WiFi (5.3.2): TCP server (port 8001 by default, like Dire Wolf) and optional UDP, so that APRSDroid ("TCP KISS TNC"),
 * Xastir, YAAC and similar programs can use the tracker's LoRa radio through the WiFi network it has joined
 * (home network or the phone's hotspot; the same network as the firmware update). APRS LoRa and APRS FSK modes.
 * Part of AIS/LoRa APRS Tracker - Manuel Canca EA7JRS (based on LoRa APRS Tracker by Ricardo Guzman CA2RXU, GPL-3.0)
 */
#ifndef KISS_NET_H_
#define KISS_NET_H_

#include <Arduino.h>
#include "sdkconfig.h"

// ESP32-S3 / ESP32-C3 only: the classic ESP32 boards have little flash left, and the T-Deck builds use an old Arduino core.
#if (defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)) && !defined(FW_UPDATE_DISABLE) && !defined(KISS_NET_DISABLE)
    #define KISS_NET_SUPPORTED 1
#else
    #define KISS_NET_SUPPORTED 0
#endif

namespace KISS_NET {

    void setup();                                   // after the WiFi controller has been switched off at start-up; joins the WiFi if enabled
    void poll();                                    // every pass of the main loop: reconnects, accepts clients, forwards their frames to the radio
    void onRx(const String& tnc2Frame);             // a frame heard on the air (TNC2 text, without the LoRa header) -> every client
    void onTx(const String& tnc2Frame);             // a frame the tracker has just transmitted (beacons) -> every client, like BLE does
    bool active();                                  // running (enabled, supported board, APRS LoRa / FSK mode)
    void refresh();                                 // KISS o CAT se han activado / desactivado: arranca, reajusta o apaga el enlace WiFi segun lo que quede activo
    void stop();                                    // leaves the WiFi network and closes every socket (live switch from the SYSTEM page)
    uint8_t state();                                // 0 = off, 1 = joining the WiFi, 2 = connected (sockets open)
    String  localIp();                              // address of the tracker in that network ("" if not connected)
    uint8_t clientCount();                          // TCP clients connected now

}

#endif
