/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ tracker.
 * Licensed under the GNU General Public License, version 3 or later.
 *
 * Capa de compatibilidad con RadioLib. En la 8.0.0 desaparecen las versiones de begin() / beginFSK() / beginLRFHSS() con la
 * lista de argumentos (ahora structs de configuracion), los ajustes de banda / tasa / desviacion pasan a enteros en Hz / bps
 * y setTCXO() toma un enum en lugar de voltios. Estas funciones conservan las mismas llamadas con float del codigo y eligen la
 * forma segun la version, asi que el firmware compila igual con RadioLib 7.8.x y 8.0.x. El TCXO solo existe en el SX126x; en el
 * SX127x el argumento equivalente de begin()/beginFSK() es otra cosa (ganancia / OOK), asi que se resuelve con dos sobrecargas
 * por rango (Rank0 / Rank1) que escogen la forma correcta para cada chip sin tocar el codigo que llama. (EA7JRS)
 */
#ifndef RADIOLIB_COMPAT_H_
#define RADIOLIB_COMPAT_H_

#include <RadioLib.h>
#include <math.h>

#if RADIOLIB_VERSION >= 0x08000000
    #define RLCOMPAT_V8 1
#else
    #define RLCOMPAT_V8 0
#endif

namespace RLCompat {

    // Seleccion de sobrecarga por rango: Rank1 se prefiere, Rank0 es la de reserva (los chips sin TCXO).
    struct Rank0 {};
    struct Rank1 : Rank0 {};

#if RLCOMPAT_V8
    // Voltaje del TCXO: en 8.0.0 setTCXO() toma RadioLibTCXOVoltage_t (escalones fijos); se elige el mas cercano por debajo
    inline RadioLibTCXOVoltage_t tcxoVoltage(float v) {
        if (v < 0.5f)  return VoltageNone;
        if (v < 1.65f) return Voltage1V6;
        if (v < 1.75f) return Voltage1V7;
        if (v < 2.1f)  return Voltage1V8;
        if (v < 2.3f)  return Voltage2V2;
        if (v < 2.55f) return Voltage2V4;
        if (v < 2.85f) return Voltage2V7;
        if (v < 3.15f) return Voltage3V0;
        return Voltage3V3;
    }
    inline uint32_t hzFromMHz(float mhz)  { return (uint32_t)lroundf(mhz * 1e6f); }
    inline uint32_t hzFromKHz(float khz)  { return (uint32_t)lroundf(khz * 1e3f); }
    inline uint32_t bpsFromKbps(float kb) { return (uint32_t)lroundf(kb * 1e3f); }

    // setTCXO() solo existe en el SX126x: la sobrecarga Rank1 casa ahi; en el SX127x cae en la Rank0 (no hace nada)
    template <class T> inline auto applyTcxoDisp(T& r, float v, Rank1) -> decltype(r.setTCXO(VoltageNone), void()) {
        if (v > 0.5f) r.setTCXO(tcxoVoltage(v));
    }
    template <class T> inline void applyTcxoDisp(T&, float, Rank0) {}
    template <class T> inline void applyTcxo(T& r, float v) { applyTcxoDisp(r, v, Rank1{}); }
#else
    template <class T> inline void applyTcxo(T&, float) {}   // en 7.x el TCXO va en el propio begin()
#endif

    // LoRa: frecuencia en MHz, ancho en kHz; tcxoV en voltios (0 = cristal)
    template <class T>
    inline int16_t beginLoRa(T& r, float freqMHz, float bwKHz, uint8_t sf, uint8_t cr, uint8_t sync, int8_t power, uint16_t preamble, float tcxoV) {
#if RLCOMPAT_V8
        applyTcxo(r, tcxoV);
        ConfigLoRa_t c;
        c.frequency = hzFromMHz(freqMHz); c.bandwidth = hzFromKHz(bwKHz);
        c.spreadingFactor = sf; c.codingRate = cr; c.syncWord = sync; c.power = power; c.preambleLength = preamble;
        return r.begin(c);
#else
        return r.begin(freqMHz, bwKHz, sf, cr, sync, power, preamble, tcxoV);
#endif
    }

    // begin() con los valores por defecto del chip (como radio.begin(freq)); solo fija frecuencia y TCXO
    template <class T>
    inline int16_t beginLoRaDefault(T& r, float freqMHz, float tcxoV) {
#if RLCOMPAT_V8
        applyTcxo(r, tcxoV);
        ConfigLoRa_t c; c.frequency = hzFromMHz(freqMHz);
        return r.begin(c);
#else
        (void)tcxoV;
        return r.begin(freqMHz);
#endif
    }

    // FSK: tasa en kbps, desviacion y ancho de recepcion en kHz
    template <class T>
    inline int16_t beginFsk(T& r, float freqMHz, float brKbps, float devKHz, float rxBwKHz, int8_t power, uint16_t preamble, float tcxoV) {
#if RLCOMPAT_V8
        applyTcxo(r, tcxoV);
        ConfigFSK_t c;
        c.frequency = hzFromMHz(freqMHz); c.bitRate = bpsFromKbps(brKbps);
        c.frequencyDeviation = hzFromKHz(devKHz); c.receiverBandwidth = hzFromKHz(rxBwKHz);
        c.power = power; c.preambleLength = preamble;
        return r.beginFSK(c);
#else
        return beginFskDisp(r, freqMHz, brKbps, devKHz, rxBwKHz, power, preamble, tcxoV, Rank1{});
#endif
    }

#if !RLCOMPAT_V8
    // SX126x: el 7.o argumento de beginFSK() es el TCXO. SX127x: no lo tiene (ahi es OOK), asi que se llama con 6 argumentos.
    template <class T> inline auto beginFskDisp(T& r, float f, float br, float dev, float bw, int8_t p, uint16_t pre, float tcxoV, Rank1)
        -> decltype(r.setTCXO(tcxoV), int16_t()) {
        return r.beginFSK(f, br, dev, bw, p, pre, tcxoV);
    }
    template <class T> inline int16_t beginFskDisp(T& r, float f, float br, float dev, float bw, int8_t p, uint16_t pre, float, Rank0) {
        return r.beginFSK(f, br, dev, bw, p, pre);
    }
#endif

    // LR-FHSS (solo SX126x): bw y cr son constantes RADIOLIB_SX126X_LR_FHSS_*
    template <class T>
    inline int16_t beginLrFhss(T& r, float freqMHz, uint8_t bw, uint8_t cr, bool narrowGrid, int8_t power, float tcxoV) {
#if RLCOMPAT_V8
        applyTcxo(r, tcxoV);
        ConfigLRFHSS_t c;
        c.frequency = hzFromMHz(freqMHz); c.bandwidth = bw; c.codingRate = cr; c.narrowGrid = narrowGrid; c.power = power;
        return r.beginLRFHSS(c);
#else
        return r.beginLRFHSS(freqMHz, bw, cr, narrowGrid, power, tcxoV, false);
#endif
    }

    // Frecuencia: en 8.0.0 setFrequency() toma uint32_t en Hz; en 7.x era float en MHz
    template <class T> inline int16_t setFreqMHz(T& r, float mhz) {
#if RLCOMPAT_V8
        return r.setFrequency(hzFromMHz(mhz));
#else
        return r.setFrequency(mhz);
#endif
    }

    // Ajustes que en 8.0.0 pasan a enteros (Hz / bps); en 7.x siguen en float (kHz / kbps)
    template <class T> inline int16_t setBandwidthKHz(T& r, float khz)   {
#if RLCOMPAT_V8
        return r.setBandwidth(hzFromKHz(khz));
#else
        return r.setBandwidth(khz);
#endif
    }
    template <class T> inline int16_t setBitRateKbps(T& r, float kbps)   {
#if RLCOMPAT_V8
        return r.setBitRate(bpsFromKbps(kbps));
#else
        return r.setBitRate(kbps);
#endif
    }
    template <class T> inline int16_t setFreqDevKHz(T& r, float khz)     {
#if RLCOMPAT_V8
        return r.setFrequencyDeviation(hzFromKHz(khz));
#else
        return r.setFrequencyDeviation(khz);
#endif
    }
    template <class T> inline int16_t setRxBandwidthKHz(T& r, float khz) {
#if RLCOMPAT_V8
        return r.setRxBandwidth(hzFromKHz(khz));
#else
        return r.setRxBandwidth(khz);
#endif
    }
    template <class T> inline int16_t setTcxoVolts(T& r, float v) {
#if RLCOMPAT_V8
        return r.setTCXO(tcxoVoltage(v));
#else
        return r.setTCXO(v);
#endif
    }

}  // namespace RLCompat

#endif
