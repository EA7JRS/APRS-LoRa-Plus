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

#include <ArduinoJson.h>
#include <cstddef>
#include <SPIFFS.h>
#include "configuration.h"
#include "rotor_follow.h"
#include "board_pins.h"
#include "power_utils.h"
#include "board_pinout.h"
#include "display.h"
#include "ais_vessel.h"
#include "logger.h"

#ifdef NMEA_UART_TX_PIN
    #define NMEA_UART_TX_DEFAULT    NMEA_UART_TX_PIN        // el board_pinout.h de la variante (-1 = apagado)
#else
    #define NMEA_UART_TX_DEFAULT    -1
#endif
#ifdef NMEA_UART_BAUD
    #define NMEA_UART_BAUD_DEFAULT  NMEA_UART_BAUD
#else
    #define NMEA_UART_BAUD_DEFAULT  38400
#endif

// Pines por defecto de LEDs, flash y buzzer (todo apagado por defecto). En la T-Beam S3 Supreme los de otras placas chocan con la radio, el IMU o el RTC:
// se usan pines libres de su cabecera de expansion (38 LED Tx, 21 LED mensaje, 45 flash, 39 tono y 48 Vcc del buzzer; el 2 (ADC1) queda para el teclado FH-2, 44 / 46 para el manipulador y 43 para la salida NMEA; esquema en docs/HARDWARE_SUPREME_EXTRAS.md)
#ifdef TTGO_T_Beam_S3_SUPREME_V3
    #define NOTIF_DEF_LED_TX        38
    #define NOTIF_DEF_LED_MESSAGE   21
    #define NOTIF_DEF_FLASHLIGHT    45
    #define NOTIF_DEF_BUZZER_TONE   39
    #define NOTIF_DEF_BUZZER_VCC    48
#else
    #define NOTIF_DEF_LED_TX        13
    #define NOTIF_DEF_LED_MESSAGE   2
    #define NOTIF_DEF_FLASHLIGHT    14
    #define NOTIF_DEF_BUZZER_TONE   33
    #define NOTIF_DEF_BUZZER_VCC    25
#endif

// Pin por defecto de LEDs, flash y buzzer: -1 (sin asignar) si lo ocupa la propia placa o no sirve para una salida
static int defaultPin(int pin) {
    return (POWER_Utils::pinReserved(pin) || BOARD_PINS::inputOnly(pin)) ? -1 : pin;
}

// ---- Tabla de campos (Manuel - EA7JRS) ----------------------------------------------------------------------------------------------
// Un registro por ajuste en lugar de dos lineas de ArduinoJson por ajuste (leer y escribir): el mismo JSON con bastante menos codigo.
// El tipo se deduce del miembro de la clase, asi que no puede desajustarse. El orden de la tabla es el orden del fichero.
// Lo que necesita logica propia (combinaciones validas, listas de valores permitidos...) se comprueba despues de leer cada grupo.
namespace {
    enum FieldType : uint8_t { FT_BOOL, FT_INT, FT_LONG, FT_U8, FT_U16, FT_U32, FT_FLT, FT_DBL, FT_STR };
    enum : uint8_t { FF_REQUIRED = 1,       // falta en el fichero -> se reescribe con los valores por defecto (configuraciones antiguas)
                     FF_PIN      = 2,       // el valor por defecto es un pin de defaultPin()
                     FF_KEEP     = 4,       // lo que falta en el fichero conserva el valor que ya tiene el miembro (los set*Defaults)
                     FF_CLAMP    = 8,       // se limita a [lo, hi]
                     FF_RESET    = 16,      // fuera de [lo, hi] -> valor por defecto
                     FF_NOREAD   = 32 };    // solo se escribe: la lectura la hace el codigo del grupo
    struct Field {
        const char* key;
        uint16_t    offset;
        uint8_t     type;
        uint8_t     flags;
        int32_t     lo, hi;
        union { int32_t def; const char* sdef; };       // enteros y logicos; decimales en milesimas; cadenas
        constexpr Field(const char* k, uint16_t o, uint8_t t, uint8_t f, int32_t d, int32_t l, int32_t h)
            : key(k), offset(o), type(t), flags(f), lo(l), hi(h), def(d) {}
        constexpr Field(const char* k, uint16_t o, uint8_t t, uint8_t f, const char* d)
            : key(k), offset(o), type(t), flags(f), lo(0), hi(0), sdef(d) {}
    };
    template <typename T> struct FType;
    template <> struct FType<bool>     { static constexpr uint8_t v = FT_BOOL; };
    template <> struct FType<int>      { static constexpr uint8_t v = FT_INT; };
    template <> struct FType<long>     { static constexpr uint8_t v = FT_LONG; };
    template <> struct FType<uint8_t>  { static constexpr uint8_t v = FT_U8; };
    template <> struct FType<uint16_t> { static constexpr uint8_t v = FT_U16; };
    template <> struct FType<uint32_t> { static constexpr uint8_t v = FT_U32; };
    template <> struct FType<float>    { static constexpr uint8_t v = FT_FLT; };
    template <> struct FType<double>   { static constexpr uint8_t v = FT_DBL; };
    template <> struct FType<String>   { static constexpr uint8_t v = FT_STR; };
    #define FLD(cls, jkey, member, flags, def, lo, hi)  Field(jkey, (uint16_t)offsetof(cls, member), FType<decltype(cls::member)>::v, (uint8_t)(flags), (int32_t)(def), (int32_t)(lo), (int32_t)(hi))
    #define FB(cls, key, flags, def)                    FLD(cls, #key, key, flags, def, 0, 0)                           // sin limites
    #define FC(cls, key, flags, def, lo, hi)            FLD(cls, #key, key, (flags) | FF_CLAMP, def, lo, hi)            // limitado a [lo, hi]
    #define FR(cls, key, flags, def, lo, hi)            FLD(cls, #key, key, (flags) | FF_RESET, def, lo, hi)            // fuera de [lo, hi] -> def
    #define FS(cls, key, flags, str)                    Field(#key, (uint16_t)offsetof(cls, key), FT_STR, (uint8_t)(flags), (const char*)(str))
    #define NFIELDS(t)                                  (sizeof(t) / sizeof((t)[0]))

    void writeFields(JsonObject o, const void* base, const Field* f, size_t n) {
        for (size_t i = 0; i < n; i++) {
            const char* p = (const char*)base + f[i].offset;
            switch (f[i].type) {
                case FT_BOOL: o[f[i].key] = *(const bool*)p;      break;
                case FT_INT:  o[f[i].key] = *(const int*)p;       break;
                case FT_LONG: o[f[i].key] = *(const long*)p;      break;
                case FT_U8:   o[f[i].key] = *(const uint8_t*)p;   break;
                case FT_U16:  o[f[i].key] = *(const uint16_t*)p;  break;
                case FT_U32:  o[f[i].key] = *(const uint32_t*)p;  break;
                case FT_FLT:  o[f[i].key] = *(const float*)p;     break;
                case FT_DBL:  o[f[i].key] = *(const double*)p;    break;
                default:      o[f[i].key] = *(const String*)p;    break;
            }
        }
    }

    // Devuelve true si falta algun campo obligatorio (hay que reescribir el fichero)
    bool readFields(JsonVariantConst o, void* base, const Field* f, size_t n) {
        bool rewrite = false;
        for (size_t i = 0; i < n; i++) {
            const Field& d = f[i];
            const JsonVariantConst v = o[d.key];
            if ((d.flags & FF_REQUIRED) && v.isNull()) rewrite = true;
            if (d.flags & FF_NOREAD) continue;
            char* p = (char*)base + d.offset;
            const bool keep = d.flags & FF_KEEP;
            switch (d.type) {
                case FT_BOOL: *(bool*)p = v | (keep ? *(bool*)p : d.def != 0); break;
                case FT_FLT:  *(float*)p = v | (keep ? *(float*)p : d.def / 1000.0f); break;
                case FT_DBL: {
                    double x = v | (keep ? *(double*)p : (double)d.def);
                    *(double*)p = (d.flags & FF_CLAMP) ? constrain(x, (double)d.lo, (double)d.hi) : x;
                    break;
                }
                case FT_STR:
                    if (keep) { if (!v.isNull()) *(String*)p = v.as<String>(); }
                    else      *(String*)p = v | d.sdef;
                    break;
                default: {                                                  // enteros
                    int cur = 0;
                    if (keep) {
                        switch (d.type) {
                            case FT_LONG: cur = (int)*(long*)p;     break;
                            case FT_U8:   cur = *(uint8_t*)p;       break;
                            case FT_U16:  cur = *(uint16_t*)p;      break;
                            case FT_U32:  cur = (int)*(uint32_t*)p; break;
                            default:      cur = *(int*)p;           break;
                        }
                    }
                    const int def = keep ? cur : ((d.flags & FF_PIN) ? defaultPin(d.def) : (int)d.def);
                    int x = v | def;                                    // long e int miden lo mismo en el ESP32
                    if (d.flags & FF_CLAMP) x = constrain(x, (int)d.lo, (int)d.hi);
                    else if ((d.flags & FF_RESET) && (x < d.lo || x > d.hi)) x = def;
                    switch (d.type) {
                        case FT_LONG: *(long*)p     = x;           break;
                        case FT_U8:   *(uint8_t*)p  = (uint8_t)x;  break;
                        case FT_U16:  *(uint16_t*)p = (uint16_t)x; break;
                        case FT_U32:  *(uint32_t*)p = (uint32_t)x; break;
                        default:      *(int*)p      = x;           break;
                    }
                    break;
                }
            }
        }
        return rewrite;
    }

    const Field kWifiApFields[] = {
        FB(WiFiAP, active,   FF_REQUIRED, true),
        FS(WiFiAP, password, FF_REQUIRED, "1234567890"),
    };
    const Field kFwUpdateFields[] = {
        FS(FwUpdate, ssid,     FF_REQUIRED, ""),
        FS(FwUpdate, password, 0,           ""),
    };
    const Field kKissNetFields[] = {
        FB(KissNetConfig, enabled, 0,           false),
        FC(KissNetConfig, tcpPort, FF_REQUIRED, 8001, 1, 65535),
        FC(KissNetConfig, udpPort, 0,           0,    0, 65535),
        FS(KissNetConfig, allowed, 0,           ""),
    };
    const Field kNmeaUartFields[] = {
        FC(NmeaUartConfig, txPin, 0,           NMEA_UART_TX_DEFAULT, -1, 48),
        FB(NmeaUartConfig, baud,  FF_REQUIRED, NMEA_UART_BAUD_DEFAULT),
    };
    const Field kIaruAutoFields[] = {
        FC(IaruAutoConfig, mode,    0, 0, 0, 2),
        FB(IaruAutoConfig, confirm, 0, true),
    };
    const Field kCatFields[] = {
        FB(CatConfig, enabled,   0,           false),
        FB(CatConfig, usb,       0,           false),
        FC(CatConfig, port,      FF_REQUIRED, 4532, 1, 65535),
        FS(CatConfig, allowed,   0,           ""),
        FB(CatConfig, pttBeacon, 0,           false),
        FC(CatConfig, tuneKHz,   0,           0, 0, 500),
    };
    const Field kTeamFields[] = {
        FB(TeamConfig, watchActive, FF_REQUIRED, false),
        FS(TeamConfig, watchCalls,  0,           ""),
        FC(TeamConfig, timeoutMin,  0,           10, 1, 120),
        FC(TeamConfig, maxKm,       0,           0,  0, 500),
        FB(TeamConfig, pollSec,     0,           0),                                // 0 o 60..3600: se comprueba al leer
        FB(TeamConfig, eaiActive,   0,           false),
        FS(TeamConfig, eaiCalls,    0,           ""),
        FS(TeamConfig, eaiCode,     0,           ""),
        FC(TeamConfig, eaiMinutes,  0,           30, 1, 50),
    };
    const Field kBeaconFields[] = {
        FS(Beacon, callsign,           0, "NOCALL-7"),
        FS(Beacon, symbol,             0, "["),
        FS(Beacon, overlay,            0, "/"),
        FS(Beacon, micE,               0, ""),
        FS(Beacon, comment,            0, ""),
        FB(Beacon, smartBeaconActive,  0, true),
        FB(Beacon, smartBeaconSetting, 0, 0),
        FB(Beacon, gpsEcoMode,         0, false),
        FS(Beacon, profileLabel,       0, ""),
        FS(Beacon, status,             0, ""),
        FB(Beacon, aisProfile,         0, false),
        FB(Beacon, aisVesselType,      0, 0),
        FS(Beacon, aisName,            0, ""),
        FB(Beacon, aisMmsi,            0, 0),
        FB(Beacon, aisLength,          0, 0),
        FB(Beacon, aisBeam,            0, 0),
        FB(Beacon, aisDraft,           0, 0),
    };
    const Field kDisplayFields[] = {
        FB(Display, ecoMode,    FF_REQUIRED, false),
        FB(Display, timeout,    FF_REQUIRED, 4),
        FB(Display, turn180,    FF_REQUIRED, false),
        FB(Display, showSymbol, FF_REQUIRED, true),
    };
    #ifdef HAS_BT_CLASSIC
        #define BT_USE_KISS_DEFAULT false
    #else
        #define BT_USE_KISS_DEFAULT true      // true = KISS, false = TNC2
    #endif
    const Field kBluetoothFields[] = {
        FB(BLUETOOTH, active,     FF_REQUIRED, false),
        FS(BLUETOOTH, deviceName, FF_REQUIRED, "LoRaTracker"),
        FB(BLUETOOTH, useBLE,     FF_REQUIRED, false),                              // sin Bluetooth clasico queda fijo en BLE
        FB(BLUETOOTH, useKISS,    FF_REQUIRED, BT_USE_KISS_DEFAULT),
        FC(BLUETOOTH, controlPin, 0,           123456, 0, 999999),
    };
    const Field kCwFields[] = {                                                     // los valores que faltan parten de setCwDefaults()
        FC(CWConfig, wpm,           FF_REQUIRED | FF_KEEP, 0, 5, 40),
        FC(CWConfig, keyMode,       FF_REQUIRED | FF_KEEP, 0, 0, 2),
        FB(CWConfig, keyPin,        FF_KEEP, 0),
        FB(CWConfig, dahPin,        FF_KEEP, 0),
        FB(CWConfig, swapPaddles,   FF_KEEP, 0),
        FB(CWConfig, keypadPin,     FF_KEEP, 0),
        FB(CWConfig, keyTx,         FF_KEEP, 0),
        FB(CWConfig, sidetone,      FF_KEEP, 0),
        FC(CWConfig, trainerLevel,  FF_REQUIRED | FF_KEEP, 0, 2, 41),
        FC(CWConfig, trainerGapWpm, FF_KEEP, 0, 0, 40),
        FC(CWConfig, squelchDbm,    FF_KEEP, 0, -130, 0),
        FC(CWConfig, beaconMin,     FF_REQUIRED | FF_KEEP, 0, 0, 240),
        FB(CWConfig, beaconFreq,    0, 0),                                          // 0 = se completa con el perfil CW al terminar de leer
        FB(CWConfig, beaconPower,   0, 0),
        FC(CWConfig, beaconRegion,  0, 1, 1, 3),
        FB(CWConfig, keyFreq,       0, 0),
        FB(CWConfig, keyPower,      0, 0),
        FC(CWConfig, keyRegion,     0, 1, 1, 3),
        FS(CWConfig, beaconText,    FF_KEEP, ""),
    };
    const Field kGpsFields[] = {                                                    // los valores que faltan parten de setGpsDefaults()
        FB(GPSConfig, gnssMask,        FF_REQUIRED | FF_KEEP, 0),                   // combinaciones validas: se comprueba al leer
        FB(GPSConfig, assistNow,       FF_KEEP, 0),
        FB(GPSConfig, dynModel,        FF_KEEP, 0),
        FC(GPSConfig, rateMs,          FF_KEEP, 0, 200, 10000),                     // 200-250 ms solo los aplica el L76K; el M10 se limita a 500 ms
        FC(GPSConfig, minElev,         FF_KEEP, 0, 0, 30),
        FC(GPSConfig, staticHoldCms,   FF_KEEP, 0, 0, 255),
        FC(GPSConfig, staticHoldDistM, FF_KEEP, 0, 0, 1000),
        FB(GPSConfig, sbasDiffCorr,    FF_KEEP, 0),
        FB(GPSConfig, sbasRanging,     FF_KEEP, 0),
        FB(GPSConfig, sbasIntegrity,   FF_KEEP, 0),
        FB(GPSConfig, ppsEnabled,      FF_KEEP, 0),
        FC(GPSConfig, ppsFreqHz,       FF_KEEP, 0, 1, 10),
        FC(GPSConfig, ppsLenMs,        FF_KEEP, 0, 1, 500),
        FB(GPSConfig, ppsRising,       FF_KEEP, 0),
        FC(GPSConfig, ppsCableDelayNs, FF_KEEP, 0, -32000, 32000),
        FC(GPSConfig, ppsUserDelayNs,  FF_KEEP, 0, -1000000, 1000000),
        FB(GPSConfig, cnMask,          FF_REQUIRED | FF_KEEP, 0),
        FB(GPSConfig, nmeaLean,        FF_REQUIRED | FF_KEEP, 0),
    };
    const Field kWsprFields[] = {                                                   // los valores que faltan parten de setWsprDefaults()
        FC(WSPRConfig, freqHz,       FF_REQUIRED | FF_KEEP, 0, 400000000, 480000000),
        FC(WSPRConfig, corrHz,       FF_KEEP, 0, -5000, 5000),
        FB(WSPRConfig, useGlobalPpm, FF_REQUIRED, false),
        FC(WSPRConfig, powerDbm,     FF_KEEP, 0, 0, 20),
        FC(WSPRConfig, intervalMin,  FF_KEEP, 0, 2, 60),
        FS(WSPRConfig, locator,      0, ""),
        FB(WSPRConfig, dither,       FF_KEEP, 0),
        FB(WSPRConfig, screenOff,    FF_KEEP, 0),
        FC(WSPRConfig, driftTenths,  FF_KEEP, 0, -200, 200),
        FB(WSPRConfig, perPower,     FF_KEEP, 0),
    };
    const Field kDapnetFields[] = {                                                 // los valores que faltan parten de setDapnetDefaults()
        FC(DAPNETConfig, freqHz,        FF_REQUIRED | FF_KEEP, 0, 400000000, 480000000),
        FB(DAPNETConfig, baud,          FF_KEEP, 0),
        FC(DAPNETConfig, devTenthsKHz,  FF_KEEP, 0, 20, 80),
        FB(DAPNETConfig, rxBwTenthsKHz, FF_KEEP, 0),
        FS(DAPNETConfig, rics,          0, ""),
        FB(DAPNETConfig, monitorAll,    FF_KEEP, 0),
        FB(DAPNETConfig, timeSync,      FF_KEEP, 0),
        FC(DAPNETConfig, charset,       FF_KEEP, 0, 0, 1),
    };
    const Field kBcastFields[] = {                                                  // los valores que faltan parten de setBcastDefaults()
        FB(BcastConfig, enabled,  FF_KEEP, 0),
        FC(BcastConfig, resetPin, FF_KEEP, -1, -1, 48),
        FC(BcastConfig, band,     FF_KEEP, 0, 0, 3),
        FC(BcastConfig, fmKHz,    FF_KEEP, 98000, 64000, 108000),
        FC(BcastConfig, mwKHz,    FF_KEEP, 999, 520, 1710),
        FC(BcastConfig, swKHz,    FF_KEEP, 9500, 2300, 26100),
        FC(BcastConfig, lwKHz,    FF_KEEP, 198, 153, 279),
        FC(BcastConfig, volume,   FF_KEEP, 8, 0, 15),
        FB(BcastConfig, mono,     FF_KEEP, 0),
        FB(BcastConfig, deemph75, FF_KEEP, 0),
        FB(BcastConfig, fmWide,   FF_KEEP, 0),
        FB(BcastConfig, mw10k,    FF_KEEP, 0),
    };
    const Field kAisFields[] = {                                                    // los valores que faltan parten de setAisDefaults()
        FB(AisConfig, module,        FF_KEEP, 0),
        FC(AisConfig, rxPin,         FF_KEEP, -1, -1, 48),
        FC(AisConfig, baud,          FF_KEEP, 38400, 4800, 115200),
        FC(AisConfig, channel,       FF_KEEP, 0, 0, 2),
        FC(AisConfig, altSec,        FF_KEEP, 30, 5, 600),
        FB(AisConfig, cpaAlarm,      FF_KEEP, 1),
        FC(AisConfig, cpaTenthNm,    FF_KEEP, 5, 1, 100),
        FC(AisConfig, tcpaMin,       FF_KEEP, 15, 1, 120),
        FC(AisConfig, guardTenthNm,  FF_KEEP, 0, 0, 200),
        FB(AisConfig, distressAlarm, FF_KEEP, 1),
        FC(AisConfig, maxAgeMin,     FF_KEEP, 10, 1, 120),
        FB(AisConfig, forwardNmea,   FF_KEEP, 1),
        FC(AisConfig, hpfHz,         FF_KEEP, 320, 0, 2000),
    };
#ifdef HAS_SA868
    const Field kSa868Fields[] = {                                                  // los valores que faltan parten de setSa868Defaults()
        FC(Sa868Config, rxFreqHz,  FF_REQUIRED | FF_KEEP, 0, 134000000, 480000000),
        FC(Sa868Config, txFreqHz,  FF_KEEP, 0, 134000000, 480000000),
        FB(Sa868Config, highPower, FF_KEEP, 0),
        FB(Sa868Config, wide,      FF_KEEP, 0),
        FC(Sa868Config, volume,    FF_KEEP, 0, 1, 8),
        FC(Sa868Config, squelch,   FF_KEEP, 0, 0, 8),
        FC(Sa868Config, txLevel,   FF_KEEP, 0, 1, 100),
        FC(Sa868Config, txLevel9600, FF_KEEP, 0, 1, 100),
        FC(Sa868Config, fec,       FF_KEEP, 0, 0, 4),
        FC(Sa868Config, txDelayMs, FF_KEEP, 0, 50, 1000),
        FC(Sa868Config, txTailMs,  FF_KEEP, 0, 10, 300),
        FB(Sa868Config, filters,   FF_KEEP, 0),
        FB(Sa868Config, speaker,   FF_KEEP, 0),
        FB(Sa868Config, revision,  FF_KEEP, 0),
        FC(Sa868Config, iaruRegion, FF_KEEP, 0, 1, 3),
        FB(Sa868Config, m17FreqHz, FF_KEEP, 0),
        FC(Sa868Config, m17Can,    FF_KEEP, 0, 0, 15),
        FC(Sa868Config, txLevelM17, FF_KEEP, 0, 1, 100),
        FB(Sa868Config, dapnetFreqHz, FF_KEEP, 0),
        FC(Sa868Config, dapnetHpfHz, FF_KEEP, 0, 0, 2000),
        FB(Sa868Config, cwMcw,     FF_KEEP, 0),
        FC(Sa868Config, cwToneHz,  FF_KEEP, 800, 300, 1500),
    };
    const Field kFmMemFields[] = {
        Field("n",   (uint16_t)offsetof(FmMemory, name),     FT_STR,  0,        ""),
        FC(FmMemory, rxHz,     0, 0, 0, 480000000),
        FC(FmMemory, shift,    0, 0, -1, 1),
        FC(FmMemory, offsetHz, 0, 600000, 0, 99900000),
        FC(FmMemory, toneMode, 0, 0, 0, 4),
        FC(FmMemory, tone,     0, 885, 670, 2541),
        FC(FmMemory, dcs,      0, 23, 23, 754),
        FB(FmMemory, dcsInv,   0, 0),
        FB(FmMemory, wide,     0, 0),
        FB(FmMemory, high,     0, 0),
    };
    const Field kFmFields[] = {                                                     // los valores que faltan parten de setFmDefaults()
        FC(FmConfig, stepIndex,    FF_KEEP, 3, 0, 8),
        FB(FmConfig, memMode,      FF_KEEP, 0),
        FC(FmConfig, memIndex,     FF_KEEP, 0, 0, FmConfig::MEMORIES - 1),
        FC(FmConfig, scanResume,   FF_KEEP, 0, 0, 2),
        FB(FmConfig, scanMemories, FF_KEEP, 1),
        FB(FmConfig, scanLoHz,     FF_KEEP, 0),
        FB(FmConfig, scanHiHz,     FF_KEEP, 0),
        FC(FmConfig, artsInterval, FF_KEEP, 25, 15, 25),
        FC(FmConfig, artsDcs,      FF_KEEP, 23, 23, 754),
        FB(FmConfig, cwId,         FF_KEEP, 1),
        FC(FmConfig, cwWpm,        FF_KEEP, 20, 5, 40),
        FC(FmConfig, eaiMode,      FF_KEEP, 1, 1, 2),
        FC(FmConfig, eaiMinutes,   FF_KEEP, 10, 1, 50),
        FC(FmConfig, manDownMin,   FF_KEEP, 0, 0, 120),
        FC(FmConfig, totSec,       FF_KEEP, 180, 0, 600),
        FB(FmConfig, aprsBeacon,   FF_KEEP, 1),
        FB(FmConfig, qsyInBeacon,  FF_KEEP, 1),
        FB(FmConfig, squelchAuto,  FF_KEEP, 1),
        FC(FmConfig, nrLevel,      FF_KEEP, 0, 0, 3),
        FB(FmConfig, satOn,        FF_KEEP, 0),
        FC(FmConfig, satTx,        FF_KEEP, 0, 0, 5),
    };
#endif
    const Field kSatFields[] = {                                                    // los valores que faltan parten de setDapnetDefaults()
        FB(SatConfig, noradId,        FF_KEEP, 0),
        FC(SatConfig, category,       FF_KEEP, 0, 0, 6),
        FS(SatConfig, favorites,      0, ""),
        FS(SatConfig, groups,         0, ""),
        FS(SatConfig, aprsPath,       0, "ARISS"),
        FC(SatConfig, posSource,      FF_KEEP, 0, 0, 2),
        FC(SatConfig, minEl,          FF_REQUIRED | FF_KEEP, 0, 0, 60),
        FC(SatConfig, lat,            FF_KEEP, 0, -90, 90),
        FC(SatConfig, lon,            FF_KEEP, 0, -180, 180),
        FB(SatConfig, txEnabled,      FF_REQUIRED | FF_KEEP, 0),
        FC(SatConfig, txMinEl,        FF_KEEP, 0, 0, 60),
        FC(SatConfig, txPower,        FF_KEEP, 0, -9, 22),
        FC(SatConfig, txMinIntervalS, FF_KEEP, 0, 5, 3600),
        FS(SatConfig, txText,         0, ""),
        FC(SatConfig, lnaPin,         FF_KEEP, 0, -1, 99),
        FC(SatConfig, paPin,          FF_KEEP, 0, -1, 99),
        FC(SatConfig, pttPin,         FF_KEEP, 0, -1, 99),
        FB(SatConfig, lnaRev,         FF_KEEP, 0),
        FB(SatConfig, paRev,          FF_KEEP, 0),
        FB(SatConfig, pttRev,         FF_KEEP, 0),
        FC(SatConfig, seqLeadMs,      FF_KEEP, 0, 0, 2000),
        FC(SatConfig, seqPreMs,       FF_KEEP, 0, 0, 2000),
        FC(SatConfig, seqPostMs,      FF_KEEP, 0, 0, 2000),
        FC(SatConfig, seqTailMs,      FF_KEEP, 0, 0, 2000),
    };
    const Field kRotorFields[] = {                                                  // los valores que faltan parten de setDapnetDefaults()
        FB(RotorConfig, enabled,       FF_REQUIRED | FF_KEEP, 0),
        FC(RotorConfig, transport,     FF_KEEP, 0, 0, 1),
        FS(RotorConfig, host,          0, ""),
        FC(RotorConfig, port,          FF_KEEP, 0, 1, 65535),
        FS(RotorConfig, btName,        0, ""),
        FC(RotorConfig, stepTenths,    FF_KEEP, 0, 1, 100),
        FC(RotorConfig, preSec,        FF_KEEP, 0, 0, 600),
        FC(RotorConfig, azOffset,      FF_KEEP, 0, -180, 180),
        FC(RotorConfig, elOffset,      FF_KEEP, 0, -90, 90),
        FB(RotorConfig, park,          FF_KEEP, 0),
        FC(RotorConfig, parkAz,        FF_KEEP, 0, 0, 359),
        FC(RotorConfig, parkEl,        FF_KEEP, 0, 0, 90),
        FB(RotorConfig, bleEvents,     FF_KEEP, 0),
        FB(RotorConfig, serverEnabled, FF_REQUIRED | FF_KEEP, 0),
        FC(RotorConfig, serverPort,    FF_KEEP, 0, 1, 65535),
        FC(RotorConfig, tilt,          FF_KEEP, 0, 0, 1),
        FC(RotorConfig, mount,         FF_KEEP, 0, 0, 1),
        FB(RotorConfig, source,        FF_KEEP, 0),                                 // se valida al leer (RotorFollow::validSource)
        FS(RotorConfig, followFilter,  FF_KEEP | FF_NOREAD, ""),                    // se lee a mano: un valor que no es texto no cambia nada
        FC(RotorConfig, followMaxMin,  FF_KEEP, 0, 1, 720),
        FB(RotorConfig, predict,       FF_KEEP, 0),
        FC(RotorConfig, predictMaxS,   FF_KEEP, 0, 10, 1800),
        FC(RotorConfig, targetIndex,   FF_KEEP, 0, 0, RotorConfig::TARGETS - 1),
    };
    const Field kRotorTargetFields[] = {
        Field("n",   (uint16_t)offsetof(RotorConfig::Target, name),   FT_STR,  0,        ""),
        Field("lat", (uint16_t)offsetof(RotorConfig::Target, lat),    FT_DBL,  FF_CLAMP, 0, -90,   90),
        Field("lon", (uint16_t)offsetof(RotorConfig::Target, lon),    FT_DBL,  FF_CLAMP, 0, -180,  180),
        Field("alt", (uint16_t)offsetof(RotorConfig::Target, alt),    FT_INT,  FF_CLAMP, 0, -500,  9000),
        Field("f",   (uint16_t)offsetof(RotorConfig::Target, freqHz), FT_LONG, 0,        0, 0,     0),
    };
    const Field kFtFields[] = {                                                     // los valores que faltan parten de setFtDefaults()
        FC(FTConfig, ft8FreqHz,  FF_REQUIRED | FF_KEEP, 0, 400000000, 480000000),
        FB(FTConfig, ft4FreqHz,  FF_KEEP, 0),                                       // 0 o 400-480 MHz: se comprueba al leer
        FC(FTConfig, ft8Region,  FF_KEEP, 0, 1, 3),
        FC(FTConfig, ft4Region,  FF_KEEP, 0, 1, 3),
        FB(FTConfig, fstFreqHz,  FF_KEEP, 0),
        FC(FTConfig, fstRegion,  FF_KEEP, 0, 1, 3),
        FB(FTConfig, fstSeconds, FF_KEEP, 0),                                       // 120 o 300
        FC(FTConfig, fstEvery,   FF_KEEP, 0, 2, 240),
        FC(FTConfig, corrHz,     FF_KEEP, 0, -5000, 5000),
        FC(FTConfig, powerDbm,   FF_KEEP, 0, 0, 22),
        FC(FTConfig, everyCycles, FF_KEEP, 0, 2, 240),
        FS(FTConfig, locator,    0, ""),
        FS(FTConfig, text,       0, ""),
        FB(FTConfig, screenOff,  FF_KEEP, 0),
    };
    const Field kLrfhssFields[] = {                                                 // los valores que faltan parten de setLrfhssDefaults()
        FB(LRFHSSConfig, enabled,     FF_KEEP, 0),
        FC(LRFHSSConfig, freqHz,      FF_REQUIRED | FF_KEEP, 0, 400000000, 480000000),
        FC(LRFHSSConfig, bwIdx,       FF_KEEP, 0, 0, 9),
        FC(LRFHSSConfig, crIdx,       FF_KEEP, 0, 0, 3),
        FB(LRFHSSConfig, narrowGrid,  FF_KEEP, 0),
        FC(LRFHSSConfig, powerDbm,    FF_KEEP, 0, -9, 22),
        FC(LRFHSSConfig, intervalMin, FF_KEEP, 0, 2, 60),
        FS(LRFHSSConfig, locator,     0, ""),
        FB(LRFHSSConfig, locatorLen,  FF_KEEP, 0),                                  // 4, 6 u 8: se comprueba al leer
    };
    const Field kLoraFields[] = {
        FB(LoraType, frequency,       0, 433775000),
        FB(LoraType, spreadingFactor, 0, 12),
        FB(LoraType, signalBandwidth, 0, 125000),
        FB(LoraType, codingRate4,     0, 5),
        FB(LoraType, power,           0, 20),
        FB(LoraType, ais,             0, false),
        FB(LoraType, fsk,             0, false),
        FB(LoraType, fskBitRate,      0, 9600),                                     // decimales en milesimas
        FB(LoraType, fskFreqDev,      0, 3000),
        FB(LoraType, fskRxBandwidth,  0, 39000),
        FR(LoraType, iaruRegion,      0, 1, 1, 3),
        FB(LoraType, fskFrequency,    FF_REQUIRED, 0),
        FB(LoraType, fskPower,        FF_REQUIRED, 0),
        FC(LoraType, fskShaping,      0, 1, 0, 4),
        FC(LoraType, fskFec,          0, 0, 0, 4),
        FB(LoraType, fskIaruRegion,   FF_NOREAD, 1),                                // por defecto la region del perfil LoRa: la lee el codigo
    };
    const Field kBatteryFields[] = {
        FB(Battery, sendVoltage,        FF_REQUIRED, false),
        FB(Battery, voltageAsTelemetry, FF_REQUIRED, false),
        FB(Battery, sendVoltageAlways,  FF_REQUIRED, false),
        FB(Battery, monitorVoltage,     FF_REQUIRED, false),
        FB(Battery, sleepVoltage,       FF_REQUIRED, 2900),
    };
    const Field kTelemetryFields[] = {
        FB(Telemetry, active,                FF_REQUIRED, false),
        FB(Telemetry, sendTelemetry,         FF_REQUIRED, false),
        FB(Telemetry, temperatureCorrection, FF_REQUIRED, 0),
    };
    const Field kWinlinkFields[] = {
        FS(Winlink, password, FF_REQUIRED, "NOPASS"),
    };
    const Field kPttFields[] = {
        FB(PTT, active,    FF_REQUIRED, false),
        FB(PTT, reverse,   FF_REQUIRED, false),
        FB(PTT, preDelay,  FF_REQUIRED, 0),
        FB(PTT, postDelay, FF_REQUIRED, 0),
        FB(PTT, io_pin,    FF_REQUIRED, 4),
    };
    const Field kNotificationFields[] = {
        FB(Notification, ledTx,            FF_REQUIRED,            false),
        FB(Notification, ledTxPin,         FF_REQUIRED | FF_PIN,   NOTIF_DEF_LED_TX),
        FB(Notification, ledMessage,       FF_REQUIRED,            false),
        FB(Notification, ledMessagePin,    FF_REQUIRED | FF_PIN,   NOTIF_DEF_LED_MESSAGE),
        FB(Notification, ledRxBeacon,      0,                      true),
        FB(Notification, flashEmergency,   0,                      false),
        FB(Notification, ledTxCw,          0,                      false),
        FB(Notification, ledRxCw,          0,                      true),
        FB(Notification, flashCw,          0,                      false),
        FB(Notification, buzzerActive,     FF_REQUIRED,            false),
        FB(Notification, buzzerPinTone,    FF_REQUIRED | FF_PIN,   NOTIF_DEF_BUZZER_TONE),
        FB(Notification, buzzerPinVcc,     FF_REQUIRED | FF_PIN,   NOTIF_DEF_BUZZER_VCC),
        FB(Notification, bootUpBeep,       FF_REQUIRED,            false),
        FB(Notification, txBeep,           FF_REQUIRED,            false),
        FB(Notification, messageRxBeep,    FF_REQUIRED,            false),
        FB(Notification, stationBeep,      FF_REQUIRED,            false),
        FB(Notification, lowBatteryBeep,   FF_REQUIRED,            false),
        FB(Notification, shutDownBeep,     FF_REQUIRED,            false),
        FB(Notification, bellRings,        FF_REQUIRED,            true),
        FB(Notification, ledFlashlight,    FF_REQUIRED,            false),
        FB(Notification, ledFlashlightPin, FF_REQUIRED | FF_PIN,   NOTIF_DEF_FLASHLIGHT),
    };
}

extern logging::Logger logger;

bool Configuration::writeFile() {

    Serial.println("Saving config..");

    JsonDocument data;
    // Se escribe en un fichero temporal y solo al terminar se sustituye el definitivo: un corte o fallo a mitad de guardado
    // ya no deja la configuracion vacia (causa de perder todos los perfiles).
    File configFile = SPIFFS.open("/tracker_conf.tmp", "w");

    if (!configFile) {
        Serial.println("Error: Could not open config file for writing");
        return false;
    }
    try {
        writeFields(data["wifiAP"].to<JsonObject>(),       &wifiAP,       kWifiApFields,       NFIELDS(kWifiApFields));
        writeFields(data["fwUpdate"].to<JsonObject>(),     &fwUpdate,     kFwUpdateFields,     NFIELDS(kFwUpdateFields));
        writeFields(data["kissNet"].to<JsonObject>(),      &kissNet,      kKissNetFields,      NFIELDS(kKissNetFields));
        writeFields(data["nmeaUart"].to<JsonObject>(),     &nmeaUart,     kNmeaUartFields,     NFIELDS(kNmeaUartFields));
        writeFields(data["iaruAuto"].to<JsonObject>(),     &iaruAuto,     kIaruAutoFields,     NFIELDS(kIaruAutoFields));
        writeFields(data["cat"].to<JsonObject>(),          &cat,          kCatFields,          NFIELDS(kCatFields));
        writeFields(data["team"].to<JsonObject>(),         &team,         kTeamFields,         NFIELDS(kTeamFields));

        for (int i = 0; i < beacons.size(); i++) {
            beacons[i].callsign.trim();
            beacons[i].callsign.toUpperCase();
            writeFields(data["beacons"][i].to<JsonObject>(), &beacons[i], kBeaconFields, NFIELDS(kBeaconFields));
        }

        writeFields(data["display"].to<JsonObject>(),      &display,      kDisplayFields,      NFIELDS(kDisplayFields));
        writeFields(data["bluetooth"].to<JsonObject>(),    &bluetooth,    kBluetoothFields,    NFIELDS(kBluetoothFields));
        #ifndef HAS_BT_CLASSIC
            data["bluetooth"]["useBLE"]             = true; // fixed as BLE
        #endif

        data["operatingMode"]                       = savedMode;
        data["radioPpm1000"]                        = radioPpm1000;
        writeFields(data["cw"].to<JsonObject>(),           &cw,           kCwFields,           NFIELDS(kCwFields));
        for (int i = 0; i < CW_MESSAGE_COUNT; i++) data["cw"]["keypadMv"][i] = cw.keypadMv[i];
        for (int i = 0; i < CW_MESSAGE_COUNT; i++) data["cw"]["messages"][i] = cw.messages[i];
        writeFields(data["gps"].to<JsonObject>(),          &gps,          kGpsFields,          NFIELDS(kGpsFields));
        writeFields(data["wspr"].to<JsonObject>(),         &wspr,         kWsprFields,         NFIELDS(kWsprFields));
        for (int i = 0; i < 7; i++) {
            data["wspr"]["ppCorrHz"][i]             = wspr.ppCorrHz[i];
            data["wspr"]["ppDriftTenths"][i]        = wspr.ppDriftTenths[i];
        }
        writeFields(data["dapnet"].to<JsonObject>(),       &dapnet,       kDapnetFields,       NFIELDS(kDapnetFields));
        #ifdef HAS_SA868
        writeFields(data["sa868"].to<JsonObject>(),        &sa868,        kSa868Fields,        NFIELDS(kSa868Fields));
        writeFields(data["fm"].to<JsonObject>(),           &fm,           kFmFields,           NFIELDS(kFmFields));
        writeFields(data["fm"]["vfo"].to<JsonObject>(),    &fm.vfo,       kFmMemFields,        NFIELDS(kFmMemFields));
        for (int i = 0; i < FmConfig::MEMORIES; i++) {                              // solo las memorias ocupadas (el indice es la posicion)
            if (fm.mem[i].rxHz <= 0) continue;
            JsonObject m = data["fm"]["mem"].add<JsonObject>();
            m["i"] = i;
            writeFields(m, &fm.mem[i], kFmMemFields, NFIELDS(kFmMemFields));
        }
        #endif
        writeFields(data["bcast"].to<JsonObject>(),        &bcast,        kBcastFields,        NFIELDS(kBcastFields));
        writeFields(data["ais"].to<JsonObject>(),          &ais,          kAisFields,          NFIELDS(kAisFields));
        for (int i = 0; i < BcastConfig::PRESETS; i++) data["bcast"]["presets"][i] = bcast.presets[i];
        writeFields(data["sat"].to<JsonObject>(),          &sat,          kSatFields,          NFIELDS(kSatFields));
        writeFields(data["rotor"].to<JsonObject>(),        &rotor,        kRotorFields,        NFIELDS(kRotorFields));
        for (int i = 0; i < RotorConfig::TARGETS; i++) {
            if (rotor.targets[i].name.isEmpty()) continue;
            writeFields(data["rotor"]["targets"][i].to<JsonObject>(), &rotor.targets[i], kRotorTargetFields, NFIELDS(kRotorTargetFields));
        }
        writeFields(data["ft"].to<JsonObject>(),           &ft,           kFtFields,           NFIELDS(kFtFields));
        writeFields(data["lrfhss"].to<JsonObject>(),       &lrfhss,       kLrfhssFields,       NFIELDS(kLrfhssFields));

        for (int i = 0; i < loraTypes.size(); i++) {
            writeFields(data["lora"][i].to<JsonObject>(), &loraTypes[i], kLoraFields, NFIELDS(kLoraFields));
        }

        writeFields(data["battery"].to<JsonObject>(),      &battery,      kBatteryFields,      NFIELDS(kBatteryFields));
        writeFields(data["telemetry"].to<JsonObject>(),    &telemetry,    kTelemetryFields,    NFIELDS(kTelemetryFields));
        writeFields(data["winlink"].to<JsonObject>(),      &winlink,      kWinlinkFields,      NFIELDS(kWinlinkFields));
        writeFields(data["notification"].to<JsonObject>(), &notification, kNotificationFields, NFIELDS(kNotificationFields));
        writeFields(data["pttTrigger"].to<JsonObject>(),   &ptt,          kPttFields,          NFIELDS(kPttFields));

        data["other"]["simplifiedTrackerMode"]      = simplifiedTrackerMode;
        data["other"]["sendCommentAfterXBeacons"]   = sendCommentAfterXBeacons;
        data["other"]["path"]                       = path;
        data["other"]["nonSmartBeaconRate"]         = nonSmartBeaconRate;
        data["other"]["rememberStationTime"]        = rememberStationTime;
        data["other"]["standingUpdateTime"]         = standingUpdateTime;
        data["other"]["sendAltitude"]               = sendAltitude;
        data["other"]["disableGPS"]                 = disableGPS;
        data["other"]["cadActive"]                  = cadActive;
        data["other"]["cadWaitSec"]                 = cadWaitSec;
        data["other"]["cadFskRssi"]                 = cadFskRssi;
        data["other"]["email"]                      = email;

        const size_t written = serializeJson(data, configFile);
        configFile.close();
        if (written == 0) {
            Serial.println("Error: config not written, keeping the previous one");
            SPIFFS.remove("/tracker_conf.tmp");
            return false;
        }
        SPIFFS.remove("/tracker_conf.json");
        if (!SPIFFS.rename("/tracker_conf.tmp", "/tracker_conf.json")) {
            Serial.println("Error: could not replace the config file");
            return false;
        }
        return true;
    } catch (...) {
        Serial.println("Error: Exception occurred while saving config");
        configFile.close();
        SPIFFS.remove("/tracker_conf.tmp");
        return false;
    }
}

bool Configuration::readFile() {
    Serial.println("Reading config..");
    File configFile = SPIFFS.open("/tracker_conf.json", "r");

    if (configFile) {
        bool needsRewrite = false;
        JsonDocument data;
        DeserializationError error = deserializeJson(data, configFile);
        if (error) {
            // fichero vacio o danado (p. ej. corte durante un guardado): configuracion por defecto COMPLETA (3 balizas + 4 perfiles LoRa + AIS),
            // no un JSON vacio, que dejaba la WebUI sin perfiles
            Serial.println("Failed to read file, using default configuration");
            configFile.close();
            setDefaultValues();
            return writeFile();
        }

        #define READ(section, obj, table)   if (readFields(data[section], &(obj), table, NFIELDS(table))) needsRewrite = true
        READ("wifiAP",   wifiAP,   kWifiApFields);
        READ("fwUpdate", fwUpdate, kFwUpdateFields);
        READ("kissNet",  kissNet,  kKissNetFields);
        READ("nmeaUart", nmeaUart, kNmeaUartFields);
        if (nmeaUart.baud != 4800 && nmeaUart.baud != 9600 && nmeaUart.baud != 38400) nmeaUart.baud = NMEA_UART_BAUD_DEFAULT;
        READ("iaruAuto", iaruAuto, kIaruAutoFields);
        READ("cat",      cat,      kCatFields);
        READ("team",     team,     kTeamFields);
        if (team.pollSec != 0) team.pollSec = constrain(team.pollSec, 60, 3600);

        JsonArray BeaconsArray = data["beacons"];
        for (int i = 0; i < BeaconsArray.size(); i++) {
            Beacon bcn;
            readFields(BeaconsArray[i], &bcn, kBeaconFields, NFIELDS(kBeaconFields));
            bcn.callsign.toUpperCase();
            if (bcn.aisProfile) enforceAisBeacon(bcn);
            beacons.push_back(bcn);
        }

        READ("display",  display,  kDisplayFields);

        READ("bluetooth", bluetooth, kBluetoothFields);
        #ifndef HAS_BT_CLASSIC
            bluetooth.useBLE = true;    // fixed as BLE
        #endif
        if (data["operatingMode"].isNull()) needsRewrite = true;
        if (data["radioPpm1000"].isNull()) needsRewrite = true;
        if (!data["radioPpm1000"].isNull()) radioPpm1000 = constrain((int)(data["radioPpm1000"] | 0), -50000, 50000);
        else                                radioPpm1000 = constrain((int)(data["radioPpm100"] | 0) * 10, -50000, 50000);          // configuraciones de 5.2.11 (centesimas de ppm)
        savedMode = (int)(data["operatingMode"] | (int)MODE_APRS_LORA);
        if (!isAprsMode(savedMode)) savedMode = MODE_APRS_LORA;                                       // MORSE nunca arranca solo; M17 solo en la T-TWR
        #ifndef HAS_SA868
        if (savedMode == MODE_M17) savedMode = MODE_APRS_LORA;
        #endif
        operatingMode = savedMode;

        if (data["cw"]["messages"].isNull()) needsRewrite = true;
        setCwDefaults();
        READ("cw", cw, kCwFields);
        for (int i = 0; i < CW_MESSAGE_COUNT; i++) cw.keypadMv[i] = data["cw"]["keypadMv"][i] | cw.keypadMv[i];
        if (cw.squelchDbm > -40) cw.squelchDbm = 0;
        for (int i = 0; i < CW_MESSAGE_COUNT; i++) {
            if (!data["cw"]["messages"][i].isNull()) cw.messages[i] = data["cw"]["messages"][i].as<String>();
        }

        setGpsDefaults();
        const int factoryMask = gps.gnssMask;
        READ("gps", gps, kGpsFields);
        {
            const int mask = gps.gnssMask;
            const bool okMask = (mask > 0 && mask < 64) && !(((mask & 16) || (mask & 32)) && !(mask & 1)) && !((mask & 4) && (mask & 8)) && (mask & 15) != 0;
            if (!okMask) gps.gnssMask = factoryMask;               // combinacion no valida en el M10: se conserva la de fabrica
        }
        if (!(gps.dynModel == 0 || (gps.dynModel >= 2 && gps.dynModel <= 9))) gps.dynModel = 0;
        gps.cnMask         &= 13;                                       // solo GPS (1), BeiDou (4) y GLONASS (8)
        if (gps.cnMask == 0) gps.cnMask = 5;                            // GPS + BeiDou: valor de fabrica del L76K

        setWsprDefaults();
        READ("wspr", wspr, kWsprFields);
        for (int i = 0; i < 7; i++) {                                       // sin tabla guardada: cada potencia parte de los valores generales
            wspr.ppCorrHz[i]       = constrain((int)(data["wspr"]["ppCorrHz"][i] | wspr.corrHz), -5000, 5000);
            wspr.ppDriftTenths[i]  = constrain((int)(data["wspr"]["ppDriftTenths"][i] | wspr.driftTenths), -200, 200);
        }

        setDapnetDefaults();
        READ("dapnet", dapnet, kDapnetFields);
        if (dapnet.baud != 512 && dapnet.baud != 1200 && dapnet.baud != 2400) dapnet.baud = 1200;
        if (dapnet.rxBwTenthsKHz != 117 && dapnet.rxBwTenthsKHz != 146 && dapnet.rxBwTenthsKHz != 195 && dapnet.rxBwTenthsKHz != 234 && dapnet.rxBwTenthsKHz != 293) dapnet.rxBwTenthsKHz = 146;

        #ifdef HAS_SA868
        setSa868Defaults();
        READ("sa868", sa868, kSa868Fields);
        if (sa868.revision != 0 && sa868.revision != 20 && sa868.revision != 21) sa868.revision = 0;
        setFmDefaults();
        READ("fm", fm, kFmFields);
        readFields(data["fm"]["vfo"], &fm.vfo, kFmMemFields, NFIELDS(kFmMemFields));
        JsonArray fmMems = data["fm"]["mem"];
        for (JsonObject m : fmMems) {
            const int i = m["i"] | -1;
            if (i >= 0 && i < FmConfig::MEMORIES) readFields(m, &fm.mem[i], kFmMemFields, NFIELDS(kFmMemFields));
        }
        if (fm.artsInterval != 15) fm.artsInterval = 25;
        #endif

        setBcastDefaults();
        READ("bcast", bcast, kBcastFields);
        for (int i = 0; i < BcastConfig::PRESETS; i++) {
            const int f = data["bcast"]["presets"][i] | 0;
            bcast.presets[i] = (f >= 153 && f <= 108000) ? f : 0;
        }
        setAisDefaults();
        READ("ais", ais, kAisFields);
        READ("sat", sat, kSatFields);
        if (sat.txText.length() > 64) sat.txText = sat.txText.substring(0, 64);
        if (sat.favorites.length() > 260) sat.favorites = sat.favorites.substring(0, 260);
        if (sat.aprsPath.length() > 24) sat.aprsPath = sat.aprsPath.substring(0, 24);

        READ("rotor", rotor, kRotorFields);
        if (!RotorFollow::validSource(rotor.source)) rotor.source = 0;
        rotor.followFilter = data["rotor"]["followFilter"] | rotor.followFilter;
        for (int i = 0; i < RotorConfig::TARGETS; i++) readFields(data["rotor"]["targets"][i], &rotor.targets[i], kRotorTargetFields, NFIELDS(kRotorTargetFields));

        setFtDefaults();
        READ("ft", ft, kFtFields);
        if (ft.ft4FreqHz != 0) ft.ft4FreqHz = constrain(ft.ft4FreqHz, 400000000, 480000000);
        if (ft.fstFreqHz != 0) ft.fstFreqHz = constrain(ft.fstFreqHz, 400000000, 480000000);
        ft.fstSeconds = (ft.fstSeconds == 300) ? 300 : 120;

        setLrfhssDefaults();
        READ("lrfhss", lrfhss, kLrfhssFields);
        if (lrfhss.locatorLen != 4 && lrfhss.locatorLen != 8) lrfhss.locatorLen = 6;

        JsonArray LoraTypesArray = data["lora"];
        for (int j = 0; j < LoraTypesArray.size(); j++) {
            LoraType loraType;

            if (readFields(LoraTypesArray[j], &loraType, kLoraFields, NFIELDS(kLoraFields))) needsRewrite = true;
            loraType.fskIaruRegion      = LoraTypesArray[j]["fskIaruRegion"] | loraType.iaruRegion;
            if (loraType.fskIaruRegion < 1 || loraType.fskIaruRegion > 3) loraType.fskIaruRegion = 1;
            if (loraType.fskFrequency <= 0) loraType.fskFrequency = loraType.frequency;     // configuraciones anteriores: el FSK parte del perfil LoRa
            if (loraType.fskPower <= 0)     loraType.fskPower     = loraType.power;
            if (loraType.ais) loraType.fsk = false;     // el perfil AIS es siempre LoRa
            if (loraType.ais) {                     // el perfil LoRa del modo nautico AIS es siempre SF9 y CR 4/5
                loraType.spreadingFactor = 9;
                loraType.codingRate4     = 5;
            }
            loraTypes.push_back(loraType);
        }
        {   // reparacion: debe haber 3 perfiles de baliza y 4 perfiles LoRa regionales delante de los perfiles AIS
            int baseBeacons = 0, baseLora = 0;
            for (const Beacon &b : beacons)  if (!b.aisProfile) baseBeacons++;
            for (const LoraType &l : loraTypes) if (!l.ais) baseLora++;
            while (baseBeacons < 3) {
                Beacon bcn;
                bcn.callsign = "NOCALL-7"; bcn.symbol = "["; bcn.overlay = "/"; bcn.micE = ""; bcn.comment = ""; bcn.status = "";
                bcn.smartBeaconActive = true; bcn.smartBeaconSetting = 0; bcn.gpsEcoMode = false; bcn.profileLabel = "";
                beacons.insert(beacons.begin() + baseBeacons, bcn);
                baseBeacons++;
                needsRewrite = true;
            }
            while (baseLora < 4) {
                LoraType lt;
                switch (baseLora) {
                    case 0: lt.frequency = 433775000; lt.spreadingFactor = 12; lt.codingRate4 = 5; break;
                    case 1: lt.frequency = 434855000; lt.spreadingFactor = 9;  lt.codingRate4 = 7; break;
                    case 2: lt.frequency = 439912500; lt.spreadingFactor = 12; lt.codingRate4 = 5; break;
                    default: lt.frequency = 915000000; lt.spreadingFactor = 12; lt.codingRate4 = 5; break;
                }
                lt.signalBandwidth = 125000; lt.power = 20;
                loraTypes.insert(loraTypes.begin() + baseLora, lt);
                baseLora++;
                needsRewrite = true;
            }
        }
        if (ensureAisProfiles()) needsRewrite = true;       // configuraciones anteriores: se anade el perfil nautico AIS
        if (cw.beaconFreq <= 0 || cw.keyFreq <= 0 || cw.beaconPower <= 0 || cw.keyPower <= 0) {      // sin perfiles CW guardados: parten del perfil LoRa 1
            const long baseFreq = loraTypes.empty() ? 433775000L : loraTypes[0].frequency;
            const int  basePow  = loraTypes.empty() ? 17 : loraTypes[0].power;
            (void)baseFreq;                                  // CW parte del segmento de telegrafia IARU (432,050 MHz), no de la frecuencia LoRa
            if (cw.beaconFreq  <= 0) cw.beaconFreq  = 432050000L;
            if (cw.keyFreq     <= 0) cw.keyFreq     = 432050000L;
            if (cw.beaconPower <= 0) cw.beaconPower = basePow;
            if (cw.keyPower    <= 0) cw.keyPower    = basePow;
            needsRewrite = true;
        }
        if (cw.beaconFreq == 433775000L) { cw.beaconFreq = 432050000L; needsRewrite = true; }       // 5.2.11 inicial copiaba la frecuencia LoRa: CW pasa al segmento de telegrafia
        if (cw.keyFreq    == 433775000L) { cw.keyFreq    = 432050000L; needsRewrite = true; }
        wspr.freqHz = 432301500;                            // la frecuencia WSPR es fija

        READ("battery",   battery,   kBatteryFields);
        READ("telemetry", telemetry, kTelemetryFields);
        READ("winlink",   winlink,   kWinlinkFields);
        READ("notification", notification, kNotificationFields);
        READ("pttTrigger", ptt,      kPttFields);
        #undef READ

        if (data["other"]["simplifiedTrackerMode"].isNull() ||
            data["other"]["sendCommentAfterXBeacons"].isNull() ||
            data["other"]["path"].isNull() ||
            data["other"]["nonSmartBeaconRate"].isNull() ||
            data["other"]["rememberStationTime"].isNull() ||
            data["other"]["standingUpdateTime"].isNull() ||
            data["other"]["sendAltitude"].isNull() ||
            data["other"]["disableGPS"].isNull() ||
            data["other"]["cadActive"].isNull() ||
            data["other"]["email"].isNull()) needsRewrite = true;
        simplifiedTrackerMode           = data["other"]["simplifiedTrackerMode"] | false;
        sendCommentAfterXBeacons        = data["other"]["sendCommentAfterXBeacons"] | 10;
        path                            = data["other"]["path"] | "WIDE1-1";
        nonSmartBeaconRate              = data["other"]["nonSmartBeaconRate"] | 15;
        rememberStationTime             = data["other"]["rememberStationTime"] | 30;
        standingUpdateTime              = data["other"]["standingUpdateTime"] | 15;
        sendAltitude                    = data["other"]["sendAltitude"] | true;
        disableGPS                      = data["other"]["disableGPS"] | false;
        cadActive                       = data["other"]["cadActive"] | true;
        cadWaitSec                      = constrain((int)(data["other"]["cadWaitSec"] | 10), 1, 60);
        cadFskRssi                      = constrain((int)(data["other"]["cadFskRssi"] | -95), -120, -60);
        email                           = data["other"]["email"] | "";

        configFile.close();

        if (needsRewrite) {
            // La configuracion en memoria ya esta completa (valores por defecto en lo que faltaba): se guarda y se sigue.
            // No se reinicia: este codigo corre en el constructor global de Config, antes de que arranque FreeRTOS, y
            // ESP.restart() ahi aborta (xTaskGetIdleTaskHandleForCore) y, si algo vuelve a faltar, deja la placa en bucle.
            Serial.println("Config JSON incomplete, rewriting...");
            writeFile();
        }
        Serial.println("Config read successfuly");
        return true;
    } else {
        Serial.println("Config file not found");
        return false;
    }
}

void Configuration::enforceAisBeacon(Beacon& beacon) {
    const AIS_Vessel::VesselType& vessel = AIS_Vessel::vesselType(beacon.aisVesselType);
    beacon.aisVesselType        = vessel.id;
    char forced[24];
    if (AIS_Vessel::forceCallsign(beacon.callsign.c_str(), vessel.ssid, forced, sizeof(forced)) > 0) beacon.callsign = forced;   // el SSID no se puede cambiar
    beacon.symbol               = String(vessel.symbol);
    beacon.overlay              = String(vessel.overlay);
    beacon.micE                 = "";                                   // el perfil nautico usa siempre posicion comprimida (Base91)
    beacon.smartBeaconActive    = true;
    beacon.smartBeaconSetting   = AIS_Vessel::smartBeaconSettingFor(vessel.aisClass);
    char name[21];
    AIS_Vessel::sanitizeName(beacon.aisName.c_str(), name, sizeof(name));
    beacon.aisName              = name;
    if (!AIS_Vessel::validMmsi(beacon.aisMmsi)) beacon.aisMmsi = 0;
    beacon.aisLength = AIS_Vessel::clampLength(vessel.aisClass, beacon.aisLength);   // clase B (recreo): 6 - 24 m
    if (beacon.aisBeam > 126)   beacon.aisBeam   = 126;
    if (!(beacon.aisDraft >= 0.0f)) beacon.aisDraft = 0.0f;
    if (beacon.aisDraft > 25.5f)    beacon.aisDraft = 25.5f;
}

int Configuration::aisLoraIndex() const {
    for (size_t i = 0; i < loraTypes.size(); i++) {
        if (loraTypes[i].ais) return (int) i;
    }
    return -1;
}

int Configuration::aisBeaconIndex() const {
    for (size_t i = 0; i < beacons.size(); i++) {
        if (beacons[i].aisProfile) return (int) i;
    }
    return -1;
}

bool Configuration::ensureAisProfiles() {
    bool changed = false;
    if (aisBeaconIndex() < 0) {
        Beacon beacon;
        beacon.callsign             = "NOCALL-5";
        beacon.symbol               = "Y";
        beacon.overlay              = "/";
        beacon.micE                 = "";
        beacon.comment              = "";
        beacon.status               = "";
        beacon.smartBeaconActive    = true;
        beacon.smartBeaconSetting   = 5;
        beacon.gpsEcoMode           = false;
        beacon.profileLabel         = "AIS";
        beacon.aisProfile           = true;
        beacon.aisVesselType        = 0;
        enforceAisBeacon(beacon);
        beacons.push_back(beacon);
        changed = true;
    }
    int index = aisLoraIndex();
    if (index < 0) {
        LoraType loraType;
        loraType.frequency          = 433775000;            // la frecuencia se puede cambiar en la WebUI; SF y CR no
        loraType.spreadingFactor    = 9;
        loraType.signalBandwidth    = 125000;
        loraType.codingRate4        = 5;
        loraType.power              = 20;
        loraType.ais                = true;
        loraTypes.push_back(loraType);
        changed = true;
    } else if (index != (int) loraTypes.size() - 1) {       // el perfil AIS va siempre el ultimo (los demas rotan con el menu)
        LoraType aisType = loraTypes[index];
        loraTypes.erase(loraTypes.begin() + index);
        loraTypes.push_back(aisType);
        changed = true;
    }
    return changed;
}

void Configuration::setSa868Defaults() {
    sa868.rxFreqHz   = 144800000;               // APRS en VHF, Region 1 de la IARU
    sa868.txFreqHz   = 144800000;
    sa868.highPower  = false;
    sa868.wide       = false;                   // APRS: siempre estrecho (12,5 kHz) salvo que se elija ancho (Manuel, 2026-11-09)
    sa868.volume     = 6;
    sa868.squelch    = 1;
    sa868.txLevel    = 25;
    sa868.txLevel9600 = 40;
    sa868.fec        = 0;
    sa868.txDelayMs  = 300;
    sa868.txTailMs   = 30;
    sa868.filters    = true;
    sa868.speaker    = true;
    sa868.revision   = 0;
    sa868.iaruRegion = 1;
    sa868.m17FreqHz  = 0;                       // la sugerida para la banda del modulo
    sa868.m17Can     = 0;
    sa868.txLevelM17 = 40;
    sa868.dapnetFreqHz = 0;                     // 144,8625 (VHF) / 439,9875 MHz (UHF)
    sa868.dapnetHpfHz = 320;                    // 100 nF con 10 k / 10 k a la entrada del ADC
    sa868.cwMcw      = false;                   // A1A
    sa868.cwToneHz   = 800;
}

void Configuration::setAisDefaults() {
    ais.module        = false;
    ais.rxPin         = -1;
    ais.baud          = 38400;
    ais.channel       = 0;
    ais.altSec        = 30;
    ais.cpaAlarm      = true;
    ais.cpaTenthNm    = 5;
    ais.tcpaMin       = 15;
    ais.guardTenthNm  = 0;
    ais.distressAlarm = true;
    ais.maxAgeMin     = 10;
    ais.forwardNmea   = true;
    ais.hpfHz         = 320;
}

void Configuration::setBcastDefaults() {
    bcast.enabled  = false;
    bcast.resetPin = -1;
    bcast.band     = 0;
    bcast.fmKHz    = 98000;
    bcast.mwKHz    = 999;
    bcast.swKHz    = 9500;
    bcast.lwKHz    = 198;
    bcast.volume   = 8;
    bcast.mono     = false;
    bcast.deemph75 = false;
    bcast.fmWide   = false;
    bcast.mw10k    = false;
    for (int i = 0; i < BcastConfig::PRESETS; i++) bcast.presets[i] = 0;
}

void Configuration::setFmDefaults() {
    auto clearMem = [](FmMemory& m) {
        m.name = ""; m.rxHz = 0; m.shift = 0; m.offsetHz = 600000; m.toneMode = 0; m.tone = 885; m.dcs = 23;
        m.dcsInv = false; m.wide = false; m.high = false;
    };
    clearMem(fm.vfo);
    fm.vfo.rxHz      = 145500000;               // llamada simplex de 2 m en la Region 1 (se ajusta a la banda del modulo)
    for (int i = 0; i < FmConfig::MEMORIES; i++) clearMem(fm.mem[i]);
    fm.stepIndex     = 3;                       // 12,5 kHz
    fm.memMode       = false;
    fm.memIndex      = 0;
    fm.scanResume    = 0;
    fm.scanMemories  = true;
    fm.scanLoHz      = 0;                       // 0 = toda la banda del modulo
    fm.scanHiHz      = 0;
    fm.artsInterval  = 25;
    fm.artsDcs       = 23;
    fm.cwId          = true;
    fm.cwWpm         = 20;
    fm.eaiMode       = 1;
    fm.eaiMinutes    = 10;
    fm.manDownMin    = 0;
    fm.totSec        = 180;
    fm.aprsBeacon    = true;
    fm.qsyInBeacon   = true;
    fm.squelchAuto   = true;
    fm.nrLevel       = 0;
    fm.satOn         = false;
    fm.satTx         = 0;
}

void Configuration::setDapnetDefaults() {
    dapnet.freqHz        = 439987500;           // frecuencia de DAPNET en la mayor parte de Europa (hampager.de, «Common frequencies for DAPNET»)
    dapnet.baud          = 1200;
    dapnet.devTenthsKHz  = 45;                  // POCSAG: +-4,5 kHz (valor estandar; a confirmar con la documentacion de DAPNET)
    dapnet.rxBwTenthsKHz = 146;                 // 14,6 kHz: cabe la senal (2 x 4,5 + 1,2 kHz) con margen para el error del oscilador
    dapnet.rics          = "";
    dapnet.monitorAll    = false;
    dapnet.timeSync      = false;
    sat.noradId          = 0;
    sat.minEl            = 10;
    sat.category         = 0;
    sat.posSource        = 0;
    sat.lat              = 0.0;
    sat.lon              = 0.0;
    sat.txEnabled        = false;
    sat.txMinEl          = 15;
    sat.txPower          = 10;
    sat.txMinIntervalS   = 30;
    sat.txText           = "";
    sat.favorites        = "";
    sat.groups           = "";
    sat.aprsPath         = "ARISS";
    sat.lnaPin = sat.paPin = sat.pttPin = -1;
    sat.lnaRev = sat.paRev = sat.pttRev = false;
    sat.seqLeadMs        = 20;
    sat.seqPreMs         = 50;
    sat.seqPostMs        = 20;
    sat.seqTailMs        = 50;
    rotor.enabled        = false;
    rotor.transport      = 0;
    rotor.host           = "";
    rotor.port           = 4533;                // puerto por defecto de rotctld
    rotor.btName         = "";
    rotor.stepTenths     = 10;
    rotor.preSec         = 60;
    rotor.azOffset       = 0;
    rotor.elOffset       = 0;
    rotor.park           = false;
    rotor.parkAz         = 0;
    rotor.parkEl         = 0;
    rotor.bleEvents      = true;
    rotor.serverEnabled  = false;
    rotor.serverPort     = 4533;
    rotor.source         = 0;
    rotor.followFilter   = "";
    rotor.followMaxMin   = 30;
    rotor.predict        = true;
    rotor.predictMaxS    = 300;
    rotor.mount          = 0;
    rotor.tilt           = 0;
    rotor.targetIndex    = 0;
    for (int i = 0; i < RotorConfig::TARGETS; i++) { rotor.targets[i].name = ""; rotor.targets[i].lat = 0; rotor.targets[i].lon = 0; rotor.targets[i].alt = 0; rotor.targets[i].freqHz = 0; }
    dapnet.charset       = 1;                   // la red es alemana: ISO 646 aleman por defecto (los demas caracteres son ASCII)
}

void Configuration::setFtDefaults() {
    ft.ft8FreqHz   = 432174000;             // dial FT8 en 70 cm segun la lista de frecuencias de WSJT-X (valido en las tres regiones IARU)
    ft.ft4FreqHz   = 0;                     // ni WSJT-X ni los planes IARU fijan un canal de FT4 en 70 cm: se rellena a mano
    ft.ft8Region   = 1;
    ft.ft4Region   = 1;
    ft.fstFreqHz   = 0;                     // FST4W: tampoco hay canal definido en 70 cm
    ft.fstRegion   = 1;
    ft.fstSeconds  = 120;
    ft.fstEvery    = 4;                     // una emision cada 4 periodos (8 minutos con FST4W-120)
    ft.corrHz      = 0;
    ft.powerDbm    = 17;
    ft.everyCycles = 4;                     // una emision por minuto en FT8 (4 ciclos de 15 s)
    ft.locator     = "";
    ft.text        = "";
    ft.screenOff   = true;
}

void Configuration::setLrfhssDefaults() {
    lrfhss.enabled     = false;                 // desactivada por defecto: puede requerir autorizacion especial (Orden IET/1311/2013, art. 17.2)
    lrfhss.freqHz      = 434000000;             // IARU R1 (v10.02): centro de experimentos digitales, nota (p): datos tipo LoRa hasta 125 kHz
    lrfhss.bwIdx       = 0;                     // 39,06 kHz: ocupa unos 433,98-434,02 MHz
    lrfhss.crIdx       = 3;                     // 1/3
    lrfhss.narrowGrid  = true;
    lrfhss.powerDbm    = 10;
    lrfhss.intervalMin = 10;
    lrfhss.locator     = "";
    lrfhss.locatorLen  = 6;
}

void Configuration::setGpsDefaults() {
    // Valores de fabrica del M10 en la Supreme (los GPS_USE_* de board_pinout.h, si existen, prevalecen)
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
    #ifndef GPS_STATIC_HOLD_CMS
        #define GPS_STATIC_HOLD_CMS 0
    #endif
    #ifndef GPS_STATIC_HOLD_DIST_M
        #define GPS_STATIC_HOLD_DIST_M 0
    #endif
    gps.gnssMask        = (GPS_USE_GPS ? 1 : 0) | (GPS_USE_GALILEO ? 2 : 0) | (GPS_USE_BEIDOU ? 4 : 0) | (GPS_USE_GLONASS ? 8 : 0) | (GPS_USE_QZSS ? 16 : 0) | (GPS_USE_SBAS ? 32 : 0);
    gps.assistNow       = GPS_USE_ASSISTNOW_AUTONOMOUS;
    gps.dynModel        = 0;                    // portatil (el del receptor)
    gps.rateMs          = 1000;
    gps.minElev         = 5;                    // el del receptor
    gps.staticHoldCms   = GPS_STATIC_HOLD_CMS;
    gps.staticHoldDistM = GPS_STATIC_HOLD_DIST_M;
    gps.sbasDiffCorr    = true;                 // valores del receptor
    gps.sbasRanging     = true;
    gps.sbasIntegrity   = false;
    gps.ppsEnabled      = true;                 // el M10 trae el pulso activado: 1 Hz, 100 ms, flanco de subida
    gps.ppsFreqHz       = 1;
    gps.ppsLenMs        = 100;
    gps.ppsRising       = true;
    gps.ppsCableDelayNs = 50;
    gps.ppsUserDelayNs  = 0;
    gps.cnMask          = 5;                    // L76K: GPS + BeiDou (valor de fabrica del modulo)
    gps.nmeaLean        = true;
}

void Configuration::setWsprDefaults() {
    wspr.freqHz = 432301500;
    wspr.corrHz = 0;
    wspr.useGlobalPpm = false;
    wspr.powerDbm = 10;
    wspr.intervalMin = 10;
    wspr.locator = "";
    wspr.dither = true;
    wspr.screenOff = false;
    wspr.driftTenths = 0;
    wspr.perPower = false;
    for (int i = 0; i < 7; i++) { wspr.ppCorrHz[i] = 0; wspr.ppDriftTenths[i] = 0; }
}

void Configuration::setCwDefaults() {
    cw.wpm = 15;
    cw.keyMode = 0;
    cw.keyPin = -1;
    cw.dahPin = -1;
    cw.swapPaddles = false;
    cw.keypadPin = -1;
    for (int i = 0; i < CW_MESSAGE_COUNT; i++) cw.keypadMv[i] = 0;
    cw.keyTx = true;
    cw.sidetone = true;
    cw.trainerLevel = 4;
    cw.trainerGapWpm = 10;
    cw.squelchDbm    = 0;
    cw.beaconMin = 0;
    cw.beaconFreq = 432050000; cw.beaconPower = 17; cw.beaconRegion = 1;
    cw.keyFreq    = 432050000; cw.keyPower    = 17; cw.keyRegion    = 1;
    cw.beaconText = "DE {CALL}/B {LOC} V{VBAT}";
    cw.messages[0] = "CQ CQ DE {CALL} {CALL} K";
    cw.messages[1] = "{CALL} 73 SK";
    cw.messages[2] = "TEST DE {CALL}";
    cw.messages[3] = "VVV DE {CALL}";
    cw.messages[4] = "QRZ DE {CALL} K";
}

void Configuration::setDefaultValues() {
    wifiAP.active                   = true;
    wifiAP.password                 = "1234567890";

    fwUpdate.ssid                   = "";
    fwUpdate.password               = "";

    kissNet.enabled                 = false;
    kissNet.tcpPort                 = 8001;
    kissNet.udpPort                 = 0;
    kissNet.allowed                 = "";
    nmeaUart.txPin                  = NMEA_UART_TX_DEFAULT;
    nmeaUart.baud                   = NMEA_UART_BAUD_DEFAULT;
    iaruAuto.mode                   = 0;
    iaruAuto.confirm                = true;
    cat.enabled                     = false;
    cat.usb                         = false;
    cat.port                        = 4532;
    cat.allowed                     = "";
    cat.pttBeacon                   = false;
    cat.tuneKHz                     = 0;
    team.watchActive                = false;
    team.watchCalls                 = "";
    team.timeoutMin                 = 10;
    team.maxKm                      = 0;
    team.pollSec                    = 0;
    team.eaiActive                  = false;
    team.eaiCalls                   = "";
    team.eaiCode                    = "";
    team.eaiMinutes                 = 30;

    for (int i = 0; i < 3; i++) {
        Beacon beacon;
        beacon.callsign             = "NOCALL-7";
        beacon.symbol               = "[";
        beacon.overlay              = "/";
        beacon.micE                 = "";
        beacon.comment              = "";
        beacon.smartBeaconActive    = true;
        beacon.smartBeaconSetting   = 0;
        beacon.gpsEcoMode           = false;
        beacon.profileLabel         = "";
        beacon.status               = "";
        beacons.push_back(beacon);
    }

    display.ecoMode                 = false;
    display.timeout                 = 4;
    display.turn180                 = false;
    display.showSymbol              = true;

    setCwDefaults();
    setWsprDefaults();
    setLrfhssDefaults();
    setFtDefaults();
    setDapnetDefaults();
    setSa868Defaults();
    setFmDefaults();
    setBcastDefaults();
    setAisDefaults();
    setGpsDefaults();
    radioPpm1000                    = 0;
    operatingMode                   = MODE_APRS_LORA;
    savedMode                       = MODE_APRS_LORA;
    bluetooth.active                = false;
    bluetooth.deviceName            = "LoRaTracker";
    #ifdef HAS_BT_CLASSIC
        bluetooth.useBLE            = false;
        bluetooth.useKISS           = false;
    #else
        bluetooth.useBLE            = true;    // fixed as BLE
        bluetooth.useKISS           = true;
    #endif
    bluetooth.controlPin            = 123456;

    for (int j = 0; j < 4; j++) {
        LoraType loraType;
        switch (j) {
            case 0:
                loraType.frequency           = 433775000;
                loraType.spreadingFactor     = 12;
                loraType.codingRate4         = 5;
                break;
            case 1:
                loraType.frequency           = 434855000;
                loraType.spreadingFactor     = 9;
                loraType.codingRate4         = 7;
                break;
            case 2:
                loraType.frequency           = 439912500;
                loraType.spreadingFactor     = 12;
                loraType.codingRate4         = 5;
                break;
            case 3:
                loraType.frequency           = 915000000;
                loraType.spreadingFactor     = 12;
                loraType.codingRate4         = 5;
                break;
        }
        loraType.signalBandwidth    = 125000;
        loraType.power              = 20;
        loraTypes.push_back(loraType);
    }
    ensureAisProfiles();                                    // 4.o perfil de baliza (nautico AIS) y 5.o perfil LoRa (fijo SF9 / CR 4/5)

    battery.sendVoltage             = false;
    battery.voltageAsTelemetry      = false;
    battery.sendVoltageAlways       = false;
    battery.monitorVoltage          = false;
    battery.sleepVoltage            = 2.9;

    telemetry.active                 = false;
    telemetry.sendTelemetry          = false;
    telemetry.temperatureCorrection  = 0.0;

    winlink.password                = "NOPASS";

    notification.ledTx              = false;
    notification.ledTxPin           = defaultPin(NOTIF_DEF_LED_TX);
    notification.ledMessage         = false;
    notification.ledRxBeacon        = true;
    notification.flashEmergency     = false;
    notification.ledTxCw            = false;
    notification.ledRxCw            = true;
    notification.flashCw            = false;
    notification.ledMessagePin      = defaultPin(NOTIF_DEF_LED_MESSAGE);
    notification.buzzerActive       = false;
    notification.buzzerPinTone      = defaultPin(NOTIF_DEF_BUZZER_TONE);
    notification.buzzerPinVcc       = defaultPin(NOTIF_DEF_BUZZER_VCC);
    notification.bootUpBeep         = false;
    notification.txBeep             = false;
    notification.messageRxBeep      = false;
    notification.stationBeep        = false;
    notification.lowBatteryBeep     = false;
    notification.shutDownBeep       = false;
    notification.bellRings          = true;
    notification.ledFlashlight      = false;
    notification.ledFlashlightPin   = defaultPin(NOTIF_DEF_FLASHLIGHT);

    ptt.active                      = false;
    ptt.reverse                     = false;
    ptt.preDelay                    = 0;
    ptt.postDelay                   = 0;
    ptt.io_pin                      = 4;

    simplifiedTrackerMode           = false;
    sendCommentAfterXBeacons        = 10;
    path                            = "WIDE1-1";
    nonSmartBeaconRate              = 15;
    rememberStationTime             = 30;
    standingUpdateTime              = 15;
    sendAltitude                    = true;
    disableGPS                      = false;
    cadActive                       = true;
    cadWaitSec                      = 10;
    cadFskRssi                      = -95;
    email                           = "";

    Serial.println("New Data Created... All is Written!");
}

Configuration::Configuration() {
    // Vacio a proposito: SPIFFS (montar/formatear/escribir flash) NO puede ejecutarse en un constructor global,
    // porque el planificador FreeRTOS aun no existe (assert xTaskGetIdleTaskHandleForCore). Ver init().
}

void Configuration::init() {
    if (!SPIFFS.begin(false)) {
        Serial.println("SPIFFS Mount Failed, formatting...");

        if (!SPIFFS.begin(true)) {
            Serial.println("SPIFFS Format Failed");
            return;
        }
    }
    Serial.println("SPIFFS Ready");

    if (!SPIFFS.exists("/tracker_conf.json")) {
        Serial.println("Config not found, creating default...");
        setDefaultValues();
        writeFile();                                // sin reiniciar (ver readFile): la configuracion por defecto ya esta en memoria
        return;
    }

    readFile();
}