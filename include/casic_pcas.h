/*
 * Ordenes NMEA propietarias $PCAS (CASIC) del Quectel L76K (chip AT6558R) y compatibles (ATGM336H, AT6558).
 * Funciones puras, sin dependencias de Arduino: se pueden probar en un PC (tools/test_casic.cpp).
 *
 * Ordenes usadas (verificadas en la documentacion del fabricante / Waveshare / CASIC protocol):
 *   $PCAS01,br      velocidad del puerto serie: 0=4800 1=9600 2=19200 3=38400 4=57600 5=115200
 *   $PCAS02,ms      periodo de medida: 1000, 500, 250 o 200 ms (el L76K llega a 5 Hz)
 *   $PCAS03,...     frases NMEA y su frecuencia (cada n fixes, 0 = apagada): GGA,GLL,GSA,GSV,RMC,VTG,ZDA,ANT,DHV,LPS,res,res,UTC,GST
 *   $PCAS04,modo    constelaciones: 1=GPS 2=BDS 3=GPS+BDS 4=GLONASS 5=GPS+GLONASS 6=BDS+GLONASS 7=las tres (QZSS siempre activo)
 *   $PCAS10,n       reinicio: 0 caliente, 1 templado, 2 frio, 3 de fabrica
 * El L76K no soporta Galileo ni SBAS. Estado de antena: $GPTXT,01,01,01,ANTENNA OPEN|OK|SHORT
 *
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#ifndef CASIC_PCAS_H_
#define CASIC_PCAS_H_

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace Casic {

    // Bits de constelacion (los mismos que GPS_Utils::GNSS_*)
    enum : uint8_t { GNSS_GPS = 1, GNSS_GALILEO = 2, GNSS_BEIDOU = 4, GNSS_GLONASS = 8, GNSS_QZSS = 16, GNSS_SBAS = 32 };
    const uint8_t SUPPORTED_MASK = GNSS_GPS | GNSS_BEIDOU | GNSS_GLONASS;      // lo que el L76K permite elegir

    enum Antenna : uint8_t { ANT_UNKNOWN = 0, ANT_OK = 1, ANT_OPEN = 2, ANT_SHORT = 3 };

    // "$cuerpo*CS\r\n" con CS = XOR de todos los caracteres entre '$' y '*'. Devuelve los bytes escritos (0 si no cabe)
    inline size_t build(const char* body, char* out, size_t cap) {
        uint8_t cs = 0;
        size_t  n = 0;
        for (const char* c = body; *c; c++) { cs ^= (uint8_t)*c; n++; }
        if (n + 7 > cap) return 0;
        return (size_t)snprintf(out, cap, "$%s*%02X\r\n", body, cs);
    }

    // Mascara -> modo $PCAS04 (0 si no hay ninguna constelacion valida)
    inline int constellationMode(uint8_t mask) {
        return ((mask & GNSS_GPS) ? 1 : 0) | ((mask & GNSS_BEIDOU) ? 2 : 0) | ((mask & GNSS_GLONASS) ? 4 : 0);
    }

    // Se quedan solo las constelaciones que admite el L76K; si no queda ninguna, GPS + BeiDou (valor de fabrica del modulo)
    inline uint8_t sanitizeMask(uint8_t mask) {
        mask &= SUPPORTED_MASK;
        return mask ? mask : (uint8_t)(GNSS_GPS | GNSS_BEIDOU);
    }

    // Periodo pedido (ms) -> periodo admitido por $PCAS02 (200, 250, 500 o 1000)
    inline int rateMs(int requested) {
        if (requested < 225) return 200;
        if (requested < 375) return 250;
        if (requested < 750) return 500;
        return 1000;
    }

    // Cuerpo de $PCAS03. lean = true: GGA + RMC + GSV + ANT (lo que usa el tracker; menos trafico por el puerto serie);
    // lean = false: GGA, GLL, GSA, GSV, RMC, VTG, ZDA y ANT (todas, como de fabrica). La GSV baja de frecuencia si hay mas de 1 fix por segundo
    inline void pcas03Body(bool lean, int periodMs, char* out, size_t cap) {
        const int gsv = (periodMs >= 1000) ? 1 : 5;
        if (lean) snprintf(out, cap, "PCAS03,1,0,0,%d,1,0,0,1,0,0,,,0,0", gsv);
        else      snprintf(out, cap, "PCAS03,1,1,1,%d,1,1,1,1,0,0,,,0,0", gsv);
    }

    // Texto de $GPTXT ("ANTENNA OPEN" / "ANTENNA OK" / "ANTENNA SHORT") -> estado
    inline uint8_t antennaFromText(const char* text) {
        if (!text) return ANT_UNKNOWN;
        if (strstr(text, "ANTENNA OPEN"))  return ANT_OPEN;
        if (strstr(text, "ANTENNA SHORT")) return ANT_SHORT;
        if (strstr(text, "ANTENNA OK"))    return ANT_OK;
        return ANT_UNKNOWN;
    }

    // Periodo de medida que exige subir de 9600 a 115200 baudios (con la salida reducida, a 2 Hz o mas)
    inline bool needsFastBaud(int periodMs, uint32_t currentBaud) {
        return periodMs <= 500 && currentBaud < 38400;
    }

}

#endif
