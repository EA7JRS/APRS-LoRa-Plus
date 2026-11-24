/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ tracker.
 * Licensed under the GNU General Public License, version 3 or later.
 *
 * Motor del modo SSDV: TX (JPEG -> paquetes SSDV -> radio) y RX (paquetes -> JPEG), con el SSDVClient de RadioLib 8.0.x.
 * Origen de la imagen: patron de prueba (codificador jo_jpeg), URL por WiFi (HTTP), subida por BLE o camara OV2640.
 */
#include "ssdv_config.h"

#if defined(HAS_SSDV)

#include <RadioLib.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <logger.h>
#include "ssdv_utils.h"
#include "configuration.h"
#include "lora_utils.h"
#include "jo_jpeg.h"
#if defined(HAS_CAMERA)
    #include "esp_camera.h"
#endif

extern logging::Logger  logger;
extern Configuration    Config;
extern Beacon           *currentBeacon;

namespace SSDV_Utils {

    static SSDVClient*  client   = nullptr;
    static uint8_t*     jpegBuf  = nullptr;      // imagen a emitir (TX)
    static size_t       jpegCap  = 0;
    static size_t       jpegLen  = 0;
    static uint8_t*     rxBuf    = nullptr;      // JPEG reconstruido (RX)
    static size_t       rxCap    = 0;
    static uint8_t*     ulBuf    = nullptr;      // subida por BLE
    static size_t       ulCap    = 0;
    static size_t       ulLen    = 0;
    static size_t       ulExpect = 0;
    static bool         ulActive = false;
    static bool         ulReady  = false;        // una imagen subida por BLE, lista para emitir
    static uint8_t*     pkt      = nullptr;       // buffer de un paquete (256 B), en heap para no gastar DRAM estatica
    static char         statusText[48] = "SSDV idle";
    static uint16_t     rxImages  = 0;
    static uint16_t     rxPackets = 0;
    static uint16_t     lastRxId  = 0xFFFF;
    static uint32_t     lastTxMs  = 0;
    static bool         txing     = false;

    enum { ST_IDLE, ST_RX, ST_TX };
    static uint8_t      state = ST_IDLE;

    static void setStatus(const char* s) { strncpy(statusText, s, sizeof(statusText) - 1); statusText[sizeof(statusText) - 1] = '\0'; }

    static uint8_t* bigAlloc(size_t n) {
        #if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM)
            if (ESP.getPsramSize() > 0) { uint8_t* p = (uint8_t*)ps_malloc(n); if (p) return p; }
        #endif
        return (uint8_t*)malloc(n);
    }

    static bool hasPsram() {
        #if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM)
            return ESP.getPsramSize() > 0;
        #else
            return false;
        #endif
    }

    // Indicativo base (sin SSID), hasta 6 caracteres validos para SSDV (A-Z 0-9 '-')
    static void ssdvCallsign(char* out, size_t n) {
        const char* src = currentBeacon ? currentBeacon->callsign.c_str() : "NOCALL";
        size_t j = 0;
        for (size_t i = 0; src[i] && src[i] != '-' && j < n - 1 && j < 6; i++) {
            char c = src[i];
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-') out[j++] = c;
            else if (c >= 'a' && c <= 'z') out[j++] = c - 32;
        }
        if (j == 0) { strncpy(out, "NOCALL", n); return; }
        out[j] = '\0';
    }

    // ---- Origen de la imagen -------------------------------------------------------------------------------------------------

    // Patron de prueba: barras/degradado con el codificador JPEG propio. Gris pequeno sin PSRAM, color mayor con PSRAM.
    static size_t makeTestPattern() {
        const bool color = hasPsram();
        const int w = color ? 320 : 160;
        const int h = color ? 240 : 128;        // multiplos de 16
        const int comp = color ? 3 : 1;
        const size_t fbLen = (size_t)w * h * comp;
        uint8_t* fb = bigAlloc(fbLen);
        if (!fb) { setStatus("SSDV: no RAM (pattern)"); return 0; }
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                uint8_t* p = fb + ((size_t)y * w + x) * comp;
                const bool grid = (x % 32 == 0) || (y % 32 == 0);
                if (comp == 1) {
                    p[0] = grid ? 255 : (uint8_t)((x * 255) / w);
                } else {
                    p[0] = (uint8_t)((x * 255) / w);
                    p[1] = (uint8_t)((y * 255) / h);
                    p[2] = grid ? 255 : (uint8_t)(((x + y) * 255) / (w + h));
                }
            }
        }
        size_t n = jojpeg::encode(jpegBuf, jpegCap, fb, w, h, comp, 60);
        free(fb);
        if (!n) setStatus("SSDV: JPEG too big");
        return n;
    }

    // Descarga de una URL http:// (sin TLS: el tracker no lleva cliente HTTPS; para https la app descarga y sube por BLE)
    static size_t downloadUrl(const String& url) {
        if (WiFi.status() != WL_CONNECTED) { setStatus("SSDV: no WiFi"); return 0; }
        if (!url.startsWith("http://")) { setStatus("SSDV: only http://"); return 0; }
        String rest = url.substring(7);
        int slash = rest.indexOf('/');
        String host = slash < 0 ? rest : rest.substring(0, slash);
        String path = slash < 0 ? "/" : rest.substring(slash);
        int port = 80;
        int colon = host.indexOf(':');
        if (colon >= 0) { port = host.substring(colon + 1).toInt(); host = host.substring(0, colon); }
        WiFiClient cli;
        if (!cli.connect(host.c_str(), port)) { setStatus("SSDV: connect failed"); return 0; }
        cli.printf("GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: APRS-LoRa-Plus\r\nConnection: close\r\n\r\n", path.c_str(), host.c_str());
        uint32_t t0 = millis();
        // saltar las cabeceras hasta la linea en blanco
        bool body = false; String line;
        while (cli.connected() && millis() - t0 < 15000) {
            if (!cli.available()) { delay(5); continue; }
            char c = cli.read();
            line += c;
            if (c == '\n') { if (line == "\r\n" || line == "\n") { body = true; break; } line = ""; }
        }
        if (!body) { cli.stop(); setStatus("SSDV: bad HTTP"); return 0; }
        size_t n = 0;
        while (cli.connected() && millis() - t0 < 20000) {
            if (!cli.available()) { delay(2); continue; }
            int c = cli.read();
            if (c < 0) break;
            if (n < jpegCap) jpegBuf[n++] = (uint8_t)c;
            else { cli.stop(); setStatus("SSDV: image too big"); return 0; }
        }
        cli.stop();
        return n;
    }

    #if defined(HAS_CAMERA)
    static size_t grabCamera() {
        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) { setStatus("SSDV: no camera frame"); return 0; }
        size_t n = 0;
        if (fb->format == PIXFORMAT_JPEG) {
            if (fb->len <= jpegCap) { memcpy(jpegBuf, fb->buf, fb->len); n = fb->len; }
            else setStatus("SSDV: camera JPEG too big");
        } else {
            setStatus("SSDV: camera not JPEG");
        }
        esp_camera_fb_return(fb);
        return n;
    }
    #endif

    static size_t loadImage() {
        switch (Config.ssdv.source) {
            case 1:  return downloadUrl(Config.ssdv.url);
            case 2:
                if (!ulReady) { setStatus("SSDV: no uploaded image"); return 0; }
                if (ulLen > jpegCap) { setStatus("SSDV: image too big"); return 0; }
                memcpy(jpegBuf, ulBuf, ulLen);
                return ulLen;
            case 3:
                #if defined(HAS_CAMERA)
                    return grabCamera();
                #else
                    setStatus("SSDV: no camera on this board");
                    return 0;
                #endif
            default: return makeTestPattern();     // 0 = patron de prueba
        }
    }

    // ---- Ciclo de vida -------------------------------------------------------------------------------------------------------

    void begin() {
        if (!pkt)     { pkt = (uint8_t*)malloc(256); }
        if (!jpegBuf) { jpegCap = hasPsram() ? 120 * 1024 : 40 * 1024; jpegBuf = bigAlloc(jpegCap); }
        if (!rxBuf)   { rxCap   = hasPsram() ? 120 * 1024 : 40 * 1024; rxBuf   = bigAlloc(rxCap); }
        if (!pkt || !jpegBuf || !rxBuf) { setStatus("SSDV: no RAM"); state = ST_IDLE; return; }

        const bool useLora = Config.ssdv.useLora || !LoRa_Utils::fskAvailable();
        const bool fec     = !useLora;             // LoRa lleva su propia FEC; FSK usa Reed-Solomon

        if (client) { client->endDecoder(); delete client; client = nullptr; }
        client = new SSDVClient(LoRa_Utils::ssdvPhy(), fec);
        char call[8]; ssdvCallsign(call, sizeof(call));
        client->begin(call);

        float bwKHz = 125.0f;                      // LoRa: por ahora ancho fijo (se puede refinar con el perfil)
        int st = LoRa_Utils::ssdvRadioBegin(useLora, Config.ssdv.freqHz, Config.ssdv.sf, bwKHz, Config.ssdv.cr,
                                            Config.ssdv.brHz, Config.ssdv.devHz, Config.ssdv.powerDbm);
        if (st != RADIOLIB_ERR_NONE) { setStatus("SSDV: radio error"); state = ST_IDLE; return; }

        client->beginDecoder(rxBuf, rxCap);
        rxImages = rxPackets = 0; lastRxId = 0xFFFF;
        state = ST_RX; txing = false;
        setStatus(useLora ? "SSDV RX (LoRa)" : "SSDV RX (FSK)");
    }

    void end() {
        stopTx();
        if (client) { client->endDecoder(); client->clearImage(); delete client; client = nullptr; }
        LoRa_Utils::ssdvRadioEnd();
        state = ST_IDLE;
        setStatus("SSDV idle");
    }

    bool startTx() {
        if (!client) return false;
        size_t n = loadImage();
        if (!n) { txing = false; return false; }
        jpegLen = n;
        int st = client->setImage(jpegBuf, jpegLen, (uint8_t)Config.ssdv.quality, -1);
        if (st != RADIOLIB_ERR_NONE || client->numPackets() == 0) { setStatus("SSDV: bad JPEG"); return false; }
        txing = true; state = ST_TX; lastTxMs = millis();
        char buf[40]; snprintf(buf, sizeof(buf), "SSDV TX 0/%u", (unsigned)client->numPackets()); setStatus(buf);
        return true;
    }

    void stopTx() {
        if (txing && client) client->clearImage();
        txing = false;
        if (state == ST_TX) { state = ST_RX; LoRa_Utils::ssdvArmReceive(); }
    }

    bool busy() { return txing; }

    void loop() {
        if (!client || state == ST_IDLE) return;

        if (state == ST_TX && txing) {
            if (!client->isDone()) {
                uint16_t id = 0;
                int st = client->transmit(&id);
                char buf[40]; snprintf(buf, sizeof(buf), "SSDV TX %u/%u", (unsigned)client->currentPacketIndex(), (unsigned)client->numPackets());
                setStatus(buf);
                if (st < 0) { logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "SSDV", "TX error %d", st); }
            } else {
                txing = false; state = ST_RX;
                setStatus("SSDV TX done");
                LoRa_Utils::ssdvArmReceive();
                lastTxMs = millis();
            }
            return;
        }

        // RX: reemision automatica por intervalo
        if (Config.ssdv.intervalMin > 0 && !txing && millis() - lastTxMs > (uint32_t)Config.ssdv.intervalMin * 60000UL) {
            startTx();
            return;
        }

        if (state == ST_RX && LoRa_Utils::ssdvRxReady()) {
            int st = client->read(pkt);
            if (st == RADIOLIB_ERR_NONE && SSDVClient::isValidPacket(pkt, nullptr) == RADIOLIB_ERR_NONE) {
                SSDVPacketInfo_t info; SSDVClient::parseHeader(pkt, &info);
                if (info.imageId != lastRxId) { lastRxId = info.imageId; rxPackets = 0; client->resetDecoder(); }
                rxPackets++;
                int r = client->feedPacket(pkt);
                if (r == RADIOLIB_SSDV_EOI) {
                    uint8_t* jp = nullptr; size_t jl = 0;
                    if (client->getJpeg(&jp, &jl) == RADIOLIB_ERR_NONE && jl > 0) {
                        rxImages++;
                        if (Config.ssdv.rxSave && SPIFFS.begin(true)) {
                            File f = SPIFFS.open("/ssdv_rx.jpg", "w");
                            if (f) { f.write(jp, jl); f.close(); }
                        }
                        char buf[40]; snprintf(buf, sizeof(buf), "SSDV RX image %u (%uB)", (unsigned)rxImages, (unsigned)jl); setStatus(buf);
                    }
                    client->resetDecoder();
                    lastRxId = 0xFFFF;
                }
            }
            LoRa_Utils::ssdvArmReceive();
        }
    }

    const char* status() { return statusText; }
    uint16_t txTotalPackets() { return client ? client->numPackets() : 0; }
    uint16_t txSentPackets()  { return client ? client->currentPacketIndex() : 0; }
    uint16_t rxImageCount()   { return rxImages; }
    uint16_t rxPacketCount()  { return rxPackets; }

    // ---- Subida por BLE ------------------------------------------------------------------------------------------------------

    bool uploadBegin(size_t totalLen) {
        if (totalLen == 0 || totalLen > 200 * 1024) return false;
        if (ulBuf && ulCap < totalLen) { free(ulBuf); ulBuf = nullptr; ulCap = 0; }
        if (!ulBuf) { ulCap = totalLen; ulBuf = bigAlloc(ulCap); if (!ulBuf) { ulCap = 0; return false; } }
        ulLen = 0; ulExpect = totalLen; ulActive = true; ulReady = false;
        return true;
    }

    bool uploadChunk(const uint8_t* data, size_t len) {
        if (!ulActive || !ulBuf) return false;
        if (ulLen + len > ulCap) return false;
        memcpy(ulBuf + ulLen, data, len); ulLen += len;
        return true;
    }

    bool uploadEnd() {
        if (!ulActive) return false;
        ulActive = false;
        ulReady = (ulLen == ulExpect && ulLen > 0);
        return ulReady;
    }

    size_t uploadReceived() { return ulLen; }

}

#else  // !HAS_SSDV: stubs para enlazar sin el modo SSDV

namespace SSDV_Utils {
    void begin() {}
    void end() {}
    void loop() {}
    bool startTx() { return false; }
    void stopTx() {}
    bool busy() { return false; }
    const char* status() { return "SSDV n/a"; }
    uint16_t txTotalPackets() { return 0; }
    uint16_t txSentPackets() { return 0; }
    uint16_t rxImageCount() { return 0; }
    uint16_t rxPacketCount() { return 0; }
    bool uploadBegin(size_t) { return false; }
    bool uploadChunk(const uint8_t*, size_t) { return false; }
    bool uploadEnd() { return false; }
    size_t uploadReceived() { return 0; }
}

#endif
