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

#include <APRSPacketLib.h>
#include <TinyGPS++.h>
#include <SPIFFS.h>
#include "TimeLib.h"
#include "smartbeacon_utils.h"
#include "configuration.h"
#include "station_utils.h"
#include "board_pinout.h"
#include "power_utils.h"
#include "sleep_utils.h"
#include "mag_utils.h"
#include "imu_utils.h"
#include "gps_utils.h"
#include "gnss_sky.h"
#include "time_sync.h"
#include "casic_pcas.h"
#include "display.h"
#include "logger.h"


#ifdef GPS_BAUDRATE
    #define GPS_BAUD    GPS_BAUDRATE
#else
    #define GPS_BAUD    9600
#endif

#define GPS_BAUD_FILE           "/gps_baud.dat"
#define GPS_BAUD_PROBE_TIME_MS  1500
#define GPS_RX_BUFFER           2048
#define GPS_FAST_BAUD           115200  // u-blox M10: el puerto se sube de 9600 a 115200 (EA7JRS)
const uint32_t gpsBaudRates[] = {9600, 115200, 38400, 4800, 57600};     // most common first


extern Configuration        Config;
extern HardwareSerial       gpsSerial;
extern TinyGPSPlus          gps;
extern Beacon               *currentBeacon;
extern logging::Logger      logger;
extern bool                 sendUpdate;
extern bool		            sendStandingUpdate;

extern uint32_t             lastTxTime;
extern uint32_t             txInterval;
extern double               lastTxLat;
extern double               lastTxLng;
extern double               lastTxDistance;
extern uint32_t             lastTx;
extern bool                 disableGPS;
extern bool                 gpsShouldSleep;
extern SmartBeaconValues    currentSmartBeaconValues;

double      currentHeading  = 0;
double      previousHeading = 0;
float       bearing         = 0;

bool        gpsIsActive     = true;

#ifdef GPS_CONFIG_CONSTELLATIONS
    #ifndef GPS_USE_GPS
        #define GPS_USE_GPS         1
    #endif
    #ifndef GPS_USE_GALILEO
        #define GPS_USE_GALILEO     1
    #endif
    #ifndef GPS_USE_BEIDOU
        #define GPS_USE_BEIDOU      0
    #endif
    #ifndef GPS_USE_GLONASS
        #define GPS_USE_GLONASS     0
    #endif
    #ifndef GPS_USE_QZSS
        #define GPS_USE_QZSS        0
    #endif
    #ifndef GPS_USE_SBAS
        #define GPS_USE_SBAS        1
    #endif
    #ifndef GPS_USE_ASSISTNOW_AUTONOMOUS
        #define GPS_USE_ASSISTNOW_AUTONOMOUS    1
    #endif

    // Combinaciones no validas en el MAX-M10S (datasheet UBX-20035208): el receptor las rechazaria
    #if (GPS_USE_SBAS || GPS_USE_QZSS) && !GPS_USE_GPS
        #error "MAX-M10S: SBAS y QZSS solo pueden activarse si GPS tambien esta activado"
    #endif
    #if GPS_USE_BEIDOU && GPS_USE_GLONASS
        #error "MAX-M10S: BeiDou B1I y GLONASS L1OF no pueden activarse a la vez"
    #endif
    #if !GPS_USE_GPS && !GPS_USE_GALILEO && !GPS_USE_BEIDOU && !GPS_USE_GLONASS
        #error "Hay que activar al menos una constelacion"
    #endif

    TinyGPSCustom   gpsSatsInView;          // GPGSV, campo 3: satélites GPS a la vista
    TinyGPSCustom   galileoSatsInView;      // GAGSV, campo 3: satélites Galileo a la vista
    TinyGPSCustom   glonassSatsInView;      // GLGSV
    TinyGPSCustom   beidouSatsInView;       // GBGSV
    TinyGPSCustom   qzssSatsInView;         // GQGSV
    TinyGPSCustom   bdSatsInView;           // BDGSV (talker de BeiDou en el L76K con NMEA 4.0; con 4.1 es GBGSV)
    TinyGPSCustom   gpsAntennaText;         // GPTXT, campo 4: "ANTENNA OK|OPEN|SHORT" (L76K)
    uint8_t         antennaState    = 0;    // Casic::Antenna: 0 desconocido, 1 OK, 2 abierta (sin antena), 3 cortocircuito
    uint8_t         gpsModuleType   = 0;                    // 0 sin respuesta, 1 u-blox, 2 NMEA sin UBX
    // bits: 1 GPS, 2 Galileo, 4 BeiDou, 8 GLONASS, 16 QZSS, 32 SBAS (los mismos que GPS_Utils::GNSS_*)
    uint8_t         gnssMaskValue   = (uint8_t)((GPS_USE_GPS ? 1 : 0) | (GPS_USE_GALILEO ? 2 : 0) | (GPS_USE_BEIDOU ? 4 : 0) |
                                                (GPS_USE_GLONASS ? 8 : 0) | (GPS_USE_QZSS ? 16 : 0) | (GPS_USE_SBAS ? 32 : 0));
    uint8_t         constellationLogStage   = 0;            // 0 = nada, 1 = a los 10 s del fix, 2 = a los 60 s
    uint32_t        firstFixTime            = 0;
#endif


namespace GPS_Utils {

    bool isKnownBaudRate(uint32_t baud) {
        if (baud == GPS_BAUD) return true;
        for (uint32_t knownBaud : gpsBaudRates) {
            if (knownBaud == baud) return true;
        }
        return false;
    }

    uint32_t readSavedBaudRate() {
        File file = SPIFFS.open(GPS_BAUD_FILE, "r");
        if (!file) return 0;
        uint32_t baud = file.readString().toInt();
        file.close();
        return isKnownBaudRate(baud) ? baud : 0;
    }

    void saveBaudRate(uint32_t baud) {
        File file = SPIFFS.open(GPS_BAUD_FILE, "w");
        if (!file) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "GPS", "Could not save baud rate");
            return;
        }
        file.print(baud);
        file.close();
    }

    bool probeBaudRate(uint32_t baud) {
        gpsSerial.end();
        delay(50);
        gpsSerial.setRxBufferSize(GPS_RX_BUFFER);               // antes de begin(); 256 bytes se desbordan si el bucle se para (TX LoRa, pantalla)
        gpsSerial.begin(baud, SERIAL_8N1, GPS_TX, GPS_RX);
        uint32_t validSentences = gps.passedChecksum();
        uint32_t probeStart     = millis();
        while (millis() - probeStart < GPS_BAUD_PROBE_TIME_MS) {
            while (gpsSerial.available() > 0) { const char c = gpsSerial.read(); gps.encode(c); GNSS_Sky::feed(c); }   // GNSS_Sky: cielo para la app (EA7JRS)
            if (gps.passedChecksum() > validSentences) return true;   // NMEA with valid checksum
            delay(10);
        }
        return false;
    }

    uint32_t detectBaudRate() {
        uint32_t savedBaud = readSavedBaudRate();

        uint32_t candidates[2 + sizeof(gpsBaudRates) / sizeof(gpsBaudRates[0])];
        size_t   count = 0;
        auto addCandidate = [&](uint32_t baud) {
            if (baud == 0) return;
            for (size_t i = 0; i < count; i++) {
                if (candidates[i] == baud) return;
            }
            candidates[count++] = baud;
        };
        addCandidate(savedBaud);                                // saved first, then board default, then common ones
        addCandidate(GPS_BAUD);
        for (uint32_t baud : gpsBaudRates) addCandidate(baud);

        for (size_t i = 0; i < count; i++) {
            if (i == 1) displayShow("GPS", "Searching", "baud rate...", 0);
            bool found = probeBaudRate(candidates[i]);
            if (!found && i == 0) found = probeBaudRate(candidates[i]);    // retry first one: GPS may be slow to start
            if (found) {
                if (candidates[i] != savedBaud) saveBaudRate(candidates[i]);
                return candidates[i];
            }
        }
        return 0;
    }

    // ---- PPS, Unicore UC6580 y reset del receptor (Heltec Wireless Tracker y placas con GPS_PPS / GPS_RESET) (EA7JRS) ----------------
    #ifdef GPS_PPS
        // Flanco de subida del PPS = comienzo exacto del segundo UTC. Solo se usa si llega a 1 Hz (intervalo de 950 a 1050 ms)
        static volatile uint32_t ppsMs = 0, ppsPeriodMs = 0;
        static void IRAM_ATTR onPps() {
            const uint32_t t = millis();
            ppsPeriodMs = t - ppsMs;
            ppsMs = t;
        }
    #endif

    bool ppsLocked() {
        #ifdef GPS_PPS
            const uint32_t p = ppsMs, per = ppsPeriodMs;
            return p != 0 && millis() - p < 1100 && per >= 950 && per <= 1050;
        #else
            return false;
        #endif
    }

    // Milisegundos del dia UTC. Con PPS el segundo empieza en su flanco (precision ~1 ms); sin el, la hora de la frase NMEA mas su
    // antiguedad (llega decenas o cientos de ms tarde por la UART)
    bool utcMsOfDay(uint32_t& ms) {
        if (!gpsIsActive || !gps.time.isValid() || gps.time.age() > 10000) return false;
        if (gps.date.isValid() && gps.date.year() < 2024) return false;          // fecha absurda (arranque en frio)
        const uint32_t now = millis();
        uint32_t sec = (uint32_t)gps.time.hour() * 3600UL + (uint32_t)gps.time.minute() * 60UL + gps.time.second();
        #ifdef GPS_PPS
            if (ppsLocked() && gps.time.age() < 1500 && gps.time.centisecond() == 0) {
                const uint32_t p = ppsMs, rx = now - gps.time.age();
                if ((int32_t)(p - rx) > 0) sec += 1;                                    // llego otro flanco despues de la frase: ya es el segundo siguiente
                ms = (sec * 1000UL + (now - p)) % 86400000UL;
                return true;
            }
        #endif
        ms = (sec * 1000UL + gps.time.centisecond() * 10UL + gps.time.age()) % 86400000UL;
        return true;
    }

    #ifdef GNSS_UC6580
        // Unicore UC6580: GPS L1+L5, BeiDou B1I+B2a, GLONASS, Galileo E1+E5a, SBAS y QZSS ($CFGSYS,h35155, como Meshtastic; reinicia el
        // receptor), frases GSA y GSV para el grafico del cielo, sin GLL ni los $TXT de avisos. No se guarda ($CFGSAVE): se repite al
        // encender el receptor, sin bloquear el bucle
        static uint8_t  ucStep = 0;
        static uint32_t ucAt   = 0;
        static void ucStart(uint32_t delayMs) { ucStep = 1; ucAt = millis() + delayMs; }
        static void ucPump() {
            static const char* const cmds[] = { "$CFGSYS,h35155", "$CFGMSG,0,1,0", "$CFGMSG,0,2,1", "$CFGMSG,0,3,1", "$CFGMSG,6,0,0", "$CFGMSG,6,1,0" };
            if (ucStep == 0 || (int32_t)(millis() - ucAt) < 0) return;
            const uint8_t i = ucStep - 1;
            if (i >= sizeof(cmds) / sizeof(cmds[0])) {
                ucStep = 0;
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "GPS", "UC6580: GPS+BDS+GLO+GAL+SBAS+QZSS (L1/L5), GSA y GSV");
                return;
            }
            gpsSerial.print(cmds[i]);
            gpsSerial.print("\r\n");
            ucStep++;
            ucAt = millis() + (i == 0 ? 800 : 150);                 // CFGSYS reinicia el receptor
        }
    #endif

    #ifdef GPS_RESET
        #ifndef GPS_RESET_ACTIVE
            #define GPS_RESET_ACTIVE LOW
        #endif
        // Si el receptor encendido deja de mandar datos 30 s, se reinicia por su pin de reset (como mucho cada 2 min)
        static uint32_t lastGpsByteMs = 0, lastGpsResetMs = 0;
        static void gnssWatchdog(bool gotBytes) {
            const uint32_t now = millis();
            if (gotBytes || !gpsIsActive || lastGpsByteMs == 0) { lastGpsByteMs = now; return; }
            if (now - lastGpsByteMs < 30000 || (lastGpsResetMs != 0 && now - lastGpsResetMs < 120000)) return;
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "GPS", "Sin datos del GNSS en 30 s: reinicio por el pin %d", GPS_RESET);
            pinMode(GPS_RESET, OUTPUT);
            digitalWrite(GPS_RESET, GPS_RESET_ACTIVE);
            delay(20);
            digitalWrite(GPS_RESET, !GPS_RESET_ACTIVE);
            lastGpsResetMs = lastGpsByteMs = now;
            #ifdef GNSS_UC6580
                ucStart(1500);
            #endif
        }
    #endif

    // El receptor acaba de volver a tener alimentacion (modo eco): el UC6580 vuelve a sus valores y no hay que reiniciarlo mientras arranca
    static void receiverPoweredOn() {
        #ifdef GNSS_UC6580
            ucStart(1000);
        #endif
        #ifdef GPS_RESET
            lastGpsByteMs = millis();
        #endif
    }

    #ifdef GPS_CONFIG_CONSTELLATIONS
    struct UbxKeyValue {
        uint32_t    key;
        uint32_t    value;
    };

    void ubxSend(uint8_t msgClass, uint8_t msgId, const uint8_t* payload, uint16_t len) {
        uint8_t ckA = 0;
        uint8_t ckB = 0;
        auto update = [&](uint8_t b) { ckA += b; ckB += ckA; };
        update(msgClass);
        update(msgId);
        update(len & 0xFF);
        update(len >> 8);
        for (uint16_t i = 0; i < len; i++) update(payload[i]);
        gpsSerial.write((uint8_t)0xB5);
        gpsSerial.write((uint8_t)0x62);
        gpsSerial.write(msgClass);
        gpsSerial.write(msgId);
        gpsSerial.write((uint8_t)(len & 0xFF));
        gpsSerial.write((uint8_t)(len >> 8));
        gpsSerial.write(payload, len);
        gpsSerial.write(ckA);
        gpsSerial.write(ckB);
        gpsSerial.flush();
    }

    // 1 = ACK, 0 = NAK, -1 = sin respuesta. Sigue alimentando TinyGPS con las frases NMEA que lleguen.
    int8_t ubxWaitAck(uint8_t msgClass, uint8_t msgId, uint32_t timeoutMs) {
        uint8_t  window[8] = {0};
        uint32_t start     = millis();
        while (millis() - start < timeoutMs) {
            while (gpsSerial.available() > 0) {
                uint8_t b = gpsSerial.read();
                gps.encode((char)b);
                GNSS_Sky::feed((char)b);
                for (uint8_t i = 0; i < 7; i++) window[i] = window[i + 1];
                window[7] = b;
                // B5 62 05 (01 ACK | 00 NAK) 02 00 <class> <id>
                if (window[0] == 0xB5 && window[1] == 0x62 && window[2] == 0x05 &&
                    (window[3] == 0x01 || window[3] == 0x00) &&
                    window[4] == 0x02 && window[5] == 0x00 &&
                    window[6] == msgClass && window[7] == msgId) {
                    return (int8_t)window[3];
                }
            }
            delay(5);
        }
        return -1;
    }

    // Envía un UBX-CFG-VALSET (capas RAM + BBR) con items de 1 byte. 1 = ACK, 0 = NAK, -1 = sin respuesta.
    int8_t ubxValset(const UbxKeyValue* items, uint8_t count) {
        uint8_t payload[4 + 8 * 12];
        uint8_t length = 4;
        payload[0] = 0x00;      // version
        payload[1] = 0x03;      // layers: RAM (bit0) + BBR (bit1)
        payload[2] = 0x00;
        payload[3] = 0x00;
        for (uint8_t i = 0; i < count; i++) {
            uint8_t size = 1;                                   // bits 30:28 de la clave: 1 y 2 = 1 byte, 3 = 2 bytes, 4 = 4 bytes
            switch ((items[i].key >> 28) & 0x07) {
                case 3:  size = 2; break;
                case 4:  size = 4; break;
                default: size = 1; break;
            }
            if ((size_t)(length + 4 + size) > sizeof(payload)) return 0;
            for (uint8_t b = 0; b < 4; b++) payload[length++] = (uint8_t)((items[i].key >> (8 * b)) & 0xFF);
            for (uint8_t b = 0; b < size; b++) payload[length++] = (uint8_t)((items[i].value >> (8 * b)) & 0xFF);
        }
        int8_t result = -1;
        for (uint8_t attempt = 0; attempt < 2 && result == -1; attempt++) {
            ubxSend(0x06, 0x8A, payload, length);
            result = ubxWaitAck(0x06, 0x8A, 500);
        }
        return result;
    }

    // MAX-M10S de fabrica a 9600 baudios: se sube a 115200 (CFG-UART1-BAUDRATE, capas RAM + BBR). El A-GPS de la app entra 12 veces
    // mas rapido y las rafagas NMEA de todas las constelaciones ocupan el puerto 100 ms en vez de un segundo. El ACK sale ya a la
    // nueva velocidad: se da por bueno si llegan frases NMEA validas; si no, se vuelve a la velocidad anterior (EA7JRS)
    uint32_t ubloxFastBaud(uint32_t currentBaud) {
        if (currentBaud >= GPS_FAST_BAUD) return currentBaud;
        const uint32_t b = GPS_FAST_BAUD;
        const uint8_t payload[12] = {0x00, 0x03, 0x00, 0x00,  0x01, 0x00, 0x52, 0x40,
                                     (uint8_t)b, (uint8_t)(b >> 8), (uint8_t)(b >> 16), (uint8_t)(b >> 24)};
        ubxSend(0x06, 0x8A, payload, sizeof(payload));
        delay(100);
        if (probeBaudRate(b)) {
            saveBaudRate(b);
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "GPS", "u-blox: puerto serie a %u baudios", (unsigned)b);
            return b;
        }
        probeBaudRate(currentBaud);
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "GPS", "u-blox: sin respuesta a %u, se mantiene %u", (unsigned)b, (unsigned)currentBaud);
        return currentBaud;
    }

    // Reglas del MAX-M10S: SBAS y QZSS necesitan GPS; BeiDou y GLONASS no a la vez; al menos una constelacion
    bool gnssMaskValid(uint8_t mask) {
        if (gpsModuleType == 2) return (mask & Casic::SUPPORTED_MASK) != 0;      // L76K: cualquier combinacion de GPS / BeiDou / GLONASS
        if ((mask & (GNSS_SBAS | GNSS_QZSS)) && !(mask & GNSS_GPS)) return false;
        if ((mask & GNSS_BEIDOU) && (mask & GNSS_GLONASS)) return false;
        return (mask & (GNSS_GPS | GNSS_GALILEO | GNSS_BEIDOU | GNSS_GLONASS)) != 0;
    }

    uint8_t gnssMask() {
        return gnssMaskValue;
    }

    void loadGnssMask() {                                       // la mascara vive en la configuracion (WebUI); si no es valida se mantiene la de fabrica
        const int value = Config.gps.gnssMask;
        if (value > 0 && value < 64 && gnssMaskValid((uint8_t)value)) gnssMaskValue = (uint8_t)value;
    }

    void saveGnssMask() {                                       // cambio en caliente desde la pantalla GPS: queda en la configuracion
        if (gpsModuleType == 2) Config.gps.cnMask = gnssMaskValue & Casic::SUPPORTED_MASK;     // el L76K tiene su propia mascara: no se pisa la del M10
        else                    Config.gps.gnssMask = gnssMaskValue;
        Config.writeFile();
    }

    // GPS + Galileo (+ SBAS = WAAS/EGNOS; BeiDou, GLONASS y QZSS) en un u-blox M10, segun la mascara activa.
    bool configureConstellations() {
        const uint8_t m = gnssMaskValue;
        const UbxKeyValue items[] = {
            {0x1031001F, (m & GNSS_GPS) ? 1u : 0u},       // CFG-SIGNAL-GPS_ENA
            {0x10310001, (m & GNSS_GPS) ? 1u : 0u},       // CFG-SIGNAL-GPS_L1CA_ENA
            {0x10310021, (m & GNSS_GALILEO) ? 1u : 0u},   // CFG-SIGNAL-GAL_ENA
            {0x10310007, (m & GNSS_GALILEO) ? 1u : 0u},   // CFG-SIGNAL-GAL_E1_ENA
            {0x10310022, (m & GNSS_BEIDOU) ? 1u : 0u},    // CFG-SIGNAL-BDS_ENA
            {0x10310025, (m & GNSS_GLONASS) ? 1u : 0u},   // CFG-SIGNAL-GLO_ENA
            {0x10310024, (m & GNSS_QZSS) ? 1u : 0u},      // CFG-SIGNAL-QZSS_ENA
            {0x10310012, (m & GNSS_QZSS) ? 1u : 0u},      // CFG-SIGNAL-QZSS_L1CA_ENA
            {0x10310020, (m & GNSS_SBAS) ? 1u : 0u},      // CFG-SIGNAL-SBAS_ENA
            {0x10310005, (m & GNSS_SBAS) ? 1u : 0u}       // CFG-SIGNAL-SBAS_L1CA_ENA
        };
        int8_t result = ubxValset(items, sizeof(items) / sizeof(items[0]));
        if (result == 1) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "GPS", "Constellations set: GPS=%d GAL=%d BDS=%d GLO=%d QZSS=%d SBAS=%d",
                        (m & GNSS_GPS) != 0, (m & GNSS_GALILEO) != 0, (m & GNSS_BEIDOU) != 0, (m & GNSS_GLONASS) != 0, (m & GNSS_QZSS) != 0, (m & GNSS_SBAS) != 0);
            delay(100);
            return true;
        }
        if (result == 0) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "GPS", "Constellation config rejected (NAK)");
        } else {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "GPS", "No UBX ACK: not a u-blox M10? constellations unchanged");
        }
        return false;
    }

    // A-GPS offline: AssistNow Autonomous (el M10 la trae desactivada por defecto). Mensaje aparte para que,
    // si esta clave fuera rechazada, no afecte a la configuración de constelaciones.
    void configureAssistNow() {
        const UbxKeyValue items[] = {
            {0x10230001, Config.gps.assistNow ? 1u : 0u}   // CFG-ANA-USE_ANA
        };
        int8_t result = ubxValset(items, 1);
        if (result == 1) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "GPS", "AssistNow Autonomous: %s", Config.gps.assistNow ? "ON" : "OFF");
        } else {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "GPS", "AssistNow Autonomous config failed (%d)", result);
        }
    }

    // Static hold del M10: con velocidad baja y sin alejarse, congela posicion y velocidad. Mensaje aparte: si el
    // receptor rechazara estas claves no afecta al resto de la configuracion.
    void configureStaticHold() {
        if (Config.gps.staticHoldCms <= 0) return;                  // 0 = valores del firmware del receptor
        const UbxKeyValue items[] = {
            {0x20250038, (uint32_t)Config.gps.staticHoldCms},       // CFG-MOT-GNSSSPEED_THRS (0,01 m/s = cm/s)
            {0x3025003B, (uint32_t)Config.gps.staticHoldDistM}      // CFG-MOT-GNSSDIST_THRS (m)
        };
        int8_t result = ubxValset(items, 2);
        if (result == 1) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "GPS", "Static hold: %d cm/s, %d m", Config.gps.staticHoldCms, Config.gps.staticHoldDistM);
        } else {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "GPS", "Static hold no aplicado (%d)", result);
        }
    }

    // Modelo dinamico, tasa de medida y elevacion minima (u-blox M10). Cada grupo va en su propio mensaje
    void configureNavigation() {
        const UbxKeyValue dyn[]  = { {0x20110021, (uint32_t)Config.gps.dynModel} };                 // CFG-NAVSPG-DYNMODEL
        const UbxKeyValue rate[] = { {0x30210001, (uint32_t)(Config.gps.rateMs < 500 ? 500 : Config.gps.rateMs)}, {0x30210002, 1u} };   // 200-250 ms solo para el L76K  // CFG-RATE-MEAS (ms), CFG-RATE-NAV
        const UbxKeyValue elev[] = { {0x201100A4, (uint32_t)Config.gps.minElev} };                  // CFG-NAVSPG-INFIL_MINELEV (grados)
        int8_t r1 = ubxValset(dyn, 1), r2 = ubxValset(rate, 2), r3 = ubxValset(elev, 1);
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "GPS", "Navigation: model %d (%d), rate %d ms (%d), min elev %d deg (%d)",
                   Config.gps.dynModel, r1, Config.gps.rateMs, r2, Config.gps.minElev, r3);
    }

    // Opciones de SBAS (el on/off de la constelacion va con las constelaciones)
    void configureSbas() {
        const UbxKeyValue items[] = {
            {0x10360004, Config.gps.sbasDiffCorr ? 1u : 0u},        // CFG-SBAS-USE_DIFFCORR
            {0x10360003, Config.gps.sbasRanging ? 1u : 0u},         // CFG-SBAS-USE_RANGING
            {0x10360005, Config.gps.sbasIntegrity ? 1u : 0u}        // CFG-SBAS-USE_INTEGRITY
        };
        int8_t result = ubxValset(items, 3);
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "GPS", "SBAS options: diffcorr %d ranging %d integrity %d (%d)",
                   Config.gps.sbasDiffCorr, Config.gps.sbasRanging, Config.gps.sbasIntegrity, result);
    }

    // Salida de pulso (timepulse TP1) del M10: frecuencia (Hz), anchura, polaridad y retardos. Con el pulso desactivado solo se apaga TP1
    void configurePps() {
        if (!Config.gps.ppsEnabled) {
            const UbxKeyValue off[] = { {0x10050007, 0u} };         // CFG-TP-TP1_ENA
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "GPS", "PPS off (%d)", ubxValset(off, 1));
            return;
        }
        uint32_t lenUs = (uint32_t)Config.gps.ppsLenMs * 1000UL;
        const uint32_t maxLenUs = 500000UL / (uint32_t)Config.gps.ppsFreqHz;     // como maximo la mitad del periodo
        if (lenUs > maxLenUs) lenUs = maxLenUs;
        const UbxKeyValue pulse[] = {
            {0x20050023, 1u},                                       // CFG-TP-PULSE_DEF = FREQ
            {0x20050030, 1u},                                       // CFG-TP-PULSE_LENGTH_DEF = LENGTH (us)
            {0x40050024, (uint32_t)Config.gps.ppsFreqHz},           // CFG-TP-FREQ_TP1
            {0x40050025, (uint32_t)Config.gps.ppsFreqHz},           // CFG-TP-FREQ_LOCK_TP1
            {0x40050005, lenUs},                                    // CFG-TP-LEN_LOCK_TP1 (us)
            {0x10050007, 1u}                                        // CFG-TP-TP1_ENA
        };
        const UbxKeyValue shape[] = {
            {0x1005000B, Config.gps.ppsRising ? 1u : 0u},           // CFG-TP-POL_TP1
            {0x10050009, 1u},                                       // CFG-TP-USE_LOCKED_TP1
            {0x30050001, (uint32_t)(int32_t)Config.gps.ppsCableDelayNs},   // CFG-TP-ANT_CABLEDELAY (ns)
            {0x40050006, (uint32_t)(int32_t)Config.gps.ppsUserDelayNs}     // CFG-TP-USER_DELAY_TP1 (ns)
        };
        int8_t r1 = ubxValset(pulse, 6), r2 = ubxValset(shape, 4);
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "GPS", "PPS: %d Hz, %lu us, %s edge (%d/%d)", Config.gps.ppsFreqHz, (unsigned long)lenUs,
                   Config.gps.ppsRising ? "rising" : "falling", r1, r2);
    }

    // ---- Quectel L76K (CASIC AT6558R) y compatibles: ordenes $PCAS (ver include/casic_pcas.h) ------------------------
    void sendPcas(const char* body) {
        char line[96];
        const size_t n = Casic::build(body, line, sizeof(line));
        if (n == 0) return;
        gpsSerial.write((const uint8_t*)line, n);
        gpsSerial.flush();
        delay(60);                                              // el receptor procesa cada orden antes de la siguiente
    }

    void l76kSendConstellations() {
        int mode = Casic::constellationMode(gnssMaskValue);
        if (mode == 0) mode = 1;
        char body[16];
        snprintf(body, sizeof(body), "PCAS04,%d", mode);
        sendPcas(body);
    }

    // 115200 baudios: a 2 Hz o mas, 9600 se queda corto. Solo se queda en la nueva velocidad si llegan tramas NMEA validas
    void l76kFastBaud(uint32_t currentBaud) {
        sendPcas("PCAS01,5");
        delay(100);
        if (probeBaudRate(115200)) {
            saveBaudRate(115200);
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "GPS", "L76K: puerto serie a 115200 baudios");
        } else {
            probeBaudRate(currentBaud);                         // el receptor no cambio: se vuelve a la velocidad anterior
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "GPS", "L76K: sin respuesta a 115200, se mantiene %u", (unsigned)currentBaud);
        }
    }

    void configureL76K(uint32_t baud) {
        gnssMaskValue = (uint8_t)(Casic::sanitizeMask((uint8_t)Config.gps.cnMask) | GNSS_QZSS);       // QZSS va siempre activo en el L76K
        l76kSendConstellations();
        const int period = Casic::rateMs(Config.gps.rateMs);
        char body[48];
        Casic::pcas03Body(Config.gps.nmeaLean, period, body, sizeof(body));
        sendPcas(body);                                         // salida NMEA reducida (o completa)
        snprintf(body, sizeof(body), "PCAS02,%d", period);
        sendPcas(body);                                         // tasa de medida: 1, 2, 4 o 5 Hz
        if (Casic::needsFastBaud(period, baud)) l76kFastBaud(baud);
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "GPS", "L76K: PCAS04 modo %d, %d ms, NMEA %s",
                   Casic::constellationMode(gnssMaskValue), period, Config.gps.nmeaLean ? "reducida" : "completa");
    }

    uint8_t antennaStatus() {
        return antennaState;
    }

    // Estado de la antena (GPTXT de la L76K): se avisa solo cuando cambia
    void updateAntennaStatus() {
        if (!gpsAntennaText.isUpdated()) return;
        const uint8_t state = Casic::antennaFromText(gpsAntennaText.value());
        if (state == 0 || state == antennaState) return;
        antennaState = state;
        logger.log(state == Casic::ANT_OK ? logging::LoggerLevel::LOGGER_LEVEL_INFO : logging::LoggerLevel::LOGGER_LEVEL_WARN, "GPS",
                   "Antenna: %s", state == Casic::ANT_OK ? "OK" : (state == Casic::ANT_OPEN ? "OPEN (no antenna)" : "SHORT circuit"));
    }

    // Tras reactivar la alimentacion del receptor (modo eco) el L76K arranca con sus valores de fabrica: se vuelve a configurar
    void reapplyAfterWake() {
        receiverPoweredOn();
        if (gpsModuleType != 2) return;
        const uint32_t baud = detectBaudRate();
        if (baud != 0) configureL76K(baud);
    }

    // Cambio en caliente desde la pantalla GPS: 1 = aplicado, 0 = combinacion no valida, -1 = el receptor no respondio (no se cambia nada)
    int8_t gnssSet(uint8_t newMask) {
        if (gpsModuleType == 2) {                               // L76K: solo GPS / BeiDou / GLONASS; Galileo, SBAS y QZSS no se pueden cambiar
            if ((newMask ^ gnssMaskValue) & (GNSS_GALILEO | GNSS_SBAS | GNSS_QZSS)) return 2;
            if (!gnssMaskValid(newMask)) return 0;
            if (newMask == gnssMaskValue) return 1;
            gnssMaskValue = (uint8_t)(newMask | GNSS_QZSS);
            l76kSendConstellations();
            saveGnssMask();
            return 1;
        }
        if (!gnssMaskValid(newMask)) return 0;
        if (newMask == gnssMaskValue) return 1;
        uint8_t previous = gnssMaskValue;
        gnssMaskValue = newMask;
        if (!configureConstellations()) {
            gnssMaskValue = previous;
            return -1;
        }
        saveGnssMask();
        return 1;
    }

    // Satelites a la vista (suma de las constelaciones con frases GSV recientes); -1 si aun no hay datos
    int satsInView() {
        TinyGPSCustom* sources[] = { &gpsSatsInView, &galileoSatsInView, &glonassSatsInView, &beidouSatsInView, &qzssSatsInView, &bdSatsInView };
        int total = 0;
        bool any = false;
        for (TinyGPSCustom* source : sources) {
            if (source->isValid() && source->age() < 15000) {
                total += atoi(source->value());
                any = true;
            }
        }
        return any ? total : -1;
    }
    #endif

    uint8_t gpsModule() {
        #ifdef GPS_CONFIG_CONSTELLATIONS
            return gpsModuleType;
        #else
            return 0;
        #endif
    }

    bool gpsIsCasic() {
        return gpsModule() == 2;
    }

    // A-GPS desde la app APRS LoRa+ (EA7JRS): la app prepara los mensajes UBX-MGA (hora y posicion del movil, Predictive
    // Orbits de u-blox descargadas con el internet del movil) y aqui solo se escriben tal cual en el puerto del receptor
    // Sin bloquear el bucle: los datos van a un buffer circular y aidPump() los pasa a la FIFO de la UART segun hay hueco
    // (a 9600 baudios, el M10 de la Supreme, 135 bytes son 140 ms). Si no caben, la app reintenta ("busy").
    #ifdef GPS_CONFIG_CONSTELLATIONS
        static uint8_t aidBuf[1024];
        static size_t  aidHead = 0, aidLen = 0;
    #endif

    size_t aidRoom() {
        #ifdef GPS_CONFIG_CONSTELLATIONS
            return sizeof(aidBuf) - aidLen;
        #else
            return 0;
        #endif
    }

    void aidPump() {
        #ifdef GPS_CONFIG_CONSTELLATIONS
            while (aidLen > 0) {
                const int room = gpsSerial.availableForWrite();
                if (room <= 0) break;
                size_t n = aidLen;
                if (n > (size_t)room) n = (size_t)room;
                if (n > sizeof(aidBuf) - aidHead) n = sizeof(aidBuf) - aidHead;
                gpsSerial.write(aidBuf + aidHead, n);
                aidHead = (aidHead + n) % sizeof(aidBuf);
                aidLen -= n;
            }
        #endif
    }

    void aidClear() {
        #ifdef GPS_CONFIG_CONSTELLATIONS
            aidHead = 0; aidLen = 0;
        #endif
    }

    bool aidWrite(const uint8_t* data, size_t length) {
        #ifdef GPS_CONFIG_CONSTELLATIONS
            if (gpsModule() != 1 || data == nullptr || length == 0 || length > aidRoom()) return false;
            for (size_t i = 0; i < length; i++) aidBuf[(aidHead + aidLen + i) % sizeof(aidBuf)] = data[i];
            aidLen += length;
            aidPump();
            return true;
        #else
            (void)data; (void)length;
            return false;
        #endif
    }

    #ifndef GPS_CONFIG_CONSTELLATIONS
    uint8_t antennaStatus() { return 0; }
    void    reapplyAfterWake() { receiverPoweredOn(); }
    #endif

    bool gpsConfigurable() {
        #ifdef GPS_CONFIG_CONSTELLATIONS
            return true;
        #else
            return false;
        #endif
    }

    void setup() {
        if (disableGPS) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "Main", "GPS disabled");
            return;
        }
        #ifdef LIGHTTRACKER_PLUS_1_0
            pinMode(GPS_VCC, OUTPUT);
            digitalWrite(GPS_VCC, LOW);
            delay(200);
        #endif
        #if defined(F4GOH_1W_LoRa_Tracker_E22_400M30S) || defined(F4GOH_1W_LoRa_Tracker_E220_400M30S)
            pinMode(GPS_VCC, OUTPUT);
            digitalWrite(GPS_VCC, HIGH);
            delay(200);
        #endif

        uint32_t detectedBaud = detectBaudRate();
        if (detectedBaud != 0) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "GPS", "Baud rate detected: %u", detectedBaud);
            #ifdef GPS_CONFIG_CONSTELLATIONS
                loadGnssMask();
                if (configureConstellations()) {
                    gpsModuleType = 1;
                    ubloxFastBaud(detectedBaud);
                    configureAssistNow();
                    configureStaticHold();
                    configureNavigation();
                    configureSbas();
                    configurePps();
                } else {
                    gpsModuleType = 2;                          // habla NMEA pero no responde a UBX: L76K u otro modulo
                    configureL76K(detectedBaud);
                }
            #endif
        } else {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "GPS", "No NMEA data found, using default baud rate: %u", GPS_BAUD);
            gpsSerial.end();
            delay(50);
            gpsSerial.setRxBufferSize(GPS_RX_BUFFER);
            gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_TX, GPS_RX);
        }
        #ifdef GPS_RESET
            pinMode(GPS_RESET, OUTPUT);
            digitalWrite(GPS_RESET, !GPS_RESET_ACTIVE);             // fuera de reset
        #endif
        #ifdef GPS_PPS
            pinMode(GPS_PPS, INPUT);
            attachInterrupt(digitalPinToInterrupt(GPS_PPS), onPps, RISING);
        #endif
        #ifdef GNSS_UC6580
            if (detectedBaud != 0) ucStart(200);
        #endif
        #ifdef GPS_CONFIG_CONSTELLATIONS
            gpsSatsInView.begin(gps, "GPGSV", 3);
            galileoSatsInView.begin(gps, "GAGSV", 3);
            glonassSatsInView.begin(gps, "GLGSV", 3);
            beidouSatsInView.begin(gps, "GBGSV", 3);
            qzssSatsInView.begin(gps, "GQGSV", 3);
            bdSatsInView.begin(gps, "BDGSV", 3);
            gpsAntennaText.begin(gps, "GPTXT", 4);
        #endif
    }

    void calculateDistanceCourse(const String& callsign, double checkpointLatitude, double checkPointLongitude) {
        double distanceKm = TinyGPSPlus::distanceBetween(gps.location.lat(), gps.location.lng(), checkpointLatitude, checkPointLongitude) / 1000.0;
        double courseTo   = TinyGPSPlus::courseTo(gps.location.lat(), gps.location.lng(), checkpointLatitude, checkPointLongitude);
        STATION_Utils::deleteListenedStationsByTime();
        STATION_Utils::orderListenedStationsByDistance(callsign, distanceKm, courseTo);
    }

    void getData() {
        if (disableGPS) return;
        bool gotBytes = false;
        while (gpsSerial.available() > 0) { const char c = gpsSerial.read(); gps.encode(c); GNSS_Sky::feed(c); gotBytes = true; }   // GNSS_Sky: cielo para la app (EA7JRS)
        #ifdef GPS_RESET
            gnssWatchdog(gotBytes);
        #else
            (void)gotBytes;
        #endif
        #ifdef GNSS_UC6580
            ucPump();
        #endif
        if (gps.speed.isUpdated()) updateSpeedFilter();
        #ifdef GPS_CONFIG_CONSTELLATIONS
            if (gpsModuleType == 2) updateAntennaStatus();
            if (constellationLogStage < 2 && gps.location.isValid()) {
                if (firstFixTime == 0) firstFixTime = millis();
                uint32_t wait = (constellationLogStage == 0) ? 10000 : 60000;       // el M10 envia las frases GSV cada 5 s por defecto
                if (millis() - firstFixTime > wait) {
                    constellationLogStage++;
                    logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "GPS", "Fix +%us: used %d, in view -> GPS: %s  Galileo: %s",
                                (unsigned) ((millis() - firstFixTime) / 1000), (int) gps.satellites.value(),
                                gpsSatsInView.isValid() ? gpsSatsInView.value() : "0",
                                galileoSatsInView.isValid() ? galileoSatsInView.value() : "0");
                }
            }
        #endif
    }

    void setDateFromData() {
        if (gps.time.isValid() && gps.date.isValid() && gps.date.year() >= 2024) {
            setTime(gps.time.hour(), gps.time.minute(), gps.time.second(), gps.date.day(), gps.date.month(), gps.date.year());
            TIME_Utils::gpsTimeSet();       // fuente GPS y reloj del sistema al dia (EA7JRS)
        }
    }

    void calculateDistanceTraveled() {
        currentHeading  = getCourseDeg();
        lastTxDistance  = TinyGPSPlus::distanceBetween(gps.location.lat(), gps.location.lng(), lastTxLat, lastTxLng);
        if (lastTx >= txInterval) {
            // En modo clasico, exigir que la unidad este en movimiento o que la distancia supere holgadamente la deriva si no hay IMU
            bool realMove = isMoving() || (!IMU_Utils::isReady() && lastTxDistance > currentSmartBeaconValues.minTxDist);
            if ((realMove && lastTxDistance > currentSmartBeaconValues.minTxDist) || currentSmartBeaconValues.mode != 0) {
                sendUpdate = true;
                sendStandingUpdate = false;
            } else {
                if (currentBeacon->gpsEcoMode) {
                    logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "GPS", "minTxDistance not achieved : %.1f", lastTxDistance);
                    gpsShouldSleep = true;
                }
            }
        }
    }

    void calculateHeadingDelta(int speed) {
        if (currentSmartBeaconValues.mode != 0) return;     // perfiles AIS: el cambio de rumbo ya acorta el intervalo
        if (!isMoving() || speed < currentSmartBeaconValues.slowSpeed) return;  // parado o bajo slowSpeed: no hacer corner pegging

        int TurnMinAngle;
        double headingDelta = fabs(previousHeading - currentHeading);
        if (headingDelta > 180.0) {
            headingDelta = 360.0 - headingDelta;            // wrap-around: 350° -> 10° = 20°, no 340°
        }

        // Giro rápido por giroscopio solo a velocidad de crucero (>= 12 km/h) y con umbral alto (25 dps)
        bool gyroTurn = (speed >= 12) && (millis() > 30000UL) && IMU_Utils::isReady() &&
                        (fabsf(IMU_Utils::getYawRateDps()) >= 25.0f) && (headingDelta >= 10.0);

        if (lastTx > (uint32_t)currentSmartBeaconValues.minDeltaBeacon * 1000UL) {
            TurnMinAngle = currentSmartBeaconValues.turnMinDeg + (currentSmartBeaconValues.turnSlope / (speed > 0 ? speed : 1));
            if (headingDelta > TurnMinAngle || gyroTurn) {
                sendUpdate = true;
                sendStandingUpdate = false;
            }
        }
    }

    void checkStartUpFrames() {
        if (disableGPS) return;
        if ((millis() > 10000 && gps.charsProcessed() < 10)) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "GPS",
                        "No GPS frames detected! Try to reset the GPS Chip with this "
                        "firmware: https://github.com/richonguzman/TTGO_T_BEAM_GPS_RESET");
            displayShow("ERROR", "No GPS frames!", "Reset the GPS Chip", 2000);
        }
    }

    String getHumanBearing(const String& left, const String& center, const String& right) {
        String bearing = ">.";
        bearing += left;
        for (int i = 0; i < 9; i++) {
            bearing += ".";
        }
        bearing += "(";
        bearing += center;
        bearing += ").....";
        if (right.length() == 1 && center.length() != 2) bearing += ".";
        bearing += right;
        bearing += ".<";
        return bearing;
    }

    String getCardinalDirection(float course) {
        if (isMoving()) bearing = course;
        else if (MAG_Utils::isHeadingValid()) bearing = MAG_Utils::heading();   // parado: rumbo del magnetometro

        if (bearing >= 354.375 || bearing < 5.625)    return ">.NW.....(N).....NE.<"; // N
        if (bearing >= 5.625 && bearing < 16.875)     return ">.......N.|.....NE..<";
        if (bearing >= 16.875 && bearing < 28.125)    return ">.....N...|...NE....<"; // NEN
        if (bearing >= 28.125 && bearing < 39.375)    return ">...N.....|.NE......<";
        if (bearing >= 39.375 && bearing < 50.625)    return ">.N......(NE).....E.<"; // NE
        if (bearing >= 50.625 && bearing < 61.875)    return ">.......NE|.....E...<";
        if (bearing >= 61.875 && bearing < 73.125)    return ">.....NE..|...E.....<"; // ENE
        if (bearing >= 73.125 && bearing < 84.375)    return ">...NE....|.E.......<";
        if (bearing >= 84.375 && bearing < 95.625)    return ">.NE.....(E).....SE.<"; // E
        if (bearing >= 95.625 && bearing < 106.875)   return ">.......E.|.....SE..<";
        if (bearing >= 106.875 && bearing < 118.125)  return ">.....E...|...SE....<"; // ESE
        if (bearing >= 118.125 && bearing < 129.375)  return ">...E.....|.SE......<";
        if (bearing >= 129.375 && bearing < 140.625)  return ">.E......(SE).....S.<"; // SE
        if (bearing >= 140.625 && bearing < 151.875)  return ">.......SE|.....S...<";
        if (bearing >= 151.875 && bearing < 163.125)  return ">.....SE..|...S.....<"; // SES
        if (bearing >= 163.125 && bearing < 174.375)  return ">...SE....|.S.......<";
        if (bearing >= 174.375 && bearing < 185.625)  return ">.SE.....(S).....SW.<"; // S
        if (bearing >= 185.625 && bearing < 196.875)  return ">.......S.|.....SW..<";
        if (bearing >= 196.875 && bearing < 208.125)  return ">.....S...|...SW....<"; // SWS
        if (bearing >= 208.125 && bearing < 219.375)  return ">...S.....|.SW......<";
        if (bearing >= 219.375 && bearing < 230.625)  return ">.S......(SW).....W.<"; // SW
        if (bearing >= 230.625 && bearing < 241.875)  return ">.......SW|.....W...<";
        if (bearing >= 241.875 && bearing < 253.125)  return ">.....SW..|...W.....<"; // WSW
        if (bearing >= 253.125 && bearing < 264.375)  return ">...SW....|.W.......<";
        if (bearing >= 264.375 && bearing < 275.625)  return ">.SW.....(W).....NW.<"; // W
        if (bearing >= 275.625 && bearing < 286.875)  return ">.......W.|.....NW..<";
        if (bearing >= 286.875 && bearing < 298.125)  return ">.....W...|...NW....<"; // WNW
        if (bearing >= 298.125 && bearing < 309.375)  return ">...W.....|.NW......<";
        if (bearing >= 309.375 && bearing < 320.625)  return ">.W......(NW).....N.<"; // NW
        if (bearing >= 320.625 && bearing < 331.875)  return ">.......NW|.....N...<";
        if (bearing >= 331.875 && bearing < 343.125)  return ">.....NW..|...N.....<"; // NWN
        if (bearing >= 343.125 && bearing < 354.375)  return ">...NW....|.N.......<";
        return "";
    }

}