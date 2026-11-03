/* Copyright (C) 2026 Manuel - EA7JRS
 * Firmware information and updates (5.3.1):
 *   - identity of the board and the firmware (/api/info, boot banner line, serial command) for the USB web installer,
 *   - update from a .bin file uploaded to the WebUI (browser on a PC, tablet or phone),
 *   - on-line check / install against the release site (the board joins the user's WiFi), also automatic at boot if enabled.
 * Only the boards whose partition table has a second application slot can update themselves; the others
 * report it and are updated with the USB web installer.
 * Part of AIS/LoRa APRS Tracker - Manuel Canca EA7JRS (based on LoRa APRS Tracker by Ricardo Guzman CA2RXU, GPL-3.0)
 */
#ifndef FW_UPDATE_H_
#define FW_UPDATE_H_

#include <Arduino.h>
#include "sdkconfig.h"

// The code is built for ESP32-S3 / ESP32-C3 (Arduino-ESP32 3.x). The classic ESP32 boards have no room for a second copy of the
// firmware, and the T-Deck builds use an old Arduino core: for them everything is replaced by empty functions.
#if (defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)) && !defined(FW_UPDATE_DISABLE)
    #define FW_UPDATE_SUPPORTED 1
#else
    #define FW_UPDATE_SUPPORTED 0
#endif

#ifndef APRS_BOARD_ID
    #define APRS_BOARD_ID "unknown"
#endif

class AsyncWebServer;

namespace FW_Update {

    String boardId();                       // PlatformIO environment the firmware was built for, e.g. "ttgo_t_beam_s3_SUPREME_v3"
    bool   slotAvailable();                 // true if there is a second application slot to write the new firmware into
    String infoJson();                      // {"name","version","board","chip","flashSize","ota",...}
    void   printBanner();                   // "APRS-LoRa-Plus: board=... version=..." on the serial port (read by the installer page)
    void   serialPoll();                    // answers the line "APRS-INFO?" with the banner; call it from loop()
    void   registerRoutes(AsyncWebServer& server);   // /api/info, /api/update (upload), /api/update/check|install|status
    void   autoUpdateAtBoot();              // if enabled in the configuration: join the WiFi, look for a newer version, install it

}

#endif
