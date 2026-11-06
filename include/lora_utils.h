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

#ifndef LORA_UTILS_H_
#define LORA_UTILS_H_

#include <Arduino.h>


struct ReceivedLoRaPacket {
    String  text;
    int     rssi;
    float   snr;
    int     freqError;
};

// Recepcion de satelites (modo SAT, fase 2): parametros del demodulador
struct SatRxParams {
    uint8_t kind;           // 0 = CW (FSK estrecho + RSSI), 1 = FSK / GMSK (AX.25 G3RUH), 2 = LoRa
    int64_t hz;             // frecuencia de recepcion (Hz), ya con el Doppler
    int     baud;           // FSK: baudios
    uint8_t sf;             // LoRa: factor de ensanchamiento 5..12
    float   bwKHz;          // LoRa: ancho de banda (kHz)
    uint8_t cr;             // LoRa: denominador de la tasa de codigo 5..8
};

namespace LoRa_Utils {

    void setFlag();
    int  validPower(int requested);
    bool fskAvailable();                // true si el chip tiene FSK (no el LLCC68)
    bool dapnetAvailable();             // true si el chip puede recibir POCSAG (FSK del SX1262/SX1268): modo DAPNET
    bool cwAvailable();                 // true si el chip puede emitir portadora continua (SX1262/SX1268)
    void cwSelect(bool beacon);         // MORSE: perfil de radio CW (true = CW Beacon, false = Telegrafia); sintoniza su frecuencia y potencia
    void cwKey(bool on);                // true: portadora encendida en la frecuencia del perfil; false: apagada
    void cwEnd();
    bool wsprBegin(uint32_t frf, int powerDbm);   // WSPR: prepara frecuencia y potencia del SX126x (sin emitir)
    bool wsprStart(uint32_t frf);                 // enciende la portadora en la palabra PLL frf
    void wsprTone(uint32_t frf);                  // cambia la palabra PLL sin reiniciar la portadora
    void wsprEnd();                               // apaga la portadora y rearma la recepcion
    bool lrfhssAvailable();                       // true si el chip emite LR-FHSS (SX1262/SX1268)
    int  lrfhssSend(const uint8_t* data, size_t len);   // emite una trama LR-FHSS con la configuracion de la baliza (bloquea unos segundos) y restaura el modem LoRa; codigo de estado de RadioLib
    bool cwRxBegin();                   // modem FSK de banda estrecha (4,8 kHz) en la frecuencia del perfil para escuchar CW; false si no se puede
    float cwRxLevel();                  // RSSI instantaneo en dBm (solo tras cwRxBegin)
    void cwRxEnd();                     // restaura el modem del perfil y la escucha normal                       // termina la transmision CW y vuelve a escuchar
    // ---- Modo SAT, fase 2: receptor de satelites con frecuencia absoluta (solo SX126x). Solo los toca una tarea a la vez ----
    bool satRxAvailable();              // true si el chip puede (SX1262 / SX1268)
    bool satRxBegin(const SatRxParams& p);          // carga el modem y empieza a escuchar; false si la frecuencia o los parametros no son validos
    bool satRxTune(int64_t hz);                     // cambia la frecuencia de recepcion (Doppler) y rearma la escucha
    float satRxLevel();                             // RSSI instantaneo (dBm)
    // Mira si ha llegado algo. Devuelve 0 nada, 1 trama AX.25 (texto TNC2 o volcado hexadecimal en out), 2 paquete LoRa (bytes en out)
    int  satRxPoll(uint8_t* out, size_t outMax, size_t& len, int& rssi, float& snr, int& freqErr);
    void satRxEnd();                                // devuelve el chip al perfil normal
    int  getPower();                    // potencia (dBm) del perfil LoRa activo
    int  powerMin();
    int  powerMax();
    int  setProfilePower(int requested);   // aplica y guarda la potencia del perfil activo; devuelve la aplicada
    void changeFreq();
    long profileFreqHz();               // frecuencia (Hz) del perfil activo, sin el ajuste de CAT
    long liveFreqHz();                  // frecuencia (Hz) en uso ahora (la del perfil o la que ha pedido CAT)
    void retune();                      // aplica de nuevo la frecuencia del perfil activo (tras cambiar la configuracion en caliente: region IARU automatica)
    bool setLiveFreqHz(long hz);        // CAT (Hamlib): cambia la frecuencia en caliente, solo en RAM; false si el perfil la tiene fija
    bool selectLoraIndex(uint8_t index);    // perfil LoRa concreto (app por BLE); false con el perfil nautico AIS o indice fuera de rango
    void syncAisProfile();      // aplica o retira el perfil LoRa AIS segun el perfil de baliza activo
    bool isAisActive();
    void setup();
    long   ppmCorrectionHz(long hz);        // correccion global del oscilador (ppm) para esa frecuencia, en Hz (con signo, redondeada)
    double ppmCorrectionHzExact(long hz);   // idem sin redondear (resolucion de 0,001 ppm)
    bool aprsTxAllowed();               // true solo en los modos APRS (LoRa o FSK): MORSE, WSPR y LR-FHSS no emiten paquetes APRS
    void sendNewPacket(const String& newPacket);
    void wakeRadio();
    ReceivedLoRaPacket receiveFromSleep();
    ReceivedLoRaPacket receivePacket();
    void sleepRadio();

}

#endif