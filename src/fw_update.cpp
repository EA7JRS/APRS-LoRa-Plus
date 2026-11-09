/* Copyright (C) 2026 Manuel - EA7JRS
 * Firmware information and updates. See fw_update.h
 * Part of AIS/LoRa APRS Tracker - Manuel Canca EA7JRS (based on LoRa APRS Tracker by Ricardo Guzman CA2RXU, GPL-3.0)
 */
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include <time.h>
#include <logger.h>
#ifdef HAS_SA868
    #include "sa868_radio.h"
#endif
#include "fw_update.h"
#include "cat_net.h"
#include "configuration.h"
#include "display.h"

#if FW_UPDATE_SUPPORTED
    #include <esp_ota_ops.h>
    #include <esp_partition.h>
    #include <mbedtls/sha256.h>
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    #include "freertos/semphr.h"
#endif

extern Configuration    Config;
extern String           versionNumber;
extern logging::Logger  logger;

// Identity of the firmware inside the binary: the update code reads it from an uploaded image to refuse firmware built for another board.
// The scanner ignores a prefix that is not followed by a name, so other copies of the prefix inside the code do no harm.
#define BOARD_MARKER_PREFIX "APRS-LoRa-Plus-board="
extern const char kBoardMarker[];
const char kBoardMarker[] = BOARD_MARKER_PREFIX APRS_BOARD_ID;

namespace FW_Update {

    String boardId() { return String(kBoardMarker + (sizeof(BOARD_MARKER_PREFIX) - 1)); }

#if FW_UPDATE_SUPPORTED

    // ---- slot / information ------------------------------------------------------------------------------------------

    static const esp_partition_t* targetPartition() {
        const esp_partition_t* run  = esp_ota_get_running_partition();
        const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
        if (!run || !next || next == run || next->address == run->address) return nullptr;     // a single slot: nothing to write into
        if (next->size < 0x100000) return nullptr;
        return next;
    }

    bool slotAvailable() { return targetPartition() != nullptr; }

    static void reboot(uint32_t afterMs) {
        struct Arg { uint32_t ms; };
        static Arg arg;
        arg.ms = afterMs;
        xTaskCreate([](void* p) {
            vTaskDelay(pdMS_TO_TICKS(((Arg*)p)->ms));
            ESP.restart();
        }, "fwreboot", 3072, &arg, 1, nullptr);
    }

    // A WebUI upload and an image from the app never run at the same time. The update from the internet is the app's job
    // (it downloads and checks the image with the phone's connection and sends it over BLE): the tracker has no HTTPS client for it.
    static volatile bool stBusy = false;

    // ---- upload from the browser -------------------------------------------------------------------------------------

    struct UploadState {
        bool                    started;
        bool                    failed;
        bool                    done;
        esp_ota_handle_t        handle;
        const esp_partition_t*  part;
        size_t                  written;
        char                    error[96];
        // search for the board identity inside the image
        char                    prefix[32];
        size_t                  prefixLen, matchPos;
        bool                    collecting, idDone;
        char                    id[56];
        size_t                  idLen;
    };
    static UploadState up;

    static void uploadFail(const char* msg) {
        if (up.failed) return;
        up.failed = true;
        snprintf(up.error, sizeof(up.error), "%s", msg);
        if (up.started && !up.done) { esp_ota_abort(up.handle); }
    }

    static void scanForBoardId(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len && !up.idDone; i++) {
            const uint8_t b = data[i];
            if (up.collecting) {
                if (b == 0) {
                    if (up.idLen == 0) { up.collecting = false; up.matchPos = 0; }       // prefix without a name: keep looking
                    else up.idDone = true;
                }
                else if (up.idLen < sizeof(up.id) - 1) up.id[up.idLen++] = (char)b;
                else { up.idDone = true; }
            } else if (b == (uint8_t)up.prefix[up.matchPos]) {
                if (++up.matchPos == up.prefixLen) { up.collecting = true; up.idLen = 0; }
            } else {
                up.matchPos = (b == (uint8_t)up.prefix[0]) ? 1 : 0;
            }
        }
    }

    static void onUploadChunk(AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
        (void)request; (void)filename;
        if (index == 0) {
            memset(&up, 0, sizeof(up));
            snprintf(up.prefix, sizeof(up.prefix), "%s", BOARD_MARKER_PREFIX);
            up.prefixLen = strlen(up.prefix);
            if (stBusy)              { uploadFail("Another update is in progress"); }
            else if (!(up.part = targetPartition())) { uploadFail("This board has no second firmware slot: use the USB installer"); }
            else if (esp_ota_begin(up.part, OTA_WITH_SEQUENTIAL_WRITES, &up.handle) != ESP_OK) { uploadFail("Could not prepare the flash"); }
            else up.started = true;
        }
        if (!up.failed && len > 0) {
            scanForBoardId(data, len);
            if (esp_ota_write(up.handle, data, len) != ESP_OK) uploadFail("The file is not a valid firmware for this chip");
            else up.written += len;
        }
        if (final && !up.failed) {
            if (up.written < 100000)            uploadFail("The file is too small to be a firmware");
            else if (esp_ota_end(up.handle) != ESP_OK) { up.done = true; uploadFail("The firmware image is not valid"); }
            else {
                up.done = true;
                if (!up.idDone)                                       uploadFail("This file does not say which board it is for (build it from this project)");
                else if (strcmp(up.id, APRS_BOARD_ID) != 0) {
                    char m[96];
                    snprintf(m, sizeof(m), "This firmware is for another board: %s", up.id);
                    uploadFail(m);
                }
                else if (esp_ota_set_boot_partition(up.part) != ESP_OK) uploadFail("Could not activate the new firmware");
            }
        }
    }

    static void onUploadDone(AsyncWebServerRequest* request) {
        JsonDocument doc;
        const bool ok = up.started && !up.failed && up.done;
        doc["ok"] = ok;
        if (!ok) doc["error"] = up.failed ? up.error : "No file received";
        String out;
        serializeJson(doc, out);
        AsyncWebServerResponse* r = request->beginResponse(ok ? 200 : 400, "application/json", out);
        r->addHeader("Cache-Control", "no-store");
        request->send(r);
        if (ok) reboot(2500);
    }

    // ---- image sent by the APRS LoRa+ app over BLE (EA7JRS) -----------------------------------------------------------

    static mbedtls_sha256_context bleSha;
    static bool                   bleActive   = false;
    static size_t                 bleSize     = 0;
    static char                   bleShaHex[65] = "";

    bool bleBegin(size_t size, const char* sha256Hex, String& error) {
        if (bleActive) bleAbort();
        if (stBusy)                          { error = "Another update is in progress"; return false; }
        if (size < 100000)                   { error = "The image is too small to be a firmware"; return false; }
        if (sha256Hex == nullptr || strlen(sha256Hex) != 64) { error = "Missing SHA-256"; return false; }
        memset(&up, 0, sizeof(up));
        snprintf(up.prefix, sizeof(up.prefix), "%s", BOARD_MARKER_PREFIX);
        up.prefixLen = strlen(up.prefix);
        up.part = targetPartition();
        if (!up.part)                        { error = "This board has no second firmware slot: use the USB installer"; return false; }
        if (size > up.part->size)            { error = "The image does not fit in the firmware slot"; return false; }
        if (esp_ota_begin(up.part, OTA_WITH_SEQUENTIAL_WRITES, &up.handle) != ESP_OK) { error = "Could not prepare the flash"; return false; }
        up.started = true;
        stBusy     = true;                   // no WebUI upload meanwhile
        mbedtls_sha256_init(&bleSha);
        mbedtls_sha256_starts(&bleSha, 0);
        snprintf(bleShaHex, sizeof(bleShaHex), "%s", sha256Hex);
        bleSize   = size;
        bleActive = true;
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Update", "Image from the app: %u bytes", (unsigned)size);
        return true;
    }

    bool bleWrite(const uint8_t* data, size_t length, String& error) {
        if (!bleActive)                      { error = "No update in progress"; return false; }
        if (up.written + length > bleSize)   { bleAbort(); error = "More data than announced"; return false; }
        scanForBoardId(data, length);
        mbedtls_sha256_update(&bleSha, data, length);
        if (esp_ota_write(up.handle, data, length) != ESP_OK) { bleAbort(); error = "The data is not a valid firmware for this chip"; return false; }
        up.written += length;
        return true;
    }

    size_t bleWritten() { return bleActive ? up.written : 0; }

    bool bleFinish(String& error) {
        if (!bleActive)                      { error = "No update in progress"; return false; }
        if (up.written != bleSize)           { bleAbort(); error = "Incomplete image"; return false; }
        uint8_t digest[32];
        mbedtls_sha256_finish(&bleSha, digest);
        mbedtls_sha256_free(&bleSha);
        bleActive = false;
        stBusy    = false;
        char hex[65];
        for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", digest[i]);
        if (strcasecmp(hex, bleShaHex) != 0) { esp_ota_abort(up.handle); error = "Checksum does not match: the firmware was not installed"; return false; }
        if (esp_ota_end(up.handle) != ESP_OK) { error = "The firmware image is not valid"; return false; }
        if (!up.idDone)                      { error = "The image does not say which board it is for"; return false; }
        if (strcmp(up.id, APRS_BOARD_ID) != 0) { error = String("This firmware is for another board: ") + up.id; return false; }
        if (esp_ota_set_boot_partition(up.part) != ESP_OK) { error = "Could not activate the new firmware"; return false; }
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Update", "%s", "Image from the app installed");
        return true;
    }

    void bleAbort() {
        if (!bleActive) return;
        esp_ota_abort(up.handle);
        mbedtls_sha256_free(&bleSha);
        bleActive = false;
        stBusy    = false;
    }

    // ---- routes ------------------------------------------------------------------------------------------------------

    static void sendJson(AsyncWebServerRequest* request, const String& body, int code = 200) {
        AsyncWebServerResponse* r = request->beginResponse(code, "application/json", body);
        r->addHeader("Cache-Control", "no-store");
        request->send(r);
    }

    void registerRoutes(AsyncWebServer& server) {
        server.on("/api/info", HTTP_GET, [](AsyncWebServerRequest* request) { sendJson(request, infoJson()); });
        server.on("/api/update", HTTP_POST, onUploadDone, onUploadChunk);
    }

#else   // ---- boards without a second slot or without the needed core: information only ------------------------------

    bool slotAvailable() { return false; }
    void registerRoutes(AsyncWebServer& server) {
        server.on("/api/info", HTTP_GET, [](AsyncWebServerRequest* request) {
            AsyncWebServerResponse* r = request->beginResponse(200, "application/json", infoJson());
            r->addHeader("Cache-Control", "no-store");
            request->send(r);
        });
    }
    bool   bleBegin(size_t, const char*, String& error) { error = "This board updates only with the USB installer"; return false; }
    bool   bleWrite(const uint8_t*, size_t, String& error) { error = "No update in progress"; return false; }
    size_t bleWritten() { return 0; }
    bool   bleFinish(String& error) { error = "No update in progress"; return false; }
    void   bleAbort() {}

#endif

    // ---- common ------------------------------------------------------------------------------------------------------

    String infoJson() {
        JsonDocument doc;
        doc["name"]            = "APRS LoRa+";
        doc["version"]         = versionNumber;
        doc["board"]           = boardId();
        doc["chip"]            = ESP.getChipModel();
        doc["chipRev"]         = (int)ESP.getChipRevision();
        doc["flashSize"]       = (uint32_t)ESP.getFlashChipSize();
        doc["sketchSize"]      = (uint32_t)ESP.getSketchSize();
        doc["ota"]             = slotAvailable();
        #ifdef HAS_SA868                                    // LilyGO T-TWR: radio FM SA868 (la WebUI muestra su seccion y oculta la de los modos del SX126x)
            doc["radio"]       = "sa868";
            JsonObject sa      = doc["sa868"].to<JsonObject>();
            sa["ok"]           = SA868::ready();
            sa["rev"]          = SA868::revision();
            sa["band"]         = SA868::uhf() ? "UHF" : "VHF";
            sa["fw"]           = SA868::firmwareName();
            sa["modem"]        = SA868::modemName();
            sa["rx"]           = SA868::rxFreqHz();
        #else
            doc["radio"]       = "lora";
        #endif
        #ifdef HAS_SAT
            doc["sat"]         = true;
        #else
            doc["sat"]         = false;
        #endif
        doc["wifiConfigured"]  = !Config.fwUpdate.ssid.isEmpty();
        String out;
        serializeJson(doc, out);
        return out;
    }

    void printBanner() {
        Serial.printf("APRS-LoRa-Plus: board=%s version=%s chip=%s ota=%d\n", APRS_BOARD_ID, versionNumber.c_str(), ESP.getChipModel(), slotAvailable() ? 1 : 0);
    }

    void serialPoll() {
        if (CAT_NET::usbActive()) { CAT_NET::usbPoll(); return; }       // CAT por USB: atiende tambien "APRS-INFO?"
        static char line[24];
        static uint8_t n = 0;
        while (Serial.available() > 0) {
            const int c = Serial.read();
            if (c == '\n' || c == '\r') {
                line[n] = 0;
                if (n > 0 && strcmp(line, "APRS-INFO?") == 0) printBanner();
                n = 0;
            } else if (n < sizeof(line) - 1) {
                line[n++] = (char)c;
            } else {
                n = 0;                                              // too long: not ours
            }
        }
    }

}
