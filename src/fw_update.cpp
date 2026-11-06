/* Copyright (C) 2026 Manuel - EA7JRS
 * Firmware information and updates. See fw_update.h
 * Part of AIS/LoRa APRS Tracker - Manuel Canca EA7JRS (based on LoRa APRS Tracker by Ricardo Guzman CA2RXU, GPL-3.0)
 */
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include <time.h>
#include <logger.h>
#include "fw_update.h"
#include "cat_net.h"
#include "configuration.h"
#include "display.h"

#if FW_UPDATE_SUPPORTED
    #include <esp_ota_ops.h>
    #include <esp_partition.h>
    #include <esp_http_client.h>
    #include <esp_crt_bundle.h>
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

#ifndef FW_UPDATE_BASE_URL
    #define FW_UPDATE_BASE_URL "https://ea7jrs.github.io/APRS-LoRa-Plus/"
#endif

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

    // ---- version helpers ---------------------------------------------------------------------------------------------

    // "5.3.1" / "5.3.1-beta" -> 5003001 ; 0 if it cannot be read
    static long versionNumberOf(const String& v) {
        int part[3] = { 0, 0, 0 }, n = 0;
        long cur = -1;
        for (size_t i = 0; i <= v.length() && n < 3; i++) {
            char c = i < v.length() ? v[i] : '\0';
            if (c >= '0' && c <= '9') { cur = (cur < 0 ? 0 : cur) * 10 + (c - '0'); }
            else {
                if (cur >= 0) { part[n++] = (int)cur; cur = -1; }
                if (c != '.') break;
            }
        }
        if (n == 0) return 0;
        return (long)part[0] * 1000000L + (long)part[1] * 1000L + part[2];
    }

    // ---- on-line state shared by the WebUI and the worker task -------------------------------------------------------

    enum : uint8_t { ST_IDLE = 0, ST_WORKING, ST_UPTODATE, ST_AVAILABLE, ST_DONE, ST_ERROR };

    static SemaphoreHandle_t   stMutex = nullptr;
    static volatile uint8_t    stState = ST_IDLE;
    static volatile uint8_t    stProgress = 0;
    static char                stMessage[96]  = "";
    static char                stLatest[24]   = "";
    static char                stUrl[200]     = "";
    static char                stSha[72]      = "";
    static size_t              stSize         = 0;
    static volatile bool       stBusy         = false;

    static void lockState()   { if (stMutex) xSemaphoreTake(stMutex, portMAX_DELAY); }
    static void unlockState() { if (stMutex) xSemaphoreGive(stMutex); }

    static void setStatus(uint8_t state, const char* msg, int progress = -1) {
        lockState();
        stState = state;
        snprintf(stMessage, sizeof(stMessage), "%s", msg);
        if (progress >= 0) stProgress = (uint8_t)progress;
        unlockState();
    }

    // ---- WiFi client + time ------------------------------------------------------------------------------------------

    static wifi_mode_t wifiModeBefore = WIFI_MODE_NULL;

    static bool connectSta(uint32_t timeoutMs) {
        if (Config.fwUpdate.ssid.isEmpty()) return false;
        if (WiFi.status() == WL_CONNECTED) return true;
        wifiModeBefore = WiFi.getMode();
        WiFi.mode((wifiModeBefore == WIFI_MODE_AP || wifiModeBefore == WIFI_MODE_APSTA) ? WIFI_MODE_APSTA : WIFI_MODE_STA);
        WiFi.begin(Config.fwUpdate.ssid.c_str(), Config.fwUpdate.password.c_str());
        const uint32_t t0 = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeoutMs) delay(100);
        if (WiFi.status() != WL_CONNECTED) return false;
        // the certificate check may need the date: ask an NTP server (a few seconds at most)
        if (time(nullptr) < 1700000000) {
            configTime(0, 0, "pool.ntp.org", "time.google.com");
            const uint32_t t1 = millis();
            while (time(nullptr) < 1700000000 && (millis() - t1) < 6000) delay(100);
        }
        return true;
    }

    static void leaveSta() {
        WiFi.disconnect(false);
        if (wifiModeBefore == WIFI_MODE_AP || wifiModeBefore == WIFI_MODE_APSTA) WiFi.mode(WIFI_MODE_AP);   // back to the configuration access point
        else                                                                       WiFi.mode(WIFI_MODE_NULL);
    }

    // ---- HTTPS ------------------------------------------------------------------------------------------------------

    static esp_http_client_handle_t httpOpen(const String& url, int64_t* contentLength) {
        esp_http_client_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.url               = url.c_str();
        cfg.timeout_ms        = 12000;
        cfg.buffer_size       = 2048;
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
        esp_http_client_handle_t c = esp_http_client_init(&cfg);
        if (!c) return nullptr;
        if (esp_http_client_open(c, 0) != ESP_OK) { esp_http_client_cleanup(c); return nullptr; }
        int64_t len = esp_http_client_fetch_headers(c);
        if (esp_http_client_get_status_code(c) != 200) { esp_http_client_close(c); esp_http_client_cleanup(c); return nullptr; }
        if (contentLength) *contentLength = len;
        return c;
    }

    static bool httpGetString(const String& url, String& out, size_t maxLen) {
        int64_t len = 0;
        esp_http_client_handle_t c = httpOpen(url, &len);
        if (!c) return false;
        char buf[512];
        bool ok = true;
        out = "";
        for (;;) {
            int n = esp_http_client_read(c, buf, sizeof(buf));
            if (n < 0) { ok = false; break; }
            if (n == 0) break;
            if (out.length() + (size_t)n > maxLen) { ok = false; break; }
            out.concat(buf, (unsigned int)n);
        }
        esp_http_client_close(c);
        esp_http_client_cleanup(c);
        return ok && out.length() > 0;
    }

    static String absoluteUrl(const char* relative) {
        String r(relative);
        if (r.startsWith("http://") || r.startsWith("https://")) return r;
        return String(FW_UPDATE_BASE_URL) + r;
    }

    // Looks the newest version of this board up on the release site. Fills the stXxx fields. Returns true if the check itself worked
    static bool checkLatest() {
        String body;
        if (!httpGetString(String(FW_UPDATE_BASE_URL) + "ota/" + boardId() + ".json", body, 4096)) {
            setStatus(ST_ERROR, "Could not read the release site");
            return false;
        }
        JsonDocument doc;
        if (deserializeJson(doc, body) != DeserializationError::Ok || !doc["version"].is<const char*>()) {
            setStatus(ST_ERROR, "Unexpected answer from the release site");
            return false;
        }
        lockState();
        snprintf(stLatest, sizeof(stLatest), "%s", doc["version"].as<const char*>());
        snprintf(stUrl,    sizeof(stUrl),    "%s", absoluteUrl(doc["url"] | "").c_str());
        snprintf(stSha,    sizeof(stSha),    "%s", doc["sha256"] | "");
        stSize = doc["size"] | 0;
        unlockState();
        if (versionNumberOf(String(stLatest)) > versionNumberOf(versionNumber)) {
            setStatus(ST_AVAILABLE, "New version available");
        } else {
            setStatus(ST_UPTODATE, "Firmware is up to date");
        }
        return true;
    }

    typedef void (*ProgressFn)(int percent);

    // Downloads the image announced by checkLatest() into the free slot and activates it. Returns true if the new firmware is ready
    static bool installLatest(ProgressFn progress) {
        const esp_partition_t* part = targetPartition();
        if (!part)              { setStatus(ST_ERROR, "This board has no second firmware slot"); return false; }
        if (stUrl[0] == 0 || stSize < 100000) { setStatus(ST_ERROR, "Check for updates first"); return false; }
        setStatus(ST_WORKING, "Downloading", 0);

        int64_t len = 0;
        esp_http_client_handle_t c = httpOpen(String(stUrl), &len);
        if (!c) { setStatus(ST_ERROR, "Could not download the firmware"); return false; }
        if (len > 0 && (size_t)len != stSize) {
            esp_http_client_close(c); esp_http_client_cleanup(c);
            setStatus(ST_ERROR, "Downloaded size does not match the announced one");
            return false;
        }
        esp_ota_handle_t handle = 0;
        if (esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &handle) != ESP_OK) {
            esp_http_client_close(c); esp_http_client_cleanup(c);
            setStatus(ST_ERROR, "Could not prepare the flash");
            return false;
        }
        mbedtls_sha256_context sha;
        mbedtls_sha256_init(&sha);
        mbedtls_sha256_starts(&sha, 0);
        static uint8_t buf[4096];
        size_t total = 0;
        bool ok = true;
        int lastPct = -1;
        while (total < stSize) {
            int n = esp_http_client_read(c, (char*)buf, sizeof(buf));
            if (n <= 0) { ok = false; break; }
            if (total + (size_t)n > stSize) { ok = false; break; }
            mbedtls_sha256_update(&sha, buf, (size_t)n);
            if (esp_ota_write(handle, buf, (size_t)n) != ESP_OK) { ok = false; break; }
            total += (size_t)n;
            int pct = (int)((total * 100ULL) / stSize);
            if (pct != lastPct) { lastPct = pct; stProgress = (uint8_t)pct; if (progress) progress(pct); }
        }
        esp_http_client_close(c);
        esp_http_client_cleanup(c);

        uint8_t digest[32];
        mbedtls_sha256_finish(&sha, digest);
        mbedtls_sha256_free(&sha);
        char hex[65];
        for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", digest[i]);

        if (!ok || total != stSize) {
            esp_ota_abort(handle);
            setStatus(ST_ERROR, "The download was interrupted");
            return false;
        }
        if (stSha[0] != 0 && strcasecmp(hex, stSha) != 0) {
            esp_ota_abort(handle);
            setStatus(ST_ERROR, "Checksum does not match: the firmware was not installed");
            return false;
        }
        if (esp_ota_end(handle) != ESP_OK) {
            setStatus(ST_ERROR, "The downloaded image is not valid");
            return false;
        }
        if (esp_ota_set_boot_partition(part) != ESP_OK) {
            setStatus(ST_ERROR, "Could not activate the new firmware");
            return false;
        }
        setStatus(ST_DONE, "Installed. Restarting", 100);
        return true;
    }

    // ---- worker task for the WebUI (the web server must not block) ---------------------------------------------------

    enum : uint8_t { JOB_CHECK = 1, JOB_INSTALL = 2 };
    static volatile uint8_t jobWanted = 0;

    static void workerTask(void*) {
        const uint8_t job = jobWanted;
        setStatus(ST_WORKING, "Joining the WiFi network", 0);
        if (!connectSta(15000)) {
            setStatus(ST_ERROR, Config.fwUpdate.ssid.isEmpty() ? "Enter the WiFi network below and save the configuration first" : "Could not join the WiFi network");
        } else {
            setStatus(ST_WORKING, "Looking for a new version");
            const bool checked = checkLatest();
            if (checked && job == JOB_INSTALL) {
                if (stState == ST_AVAILABLE) {
                    if (installLatest(nullptr)) { leaveSta(); stBusy = false; reboot(2500); vTaskDelete(nullptr); return; }
                } else if (stState == ST_UPTODATE) {
                    setStatus(ST_UPTODATE, "Firmware is already up to date");
                }
            }
        }
        leaveSta();
        stBusy = false;
        vTaskDelete(nullptr);
    }

    static bool startJob(uint8_t job) {
        if (stBusy) return false;
        stBusy = true;
        jobWanted = job;
        setStatus(ST_WORKING, "Starting", 0);
        if (xTaskCreate(workerTask, "fwupd", 16384, nullptr, 1, nullptr) != pdPASS) {
            stBusy = false;
            setStatus(ST_ERROR, "Not enough memory");
            return false;
        }
        return true;
    }

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
            if (stBusy)              { uploadFail("An on-line update is in progress"); }
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
        if (stBusy)                          { error = "An on-line update is in progress"; return false; }
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
        stBusy     = true;                   // no WebUI upload or on-line job meanwhile
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

    static String statusJson() {
        JsonDocument doc;
        lockState();
        doc["state"]    = stState == ST_IDLE ? "idle" : stState == ST_WORKING ? "working" : stState == ST_UPTODATE ? "uptodate" :
                          stState == ST_AVAILABLE ? "available" : stState == ST_DONE ? "done" : "error";
        doc["progress"] = (int)stProgress;
        doc["message"]  = stMessage;
        doc["latest"]   = stLatest;
        doc["current"]  = versionNumber;
        doc["size"]     = (uint32_t)stSize;
        unlockState();
        String out;
        serializeJson(doc, out);
        return out;
    }

    void registerRoutes(AsyncWebServer& server) {
        if (!stMutex) stMutex = xSemaphoreCreateMutex();
        server.on("/api/info", HTTP_GET, [](AsyncWebServerRequest* request) { sendJson(request, infoJson()); });
        server.on("/api/update", HTTP_POST, onUploadDone, onUploadChunk);
        server.on("/api/update/status", HTTP_GET, [](AsyncWebServerRequest* request) { sendJson(request, statusJson()); });
        server.on("/api/update/check", HTTP_POST, [](AsyncWebServerRequest* request) {
            sendJson(request, startJob(JOB_CHECK) ? "{\"started\":true}" : "{\"started\":false}");
        });
        server.on("/api/update/install", HTTP_POST, [](AsyncWebServerRequest* request) {
            sendJson(request, startJob(JOB_INSTALL) ? "{\"started\":true}" : "{\"started\":false}");
        });
    }

    bool onlineStart(bool install) {
        if (!stMutex) stMutex = xSemaphoreCreateMutex();
        return startJob(install ? JOB_INSTALL : JOB_CHECK);
    }

    String onlineStatus() {
        if (!stMutex) stMutex = xSemaphoreCreateMutex();
        return statusJson();
    }

    // ---- automatic update at boot --------------------------------------------------------------------------------------

    static void showProgress(int pct) {
        static int last = -10;
        if (pct < last + 10 && pct != 100) return;
        last = pct;
        displayPopup("Firmware update", "Downloading " + String(pct) + "%", "Do not power off", pct);
    }

    void autoUpdateAtBoot() {
        if (!Config.fwUpdate.autoCheck || Config.fwUpdate.ssid.isEmpty()) return;
        if (Config.wifiAP.active) return;                           // the user is about to configure the tracker: do not delay that
        if (!stMutex) stMutex = xSemaphoreCreateMutex();
        displayPopup("Firmware update", "Checking WiFi and", "new version...", -1, 600);
        if (!connectSta(10000)) {
            leaveSta();
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Update", "WiFi not reachable, no update check");
            return;
        }
        bool installed = false;
        if (checkLatest() && stState == ST_AVAILABLE) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Update", "New version %s available (installed %s)", stLatest, versionNumber.c_str());
            if (Config.fwUpdate.autoInstall && slotAvailable()) {
                displayPopup("Firmware update", String("Version ") + stLatest, "found", 0, 800);
                installed = installLatest(showProgress);
            } else {
                displayPopup("Firmware update", String("Version ") + stLatest, "ready: see WebUI", -1, 2500);
            }
        }
        leaveSta();
        if (installed) {
            displayPopup("Firmware update", "Installed", "Restarting...", 100, 1500);
            ESP.restart();
        }
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
    void autoUpdateAtBoot() {}
    bool   onlineStart(bool) { return false; }
    String onlineStatus() { return "{\"state\":\"unsupported\",\"message\":\"This board updates only with the USB installer\"}"; }
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
        doc["onlineUpdate"]    = FW_UPDATE_SUPPORTED ? true : false;
        #ifdef HAS_SAT
            doc["sat"]         = true;
        #else
            doc["sat"]         = false;
        #endif
        doc["wifiConfigured"]  = !Config.fwUpdate.ssid.isEmpty();
        doc["autoCheck"]       = Config.fwUpdate.autoCheck;
        doc["autoInstall"]     = Config.fwUpdate.autoInstall;
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
