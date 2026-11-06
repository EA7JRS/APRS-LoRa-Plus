/*
 * Modo SAT, fase 2: recepcion de satelites con correccion Doppler continua.
 *   CW      -> modem FSK estrecho (4,8 kHz) + detector de envolvente por RSSI + decodificador Morse
 *   FSK/GMSK -> modem FSK sin sincronismo + decodificador AX.25 G3RUH (9600 bd y similares), tramas UI en formato TNC2
 *   LoRa    -> modem LoRa (SF / BW / CR de la descripcion del transmisor o los que elija el usuario), carga util en texto o hexadecimal
 * AFSK 1200, BPSK, SSB, FM de voz, SSTV, LRPT, banda L... no se pueden demodular con un SX126x: se usa un SDR u otro equipo
 * (la pantalla de frecuencias da la frecuencia corregida por Doppler para sintonizarlos).
 *
 * Copyright (c) 2026 Manuel - EA7JRS. GPL-3.0-or-later.
 */
#ifndef SAT_RX_H_
#define SAT_RX_H_

#include <stddef.h>
#include <stdint.h>
#include "sat_core.h"

namespace SATRX_Utils {

    enum Kind : uint8_t { K_NONE = 0, K_CW = 1, K_FSK = 2, K_LORA = 3, K_NO = 4 };      // K_NO = no demodulable con esta radio

    Kind        classify(const SatCore::Transmitter& t, const char** why = nullptr);    // clase de recepcion de un transmisor
    const char* kindText(Kind k);
    void        loraDefaults(const SatCore::Transmitter& t, uint8_t& sf, float& bwKHz, uint8_t& cr);   // SF / BW / CR que dice la descripcion del transmisor
    bool        available();                       // true si la placa puede recibir satelites (SX1262 / SX1268 en una placa con modo SAT)

    bool        start(uint8_t txIndex);            // empieza a escuchar el transmisor del satelite elegido; false y message() si no se puede
    void        stop();
    bool        active();
    void        loop();                            // bucle principal: Doppler y vigilancia (barato)

    const char* message();                         // ultimo aviso / error
    Kind        kind();
    uint8_t     txIndex();
    double      rxHz();                            // frecuencia a la que esta sintonizado el receptor (Hz)
    int         dopplerHz();                       // correccion Doppler aplicada sobre la nominal (Hz)
    bool        dopplerOk();                       // false = sin hora o posicion: se escucha la nominal
    float       rssi();                            // dBm (ultima lectura)
    uint32_t    frames();                          // tramas / paquetes / palabras recibidos
    bool        setExternalHz(uint32_t hz);        // un programa externo (GPredict por CAT) manda la frecuencia ya con Doppler; manda 15 s sobre el calculo propio
    bool        externalActive();
    uint32_t    currentDownHz();                   // sintonizada si se escucha; si no, la nominal de la bajada del transmisor elegido
    void        fine(int deltaHz);                 // ajuste fino manual (error del oscilador del satelite)
    int         fineHz();
    void        nextLoraPreset();                  // SF / BW siguientes
    void        loraText(char* out, size_t n);     // "SF10 BW125 CR5"
    void        clearLog();

    size_t      logCount();
    bool        logLine(size_t newestFirst, char* out, size_t n, int* rssi = nullptr);
    void        cwLine(char* out, size_t n);       // texto CW en curso (ultimos caracteres)
    uint32_t    eventSeq();                        // cambia con cada linea nueva (para el evento BLE)

}

#endif
