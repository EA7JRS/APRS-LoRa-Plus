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

#include <RadioLib.h>
#include <logger.h>
#include <SPI.h>
#include <SPIFFS.h>
#include "notification_utils.h"
#include "configuration.h"
#include "dapnet_utils.h"
#include "thermal_utils.h"
#include "board_pinout.h"
#include "pages_utils.h"
#include "lora_utils.h"
#include "ax25_g3ruh.h"
#include "lrfhss.h"
#include "display.h"

extern logging::Logger  logger;
extern Configuration    Config;
extern LoraType         *currentLoRaType;
extern Beacon           *currentBeacon;
extern uint8_t          loraIndex;
extern int              loraIndexSize;

bool operationDone   = true;
bool transmitFlag    = true;

#define DIFS_SLOTS      2       // Number of secuential CAD slots to consider a free channel to Tx
#define BACKOFF_MIN     4       // Initial backoff window (slots); doubles every time the channel is found busy
#define BACKOFF_MAX     32      // Backoff window ceiling
#define CAD_MAX_WAIT_MS ((unsigned long)Config.cadWaitSec * 1000UL)   // Max total time waiting for a free channel before Tx anyway (WebUI)
unsigned long cadStartTime = 0;
bool cadNoticeShown = false;

#if defined(HAS_SX1262)
    SX1262 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);
#endif
#if defined(HAS_SX1268)
    #if defined(LIGHTTRACKER_PLUS_1_0)
        SPIClass loraSPI(FSPI);
        SX1268 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN, loraSPI);
    #else
        SX1268 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);
    #endif
#endif
#if defined(HAS_SX1278)
    SX1278 radio = new Module(RADIO_CS_PIN, RADIO_BUSY_PIN, RADIO_RST_PIN);
#endif
#if defined(HAS_SX1276)
    SX1276 radio = new Module(RADIO_CS_PIN, RADIO_BUSY_PIN, RADIO_RST_PIN);
#endif
#if defined(HAS_LLCC68) //  LLCC68 supports spreading factor only in range of 5-11!
    LLCC68 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);
#endif

#if defined(HAS_SX1278) || defined(HAS_SX1276)
    #define CHIP_MIN_POWER  2       // PA_BOOST (below 2 RadioLib switches to RFO, not wired on most modules)
    #define CHIP_MAX_POWER  20
#else                               // SX1262 / SX1268 / LLCC68
    #define CHIP_MIN_POWER  -9
    #define CHIP_MAX_POWER  22
#endif

#if defined(HAS_LLCC68)
    #define RADIO_HAS_FSK   0       // LLCC68 is LoRa only
#else
    #define RADIO_HAS_FSK   1
#endif

#ifndef RADIO_MAX_POWER             // optional per board in board_pinout.h (e.g. 1W PA modules)
    #define RADIO_MAX_POWER CHIP_MAX_POWER
#endif

namespace LoRa_Utils {

    void setFlag(void) {
        operationDone = true;
    }

    int validPower(int requested) {
        const int maxPower = (RADIO_MAX_POWER < CHIP_MAX_POWER) ? RADIO_MAX_POWER : CHIP_MAX_POWER;
        int power = constrain(requested, CHIP_MIN_POWER, maxPower);
        #if defined(HAS_SX1278) || defined(HAS_SX1276)
            if (power > 17 && power < 20) power = 17;   // SX127x PA_BOOST: only 2-17 or 20
        #endif
        return power;
    }

    void setPower(int requested) {      // each LoRa type (EU/WORLD, POLAND, UK, US) has its own power
        int power = validPower(requested);
        if (power != requested) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "LoRa", "Power adjusted: %d -> %d", requested, power);
        }
        int state = radio.setOutputPower(power);
        if (state != RADIOLIB_ERR_NONE) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "LoRa", "setOutputPower failed! State: %d", state);    // log and keep going
        }
    }

    // Potencia elegida con las pantallas: se guarda por perfil LoRa y gana a la del fichero de configuracion
    const char* POWER_FILE = "/loraPower.dat";
    const int   POWER_SLOTS = 8;
    const int   POWER_NONE  = 999;
    int         powerOverride[POWER_SLOTS] = { POWER_NONE, POWER_NONE, POWER_NONE, POWER_NONE, POWER_NONE, POWER_NONE, POWER_NONE, POWER_NONE };

    void loadSavedPower() {
        if (!SPIFFS.begin(true)) return;
        File file = SPIFFS.open(POWER_FILE, "r");
        if (!file) return;
        String content = file.readStringUntil('\n');
        file.close();
        const char* cursor = content.c_str();
        for (int i = 0; i < POWER_SLOTS; i++) {
            int value = POWER_NONE;
            int used = 0;
            if (sscanf(cursor, "%d%n", &value, &used) != 1) break;
            cursor += used;
            powerOverride[i] = value;
            if (value != POWER_NONE && i < (int)Config.loraTypes.size()) Config.loraTypes[i].power = validPower(value);
        }
    }

    void savePower() {
        File file = SPIFFS.open(POWER_FILE, FILE_WRITE);
        if (!file) return;
        for (int i = 0; i < POWER_SLOTS; i++) {
            file.print(powerOverride[i]);
            file.print(i + 1 < POWER_SLOTS ? ' ' : '\n');
        }
        file.close();
    }

    bool profileIsFsk();                // definida mas abajo
    bool cwBeaconProfile = false;       // perfil de radio CW activo en modo MORSE: true = baliza CW, false = telegrafia

    // Frecuencia y potencia del modo en curso: APRS LoRa usa el perfil LoRa; APRS FSK, su perfil FSK propio (region IARU / canal);
    // MORSE, el perfil CW Beacon o el de Telegrafia. El perfil nautico AIS conserva siempre el suyo.
    long catFreqOverrideHz = 0;         // CAT (Hamlib): frecuencia pedida por un programa, solo en RAM; 0 = la del perfil. Se descarta al cambiar de perfil

    long activeFreqHz() {
        if (catFreqOverrideHz > 0 && !currentLoRaType->ais && (Config.operatingMode == MODE_APRS_LORA || Config.operatingMode == MODE_APRS_FSK)) return catFreqOverrideHz;
        if (Config.operatingMode == MODE_DAPNET) return Config.dapnet.freqHz;           // receptor de buscapersonas: frecuencia propia
        if (currentLoRaType->ais) return currentLoRaType->frequency;
        if (profileIsFsk()) return currentLoRaType->fskFrequency > 0 ? currentLoRaType->fskFrequency : currentLoRaType->frequency;
        if (Config.operatingMode == MODE_MORSE) {
            const long f = cwBeaconProfile ? Config.cw.beaconFreq : Config.cw.keyFreq;
            if (f > 0) return f;
        }
        return currentLoRaType->frequency;
    }

    // Correccion global del oscilador (ppm): el error es proporcional a la frecuencia, igual para todos los modos y para TX y RX.
    // Con ppm > 0 la frecuencia programada sube (el oscilador real iba por debajo del nominal).
    double ppmCorrectionHzExact(long hz) {
        return (double)hz * (double)Config.radioPpm1000 / 1e9;
    }

    long ppmCorrectionHz(long hz) {
        return lround(ppmCorrectionHzExact(hz));
    }

    // Frecuencia (MHz) que se programa en el chip: la nominal del modo mas la correccion global
    static float radioFreqMHz() {
        const long hz = activeFreqHz();
        return (float)(((double)hz + ppmCorrectionHzExact(hz)) / 1e6);
    }

    // Acceso al registro de frecuencia del SX126x (RadioLib lo declara protegido): tambien lo usan WSPR y CW para cambiar de tono sin reiniciar la portadora
    #if defined(HAS_SX1262) || defined(HAS_SX1268)
        struct WsprAccess : public decltype(radio) {
            using decltype(radio)::setRfFrequency;
        };
    #endif

    // RadioLib calcula el registro de frecuencia con un float (resolucion de unos 30 Hz a 433 MHz), que se come la correccion fina en ppm.
    // Tras cada setFrequency()/begin() se reescribe el registro con la frecuencia exacta (nominal + correccion en ppm): paso del PLL de 0,954 Hz
    static void refineFrequency() {
        #if defined(HAS_SX1262) || defined(HAS_SX1268)
            const long nominal = activeFreqHz();
            const double hz = (double)nominal + ppmCorrectionHzExact(nominal);
            const uint32_t frf = (uint32_t)llround(hz * 33554432.0 / 32000000.0);       // Hz * 2^25 / 32 MHz
            static_cast<WsprAccess&>(radio).setRfFrequency(frf);
        #endif
    }

    int* activePowerRef() {
        if (currentLoRaType->ais) return &currentLoRaType->power;
        if (profileIsFsk()) {
            if (currentLoRaType->fskPower <= 0) currentLoRaType->fskPower = currentLoRaType->power;
            return &currentLoRaType->fskPower;
        }
        if (Config.operatingMode == MODE_MORSE) {
            int* p = cwBeaconProfile ? &Config.cw.beaconPower : &Config.cw.keyPower;
            if (*p <= 0) *p = currentLoRaType->power;
            return p;
        }
        return &currentLoRaType->power;
    }

    int getPower() {
        return *activePowerRef();
    }

    int powerMin() {
        return validPower(-1000);
    }

    int powerMax() {
        return validPower(1000);
    }

    // Aplica y guarda la potencia del perfil LoRa activo (devuelve la potencia realmente aplicada)
    int setProfilePower(int requested) {
        int power = validPower(requested);
        #if defined(HAS_SX1278) || defined(HAS_SX1276)
            if (requested > *activePowerRef() && power == 17 && requested == 18) power = 20;     // 18 y 19 no existen: de 17 se salta a 20
        #endif
        int* powerRef = activePowerRef();
        const bool loraSlot = powerRef == &currentLoRaType->power;
        *powerRef = power;
        setPower(power);
        #if defined(TTGO_T_BEAM_1W)
            radio.setPaRampTime(RADIOLIB_SX126X_PA_RAMP_800U);
        #endif
        if (loraSlot) {
            int index = currentLoRaType - &Config.loraTypes[0];
            if (index >= 0 && index < POWER_SLOTS) {
                powerOverride[index] = power;
                savePower();
            }
        } else {
            Config.writeFile();             // potencia de los perfiles FSK / CW: se guarda en la configuracion
        }
        return power;
    }

    bool aisLoraActive = false;     // true mientras el perfil de baliza activo es el nautico AIS (usa su propio perfil LoRa)

    bool fskMode = false;           // modem actualmente cargado en el chip (false = LoRa, true = FSK)

    // ---- Packet FSK G3RUH (AX.25 a 9600 bd) -------------------------------------------------------------------------
    // Sin palabra de sincronismo, sin preambulo, sin CRC ni whitening del chip: el chip solo mueve bits en bloques de
    // longitud fija; el scrambler G3RUH, el NRZI, el HDLC y el CRC AX.25 se hacen por software (ax25_g3ruh.cpp)
    #define G3RUH_RX_LEN        255         // bytes por bloque de recepcion (255 bytes = 212 ms a 9600 bd)
    #define G3RUH_LEAD_FLAGS    60          // banderas 0x7E antes de la trama (60 = 50 ms), se reducen si no caben en 255 bytes
    #define FSK_QUEUE_SIZE      4
    #define POCSAG_RX_LEN       64          // modo DAPNET: bytes por bloque (64 bytes = 427 ms a 1200 bd, un lote POCSAG)

    [[maybe_unused]] static uint8_t fskShapingApplied = 0xFF;                      // ultimo filtro gaussiano cargado en el chip (0xFF = ninguno)
    static inline uint8_t fskBlockLen()    { return Config.operatingMode == MODE_DAPNET ? POCSAG_RX_LEN : G3RUH_RX_LEN; }
    // Filtro gaussiano del transmisor FSK (perfil): 0 sin filtro, 1 BT 0,5 (GFSK, G3RUH), 2 BT 0,3 (GMSK), 3 BT 0,7, 4 BT 1,0. DAPNET solo recibe: sin filtro
    static inline uint8_t fskShapingValue() {
        if (Config.operatingMode == MODE_DAPNET) return RADIOLIB_SHAPING_NONE;
        switch (currentLoRaType->fskShaping) {
            case 0:  return RADIOLIB_SHAPING_NONE;
            case 2:  return RADIOLIB_SHAPING_0_3;
            #if defined(HAS_SX1276) || defined(HAS_SX1278)
            case 3:  return RADIOLIB_SHAPING_0_5;                 // el SX127x no tiene BT 0,7: se usa el mas cercano
            #else
            case 3:  return RADIOLIB_SHAPING_0_7;
            #endif
            case 4:  return RADIOLIB_SHAPING_1_0;
            default: return RADIOLIB_SHAPING_0_5;
        }
    }
    static inline float fskBitRateKbps() { return Config.operatingMode == MODE_DAPNET ? Config.dapnet.baud / 1000.0f : currentLoRaType->fskBitRate; }
    static inline float fskDevKHz()    { return Config.operatingMode == MODE_DAPNET ? Config.dapnet.devTenthsKHz / 10.0f : currentLoRaType->fskFreqDev; }
    static inline float fskRxBwKHz()    { return Config.operatingMode == MODE_DAPNET ? Config.dapnet.rxBwTenthsKHz / 10.0f : currentLoRaType->fskRxBandwidth; }

    AX25G3::Decoder fskDecoder;
    String          fskQueue[FSK_QUEUE_SIZE];
    uint8_t         fskQueueCount = 0;
    int             fskLastRssi   = 0;

    void fskOnFrame(const uint8_t* frame, size_t len, void*) {
        char tnc2[AX25G3::MAX_FRAME + 120];
        if (AX25G3::frameToTnc2(frame, len, tnc2, sizeof(tnc2)) == 0) return;
        if (fskQueueCount < FSK_QUEUE_SIZE) {
            fskQueue[fskQueueCount++] = String("\x3c\xff\x01") + tnc2;   // se repone la cabecera LoRa APRS para el resto de la aplicacion
        }
    }

    // Lee un bloque del chip y lo pasa por el decodificador G3RUH
    void fskReadBlock() {
        uint8_t block[256];
        size_t  n = radio.getPacketLength();
        if (n == 0 || n > sizeof(block)) n = fskBlockLen();
        if (radio.readData(block, n) == RADIOLIB_ERR_NONE) {
            fskLastRssi = (int)radio.getRSSI();
            if (Config.operatingMode == MODE_DAPNET) DAPNET_Utils::feed(block, n, fskLastRssi);      // POCSAG: sincronismo por software
            else                                      fskDecoder.feed(block, n, fskOnFrame, nullptr);
        }
    }

    bool fskPopPacket(ReceivedLoRaPacket& rx) {
        if (fskQueueCount == 0) return false;
        rx.text      = fskQueue[0];
        rx.rssi      = fskLastRssi;
        rx.snr       = 0;
        rx.freqError = 0;
        for (uint8_t i = 1; i < fskQueueCount; i++) fskQueue[i - 1] = fskQueue[i];
        fskQueueCount--;
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "FSK Rx", "---> %s", rx.text.substring(3).c_str());
        return true;
    }

    void armReceive() {
        #if RADIO_HAS_FSK
        if (fskMode) radio.fixedPacketLengthMode(fskBlockLen());
        #endif
        radio.startReceive();
    }

    // La modulacion la decide el modo elegido al arrancar: APRS FSK usa FSK G3RUH; APRS LoRa y MORSE usan LoRa. El perfil AIS es siempre LoRa
    bool profileIsFsk() {
        return RADIO_HAS_FSK && ((Config.operatingMode == MODE_APRS_FSK && !currentLoRaType->ais) || Config.operatingMode == MODE_DAPNET);   // DAPNET (POCSAG) tambien usa el modem FSK
    }

    bool fskAvailable() {
        return RADIO_HAS_FSK;
    }

    // begin()/beginFSK() resetean el chip: hay que reaplicar los ajustes que dependen de la placa
    void applyBoardRadioSettings() {
        #if defined(RADIO_RXEN) && defined(RADIO_TXEN)
            radio.setRfSwitchPins(RADIO_RXEN, RADIO_TXEN);
        #endif
        #if defined(TTGO_T_BEAM_1W)
            radio.setRfSwitchPins(RADIO_RXEN, RADIOLIB_NC);
        #endif
        #if defined(HAS_SX1278) || defined(HAS_SX1276)
            radio.setCurrentLimit(120); // OCP ceiling for SX127x: ~120mA needed at +20dBm (not a fixed consumption)
        #else                           // SX1262 / SX1268 / LLCC68 (also 1W Ebyte E22 / E220 modules)
            radio.setCurrentLimit(140);
        #endif
        #if defined(HAS_SX1262) || defined(HAS_SX1268) || defined(HAS_LLCC68)
            radio.setRxBoostedGainMode(true);
        #endif
        #if defined(HAS_TCXO) && !defined(HAS_1W_LORA)
            radio.setDio2AsRfSwitch();
        #endif
        #if defined(TTGO_T_BEAM_1W)
            radio.setDio2AsRfSwitch(true);                          // DIO2 drives the 1W PA
        #endif
        #ifdef HAS_TCXO
            radio.setTCXO(1.8);
        #endif
    }

    // Arranca el modem (LoRa o FSK) del perfil activo. Devuelve el codigo de estado de RadioLib
    int beginModem(float freq) {
        int state;
        #if RADIO_HAS_FSK
        if (profileIsFsk()) {                       // el modem FSK lo decide el modo elegido al arrancar
            #if defined(HAS_SX1262) || defined(HAS_SX1268)
                #if defined(RADIO_HAS_XTAL)
                    state = radio.beginFSK(freq, fskBitRateKbps(), fskDevKHz(), fskRxBwKHz(), 10, 16, 0.0);
                #else
                    state = radio.beginFSK(freq, fskBitRateKbps(), fskDevKHz(), fskRxBwKHz(), 10, 16);
                #endif
            #else
                state = radio.beginFSK(freq, fskBitRateKbps(), fskDevKHz(), fskRxBwKHz(), 10, 16);
            #endif
            if (state == RADIOLIB_ERR_NONE) {
                fskMode = true;
                uint8_t noSync[1] = { 0 };
                radio.setSyncWord(noSync, 0);                       // sin sincronismo ni deteccion de preambulo
                radio.setPreambleLength(0);
                #if defined(HAS_SX1262) || defined(HAS_SX1268)
                    radio.setCRC(0);
                    radio.setWhitening(false);
                #else
                    radio.setCRC(false);
                    radio.setEncoding(RADIOLIB_ENCODING_NRZ);
                #endif
                fskShapingApplied = fskShapingValue();
                radio.setDataShaping(fskShapingApplied);              // por defecto GFSK BT 0.5 como G3RUH; GMSK (BT 0.3) y otros desde la WebUI
                radio.fixedPacketLengthMode(fskBlockLen());
                fskDecoder.reset();
                fskQueueCount = 0;
                if (Config.operatingMode == MODE_DAPNET) DAPNET_Utils::reset();
            }
            return state;
        }
        #endif
        #if defined(RADIO_HAS_XTAL)
            // RadioLib >= 7.8: radio.XTAL no longer exists; a TCXO voltage of 0 V means XTAL
            state = radio.begin(freq, 125.0, 9, 7, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 10, 8, 0.0);
        #else
            state = radio.begin(freq);
        #endif
        if (state == RADIOLIB_ERR_NONE) {
            fskMode = false;
            radio.setSpreadingFactor(currentLoRaType->spreadingFactor);
            float signalBandwidth = currentLoRaType->signalBandwidth/1000;
            radio.setBandwidth(signalBandwidth);
            radio.setCodingRate(currentLoRaType->codingRate4);
            radio.setCRC(true);
        }
        return state;
    }

    void applyCurrentType() {
        float freq = radioFreqMHz();
        if (profileIsFsk() != fskMode) {            // cambio de modem LoRa <-> FSK: reinicio completo del chip
            int state = beginModem(freq);
            if (state != RADIOLIB_ERR_NONE) {
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "LoRa", "Modem switch failed! State: %d", state);
                return;
            }
            applyBoardRadioSettings();
            refineFrequency();
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "LoRa", "Modem: %s", fskMode ? "FSK" : "LoRa");
        } else if (fskMode) {
            #if RADIO_HAS_FSK
            radio.setFrequency(freq);
            refineFrequency();
            radio.setBitRate(fskBitRateKbps());
            radio.setFrequencyDeviation(fskDevKHz());
            radio.setRxBandwidth(fskRxBwKHz());
            if (fskShapingValue() != fskShapingApplied) {            // solo si cambia: en el SX127x poner el filtro pasa por standby
                fskShapingApplied = fskShapingValue();
                radio.setDataShaping(fskShapingApplied);
                #if defined(HAS_SX1276) || defined(HAS_SX1278)
                    radio.startReceive();
                #endif
            }
            fskDecoder.reset();
            #endif
        } else {
            radio.setFrequency(freq);
            refineFrequency();
            radio.setSpreadingFactor(currentLoRaType->spreadingFactor);
            float signalBandwidth = currentLoRaType->signalBandwidth/1000;
            radio.setBandwidth(signalBandwidth);
            radio.setCodingRate(currentLoRaType->codingRate4);
        }
        setPower(getPower());
        #if defined(TTGO_T_BEAM_1W)
            radio.setPaRampTime(RADIOLIB_SX126X_PA_RAMP_800U);  // after setPower(): setOutputPower() resets the PA ramp time
        #endif
    }

    bool isAisActive() {
        return aisLoraActive;
    }

    // El perfil de baliza nautico AIS usa siempre su perfil LoRa (SF9 / CR 4/5); al salir de el se vuelve al perfil elegido con el menu
    void syncAisProfile() {
        int aisIndex = Config.aisLoraIndex();
        bool wanted  = currentBeacon != nullptr && currentBeacon->aisProfile && aisIndex >= 0;
        if (wanted == aisLoraActive) return;
        aisLoraActive   = wanted;
        currentLoRaType = wanted ? &Config.loraTypes[aisIndex] : &Config.loraTypes[loraIndex];
        applyCurrentType();
        transmitFlag    = true;                 // la recepcion se rearma con los parametros nuevos
        operationDone   = true;
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "LoRa", "%s profile: Freq %ld / SF%d / CR 4/%d", wanted ? "AIS nautical" : "Regional",
                   currentLoRaType->frequency, currentLoRaType->spreadingFactor, currentLoRaType->codingRate4);
    }

    // Frecuencia del perfil activo sin el ajuste de CAT (Hz)
    long profileFreqHz() {
        const long saved = catFreqOverrideHz;
        catFreqOverrideHz = 0;
        const long hz = activeFreqHz();
        catFreqOverrideHz = saved;
        return hz;
    }

    long liveFreqHz() {
        return activeFreqHz();
    }

    // Aplica de nuevo la frecuencia del perfil activo (la configuracion cambio en caliente) y rearma la escucha
    void retune() {
        if (aisLoraActive) return;
        catFreqOverrideHz = 0;
        applyCurrentType();
        transmitFlag  = true;
        operationDone = true;
    }

    // CAT: cambia la frecuencia en caliente (no se guarda). Devuelve false si el perfil no lo permite (AIS fijo) o el chip la rechaza
    bool setLiveFreqHz(long hz) {
        if (aisLoraActive || hz <= 0) return false;
        catFreqOverrideHz = (hz == profileFreqHz()) ? 0 : hz;
        applyCurrentType();
        transmitFlag  = true;           // rearma la escucha en la nueva frecuencia
        operationDone = true;
        return true;
    }

    void changeFreq() {
        if (aisLoraActive) {            // el perfil nautico AIS tiene la frecuencia fija
            displayShow("LORA FREQ>", "", "FIXED BY AIS", "NAUTICAL PROFILE", "", "", 2000);
            return;
        }
        if (loraIndex >= (loraIndexSize - 1)) {
            loraIndex = 0;
        } else {
            loraIndex++;
        }
        currentLoRaType = &Config.loraTypes[loraIndex];
        catFreqOverrideHz = 0;
        applyCurrentType();
        transmitFlag  = true;           // rearma la escucha en la nueva frecuencia
        operationDone = true;

        String loraCountryFreq;
        switch (loraIndex) {
            case 0: loraCountryFreq = "EU/WORLD"; break;
            case 1: loraCountryFreq = "POLAND"; break;
            case 2: loraCountryFreq = "UK"; break;
            case 3: loraCountryFreq = "US"; break;
            default: loraCountryFreq = "CUSTOM"; break;
        }
        String currentLoRainfo = "LoRa ";
        currentLoRainfo += loraCountryFreq;
        currentLoRainfo += " / Freq: ";
        currentLoRainfo += String(currentLoRaType->frequency);
        currentLoRainfo += " / SF:";
        currentLoRainfo += String(currentLoRaType->spreadingFactor);
        currentLoRainfo += " / CR: ";
        currentLoRainfo += String(currentLoRaType->codingRate4);

        logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "LoRa", "%s", currentLoRainfo.c_str());
        displayShow("LORA FREQ>", "", "CHANGED TO: " + loraCountryFreq, "", "", "", 2000);
    }

    void setup() {
        #if defined(LIGHTTRACKER_PLUS_1_0) || defined(TTGO_T_BEAM_1W)
            pinMode(RADIO_VCC_PIN,OUTPUT);
            digitalWrite(RADIO_VCC_PIN,HIGH);
        #endif
        #if defined(TTGO_T_BEAM_1W)
            pinMode(RADIO_RXEN, OUTPUT);
            digitalWrite(RADIO_RXEN, LOW);  // start setup in Tx mode
        #endif

        logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "LoRa", "Set SPI pins!");
        #if defined(LIGHTTRACKER_PLUS_1_0)
            loraSPI.begin(RADIO_SCLK_PIN, RADIO_MISO_PIN, RADIO_MOSI_PIN, RADIO_CS_PIN);
        #else
            SPI.begin(RADIO_SCLK_PIN, RADIO_MISO_PIN, RADIO_MOSI_PIN);
        #endif
        float freq = radioFreqMHz();
        int state = beginModem(freq);
        if (state == RADIOLIB_ERR_NONE) {
            #if defined(HAS_SX1262) || defined(HAS_SX1268)
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "LoRa", "Initializing SX126X ...");
            #else
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "LoRa", "Initializing SX127X ...");
            #endif
        } else {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "LoRa", "Starting LoRa failed! State: %d", state);
            while (true);
        }
        #if defined(HAS_SX1262) || defined(HAS_SX1268) || defined(HAS_LLCC68)
            radio.setDio1Action(setFlag);
        #endif
        #if defined(HAS_SX1278) || defined(HAS_SX1276)
            radio.setDio0Action(setFlag, RISING);
        #endif
        applyBoardRadioSettings();
        refineFrequency();
        loadSavedPower();
        setPower(getPower());
        #if defined(TTGO_T_BEAM_1W)
            radio.setPaRampTime(RADIOLIB_SX126X_PA_RAMP_800U);      // PA needs >800us to settle (default 200us)
        #endif

        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "LoRa", "LoRa init done!");
    }

    // Canal ocupado? LoRa: CAD (deteccion de preambulo LoRa). FSK (analogico): no hay preambulo que detectar, se mide el nivel
    // de RF con el receptor FSK y se compara con el umbral de la WebUI. Un fallo del chip cuenta como canal libre: no debe
    // bloquear la baliza.
    bool channelBusy() {
        if (fskMode) {
            #if defined(HAS_SX1262) || defined(HAS_SX1268)
                radio.startReceive();                       // el RSSI instantaneo solo es valido con el receptor en marcha
                delay(6);                                   // tiempo de asentamiento del AGC / RSSI
                return radio.getRSSI(false) > (float)Config.cadFskRssi;
            #else
                return false;                               // SX127x: sin lectura instantanea fiable
            #endif
        }
        int state = radio.scanChannel();
        if (state == RADIOLIB_LORA_DETECTED) return true;
        if (state != RADIOLIB_CHANNEL_FREE) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "LoRa Tx", "CAD error %d: treating the channel as free", state);
        }
        return false;
    }

    bool doDIFS() {
        for (uint8_t i = DIFS_SLOTS; i > 0; i--) {
            if (channelBusy()) return false;
        }
        return true;
    }

    bool cadTimedOut() {
        return millis() - cadStartTime > CAD_MAX_WAIT_MS;
    }

    void showCadNotice() {
        if (cadNoticeShown) return;
        cadNoticeShown = true;
        displayShow(fskMode ? "<<< LBT >>>" : "<<< CAD >>>", "Waiting for", "free channel...", 0);
    }

    void waitForDIFS() {
        while (!doDIFS()) {
            if (cadTimedOut()) return;
            showCadNotice();
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "LoRa Tx", "Channel busy (DIFS), retry...");
            delay(random(5, 25));                           // des-sincroniza a los que esperan el mismo canal y cede tiempo al sistema
        }
    }

    // Backoff exponencial binario: cada vez que el canal se encuentra ocupado la ventana se duplica (4, 8, 16, 32 ranuras)
    void doBEB() {
        int window = BACKOFF_MIN;
        int backoffCounter = random(1, window + 1);
        while (backoffCounter > 0) {
            if (cadTimedOut()) return;
            if (channelBusy()) {
                showCadNotice();
                if (window < BACKOFF_MAX) window *= 2;
                backoffCounter = random(1, window + 1);     // ocupado: se amplia la ventana y se sortea de nuevo
                waitForDIFS();                              // y se espera a que el canal quede libre durante el DIFS
            } else {
                backoffCounter--;
            }
        }
    }

    // Cada modo emite solo lo suyo: en MORSE/CW, WSPR y LR-FHSS el SX126x esta configurado para ese modo y un paquete APRS
    // (SOS, mensaje, ACK, digipeater, telemetria, Bluetooth...) saldria con la modulacion y la frecuencia equivocadas.
    bool aprsTxAllowed() {
        return Config.operatingMode == MODE_APRS_LORA || Config.operatingMode == MODE_APRS_FSK;
    }

    void sendNewPacket(const String& newPacket) {
        if (!aprsTxAllowed()) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "LoRa Tx", "APRS packet dropped: mode %d is not an APRS mode", (int)Config.operatingMode);
            return;
        }
        if (Config.cadActive) {                 // CAD en LoRa, deteccion de portadora en FSK
            cadStartTime = millis();
            cadNoticeShown = false;
            waitForDIFS();  // DIFS (Distributed Inter-Frame Space)
            doBEB();        // BEB  (Binary Exponential Backoff)
            if (cadTimedOut()) {
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "LoRa Tx", "CAD timeout, transmitting anyway");
            }
            if (cadNoticeShown) displayShow("<<< TX >>>", "", newPacket, 0);
        }
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "LoRa Tx","---> %s", newPacket.c_str());
        /*logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "LoRa","Send data: %s", newPacket.c_str());
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "LoRa","Send data: %s", newPacket.c_str());
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "LoRa","Send data: %s", newPacket.c_str());*/

        if (Config.ptt.active) {
            digitalWrite(Config.ptt.io_pin, Config.ptt.reverse ? LOW : HIGH);
            delay(Config.ptt.preDelay);
        }
        if (Config.notification.ledTx) {
            #ifdef RPC_MicroTracker
                digitalWrite(Config.notification.ledTxPin, LOW);
            #else
                digitalWrite(Config.notification.ledTxPin, HIGH);
            #endif
        }
        if (Config.notification.buzzerActive && Config.notification.txBeep) NOTIFICATION_Utils::beaconTxBeep();

        #if defined(TTGO_T_BEAM_1W)
            digitalWrite(RADIO_RXEN, LOW);
        #endif
        #ifdef FAN_CTRL_PIN
            THERMAL_Utils::onTxStart();
        #endif
        int state;
        size_t fskBytes = 0;
        if (fskMode) {
            // TNC2 -> trama AX.25 UI -> flujo G3RUH (banderas + HDLC + FCS, NRZI, scrambler). La cabecera LoRa APRS no se envia
            uint8_t frame[AX25G3::MAX_FRAME], air[256];
            size_t  fl = AX25G3::tnc2ToFrame(newPacket.c_str(), frame, sizeof(frame));
            fskBytes   = 0;
            if (fl) {
                // protocolo elegido en el perfil; si la trama no cabe en un bloque de 255 bytes se emite como AX.25 normal
                const int fec = currentLoRaType->fskFec;
                if (fec >= 1 && fec <= 3) fskBytes = AX25G3::g3ruhEncodeFx25(frame, fl, air, sizeof(air), G3RUH_LEAD_FLAGS, 16 << (fec - 1));
                if (fec == 4)             fskBytes = AX25G3::g3ruhEncodeIl2p(frame, fl, air, sizeof(air), G3RUH_LEAD_FLAGS);
                if (fskBytes == 0)        fskBytes = AX25G3::g3ruhEncode(frame, fl, air, sizeof(air), G3RUH_LEAD_FLAGS);
            }
            if (fskBytes == 0) {
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "FSK Tx", "Packet not valid for AX.25 or too long: %s", newPacket.c_str());
                state = RADIOLIB_ERR_PACKET_TOO_LONG;
            } else {
                #if RADIO_HAS_FSK
                radio.fixedPacketLengthMode((uint8_t)fskBytes);
                #endif
                state = radio.transmit(air, fskBytes);
            }
            fskDecoder.reset();
        } else {
            state = radio.transmit("\x3c\xff\x01" + newPacket);
        }
        #ifdef FAN_CTRL_PIN
            THERMAL_Utils::onTxEnd();
        #endif
        transmitFlag = true;
        if (state == RADIOLIB_ERR_NONE) {
            //Serial.println(F("success!"));
            #ifdef HAS_APRS_PAGES
                PAGES_Utils::onTx(fskMode ? (int)fskBytes : (int)newPacket.length() + 3);
            #endif
        } else {
            Serial.print(F("Tx failed, code "));
            Serial.println(state);
        }

        if (Config.notification.ledTx) {
            #ifdef RPC_MicroTracker
                digitalWrite(Config.notification.ledTxPin, HIGH);
            #else
                digitalWrite(Config.notification.ledTxPin, LOW);
            #endif
        }
        if (Config.ptt.active) {
            delay(Config.ptt.postDelay);
            digitalWrite(Config.ptt.io_pin, Config.ptt.reverse ? HIGH : LOW);
        }
    }

    // ---- CW (Morse): portadora continua del SX126x con manipulacion todo/nada ---------------------------------------
    // Receptor DAPNET (POCSAG): FSK del SX126x con el flujo de bits sin procesar; el sincronismo y el BCH se hacen por software
    bool dapnetAvailable() {
        #if RADIO_HAS_FSK && (defined(HAS_SX1262) || defined(HAS_SX1268))
            return true;
        #else
            return false;
        #endif
    }

    bool cwAvailable() {
        #if defined(HAS_SX1262) || defined(HAS_SX1268)
            return true;
        #else
            return false;
        #endif
    }

    // Elige el perfil de radio CW (true = CW Beacon, false = Telegrafia) y sintoniza su frecuencia y potencia
    void cwSelect(bool beacon) {
        #if defined(HAS_SX1262) || defined(HAS_SX1268)
            if (Config.operatingMode != MODE_MORSE || cwBeaconProfile == beacon) return;
            cwBeaconProfile = beacon;
            radio.standby();
            int state = radio.setFrequency(radioFreqMHz());
            if (state != RADIOLIB_ERR_NONE) logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "CW", "setFrequency failed! State: %d", state);
            else refineFrequency();
            setPower(getPower());
            #if defined(TTGO_T_BEAM_1W)
                radio.setPaRampTime(RADIOLIB_SX126X_PA_RAMP_800U);
            #endif
        #else
            (void)beacon;
        #endif
    }

    void cwKey(bool on) {
        NOTIFICATION_Utils::lightTx(on);                    // LED rojo (Tx) = portadora (si esta elegido: LEDs in CW mode)
        NOTIFICATION_Utils::lightMorse(on);                 // flash blanco = lampara Morse
        #if defined(HAS_SX1262) || defined(HAS_SX1268)
            if (on) {
                #if defined(TTGO_T_BEAM_1W)
                    digitalWrite(RADIO_RXEN, LOW);
                #endif
                radio.transmitDirect();             // SetTxContinuousWave en la frecuencia y potencia actuales
            } else {
                radio.standby();
            }
        #else
            (void)on;
        #endif
    }

    bool cwRxBegin() {
        #if defined(HAS_SX1262) || defined(HAS_SX1268)
            cwBeaconProfile = false;                // la escucha de CW es del perfil de Telegrafia
            float freq = radioFreqMHz();
            #if defined(RADIO_HAS_XTAL)
                int state = radio.beginFSK(freq, 4.8f, 2.4f, 4.8f, 10, 16, 0.0f);
            #else
                int state = radio.beginFSK(freq, 4.8f, 2.4f, 4.8f, 10, 16);
            #endif
            if (state != RADIOLIB_ERR_NONE) {
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "CW Rx", "Narrow FSK start failed! State: %d", state);
                fskMode = !profileIsFsk();          // el chip queda en un estado desconocido: se fuerza el reinicio al salir
                return false;
            }
            fskMode = true;
            applyBoardRadioSettings();
            refineFrequency();
            #if defined(TTGO_T_BEAM_1W)
                digitalWrite(RADIO_RXEN, HIGH);
            #endif
            return radio.startReceive() == RADIOLIB_ERR_NONE;
        #else
            return false;
        #endif
    }

    float cwRxLevel() {
        #if defined(HAS_SX1262) || defined(HAS_SX1268)
            return radio.getRSSI(false);            // instantaneo; getRSSI() a secas devuelve el RSSI del ultimo paquete
        #else
            return -150.0f;
        #endif
    }

    void cwRxEnd() {
        #if defined(HAS_SX1262) || defined(HAS_SX1268)
            fskMode = !profileIsFsk();              // fuerza la reconfiguracion completa del modem del perfil
            applyCurrentType();
        #endif
        transmitFlag  = true;
        operationDone = true;
    }

    void cwEnd() {
        #if defined(HAS_SX1262) || defined(HAS_SX1268)
            radio.standby();
        #endif
        transmitFlag  = true;                       // se rearma la recepcion
        operationDone = true;
    }

    bool wsprBegin(uint32_t frf, int powerDbm) {
        #if defined(HAS_SX1262) || defined(HAS_SX1268)
            radio.standby();
            float freq = (float)((double)frf * 32.0 / 33554432.0);
            int state = radio.setFrequency(freq);                    // calibra la imagen para la banda
            if (state != RADIOLIB_ERR_NONE) {
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "WSPR", "setFrequency failed! State: %d", state);
                return false;
            }
            setPower(powerDbm);
            return true;
        #else
            (void)frf; (void)powerDbm;
            return false;
        #endif
    }

    bool wsprStart(uint32_t frf) {
        #if defined(HAS_SX1262) || defined(HAS_SX1268)
            #if defined(TTGO_T_BEAM_1W)
                digitalWrite(RADIO_RXEN, LOW);
            #endif
            return radio.transmitDirect(frf) == RADIOLIB_ERR_NONE;    // SetRfFrequency + SetTxContinuousWave
        #else
            (void)frf;
            return false;
        #endif
    }

    void wsprTone(uint32_t frf) {
        #if defined(HAS_SX1262) || defined(HAS_SX1268)
            static_cast<WsprAccess&>(radio).setRfFrequency(frf);
        #else
            (void)frf;
        #endif
    }

    void wsprEnd() {
        #if defined(HAS_SX1262) || defined(HAS_SX1268)
            radio.standby();
            fskMode = !profileIsFsk();                               // fuerza la reconfiguracion limpia del modem tras transmitDirect()
            applyCurrentType();                                      // vuelve a la frecuencia y la potencia del perfil
        #endif
        transmitFlag  = true;
        operationDone = true;
    }

    // ---- Baliza LR-FHSS: emision LR-FHSS solo transmision (SX1262/SX1268). RadioLib 7.8: beginLRFHSS(bw, cr, narrowGrid) y transmit() bloqueante ----
    bool lrfhssAvailable() {
        #if defined(HAS_SX1262) || defined(HAS_SX1268)
            return true;
        #else
            return false;
        #endif
    }

    #if defined(HAS_SX1262) || defined(HAS_SX1268)
        static const uint8_t LRFHSS_BW[10] = {
            RADIOLIB_SX126X_LR_FHSS_BW_39_06,  RADIOLIB_SX126X_LR_FHSS_BW_85_94,  RADIOLIB_SX126X_LR_FHSS_BW_136_72, RADIOLIB_SX126X_LR_FHSS_BW_183_59,
            RADIOLIB_SX126X_LR_FHSS_BW_335_94, RADIOLIB_SX126X_LR_FHSS_BW_386_72, RADIOLIB_SX126X_LR_FHSS_BW_722_66, RADIOLIB_SX126X_LR_FHSS_BW_773_44,
            RADIOLIB_SX126X_LR_FHSS_BW_1523_4, RADIOLIB_SX126X_LR_FHSS_BW_1574_2
        };
        static const uint8_t LRFHSS_CR[4] = {
            RADIOLIB_SX126X_LR_FHSS_CR_5_6, RADIOLIB_SX126X_LR_FHSS_CR_2_3, RADIOLIB_SX126X_LR_FHSS_CR_1_2, RADIOLIB_SX126X_LR_FHSS_CR_1_3
        };
    #endif

    // Emite una trama LR-FHSS y restaura el modem LoRa del perfil. Devuelve el codigo de estado de RadioLib
    int lrfhssSend(const uint8_t* data, size_t len) {
        #if defined(HAS_SX1262) || defined(HAS_SX1268)
            if (len == 0 || len > LRFHSS::MAX_PAYLOAD) return RADIOLIB_ERR_PACKET_TOO_LONG;
            radio.standby();
            #if defined(RADIO_HAS_XTAL)
                radio.tcxoVoltage = 0.0;                            // cristal sin TCXO (igual que beginModem)
            #else
                radio.tcxoVoltage = 1.6;                            // valor por defecto de begin(); applyBoardRadioSettings() lo sube a 1,8 V si la placa lo pide
            #endif
            int state = radio.beginLRFHSS(LRFHSS_BW[constrain(Config.lrfhss.bwIdx, 0, 9)], LRFHSS_CR[constrain(Config.lrfhss.crIdx, 0, 3)], Config.lrfhss.narrowGrid);
            if (state == RADIOLIB_ERR_NONE) state = radio.setFrequency((float)((double)(Config.lrfhss.freqHz + ppmCorrectionHz(Config.lrfhss.freqHz)) / 1e6));      // LR-FHSS (desactivado): sin afinado de PLL
            if (state == RADIOLIB_ERR_NONE) {
                applyBoardRadioSettings();
                setPower(Config.lrfhss.powerDbm);
                #if defined(TTGO_T_BEAM_1W)
                    digitalWrite(RADIO_RXEN, LOW);
                #endif
                state = radio.transmit(data, len);
            } else {
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "LR-FHSS", "LR-FHSS start failed! State: %d", state);
            }
            radio.standby();
            fskMode = !profileIsFsk();                              // fuerza la reconfiguracion limpia del modem LoRa tras LR-FHSS
            applyCurrentType();                                     // frecuencia, potencia y modem del perfil
            transmitFlag  = true;                                   // se rearma la recepcion
            operationDone = true;
            return state;
        #else
            (void)data; (void)len;
            return -1;
        #endif
    }

    void wakeRadio() {
        armReceive();
    }

    ReceivedLoRaPacket receiveFromSleep() {
        ReceivedLoRaPacket receivedLoraPacket;
        String packet = "";
        #if defined(TTGO_T_BEAM_1W)
            digitalWrite(RADIO_RXEN, HIGH);
        #endif
        if (fskMode) {
            fskReadBlock();
            fskPopPacket(receivedLoraPacket);
            return receivedLoraPacket;
        }
        int state = radio.readData(packet);
        if (state == RADIOLIB_ERR_NONE) {
            receivedLoraPacket.text       = packet;
            receivedLoraPacket.rssi       = radio.getRSSI();
            receivedLoraPacket.snr        = radio.getSNR();
            receivedLoraPacket.freqError  = radio.getFrequencyError();
        } else {
            //
        }
        return receivedLoraPacket;
    }

    ReceivedLoRaPacket receivePacket() {
        ReceivedLoRaPacket receivedLoraPacket;
        String packet = "";
        if (fskMode && fskQueueCount > 0) {         // quedan tramas FSK ya decodificadas de un bloque anterior
            fskPopPacket(receivedLoraPacket);
            if (fskQueueCount > 0) operationDone = true;
            return receivedLoraPacket;
        }
        if (operationDone) {
            operationDone = false;
            if (transmitFlag) {
                #if defined(TTGO_T_BEAM_1W)
                    digitalWrite(RADIO_RXEN, HIGH);
                #endif
                armReceive();
                transmitFlag = false;
            } else if (fskMode) {
                fskReadBlock();
                fskPopPacket(receivedLoraPacket);
                if (fskQueueCount > 0) operationDone = true;
            } else {
                int state = radio.readData(packet);
                if (state == RADIOLIB_ERR_NONE) {
                    if(!packet.isEmpty()) {
                        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "LoRa Rx","---> %s", packet.substring(3).c_str());
                        receivedLoraPacket.text       = packet;
                        receivedLoraPacket.rssi       = radio.getRSSI();
                        receivedLoraPacket.snr        = radio.getSNR();
                        receivedLoraPacket.freqError  = radio.getFrequencyError();
                    }
                } else {
                    Serial.print(F("Rx failed, code "));   // 7 = CRC mismatch
                    Serial.println(state);
                }
            }
        }
        return receivedLoraPacket;
    }

    void sleepRadio() {
        radio.sleep();
    }

}