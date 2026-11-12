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

#ifndef GPS_UTILS_H_
#define GPS_UTILS_H_

#include <Arduino.h>


#ifdef GPS_RESET
    #ifndef GPS_RESET_ACTIVE
        #define GPS_RESET_ACTIVE LOW        // nivel que mantiene el receptor en reset (board_pinout.h puede cambiarlo)
    #endif
#endif

namespace GPS_Utils {

    void    setup();
    void    calculateDistanceCourse(const String& callsign, double checkpointLatitude, double checkPointLongitude);
    void    getData();
    void    setDateFromData();
    void    calculateDistanceTraveled();
    void    calculateHeadingDelta(int speed);
    void    checkStartUpFrames();
    String  getCardinalDirection(float course);

    // Velocidad y rumbo estables (con GPS_SPEED_FILTER; si no, devuelven el valor crudo del GPS)
    void    updateSpeedFilter();
    float   getSpeedKmph();
    float   getSpeedKnots();
    float   getCourseDeg();
    bool    isMoving();

    // Ultima posicion valida (con GPS_LAST_POSITION)
    void    loadLastPosition();
    void    updateLastPosition();
    bool    hasLastPosition();
    double  getLastLat();
    double  getLastLng();
    bool    hasLastAltitude();
    float   getLastAltitude();

    // Altitud: fusionada barometro + GPS con GPS_BARO_ALTITUDE; si no, la del GPS
    float   getAltitudeMeters();
    float   getAltitudeFeet();

    // Receptor GNSS: 0 sin respuesta o sin configurar, 1 u-blox (responde a UBX), 2 NMEA sin UBX (L76K u otro). Se fija al arrancar
    uint8_t gpsModule();
    bool    utcMsOfDay(uint32_t& ms);           // milisegundos del dia UTC del GPS; con PPS a 1 Hz alineados con su flanco (~1 ms) (EA7JRS)
    bool    ppsLocked();                        // llega el PPS del receptor a 1 Hz (placas con GPS_PPS)
    bool    gpsIsCasic();                       // true si el receptor es CASIC (Quectel L76K y compatibles, ordenes $PCAS)
    uint8_t antennaStatus();                    // L76K: 0 desconocido, 1 OK, 2 abierta (sin antena), 3 cortocircuito
    void    reapplyAfterWake();                 // L76K: reconfigura el receptor tras volver a alimentarlo (modo eco)
    bool    gpsConfigurable();                  // true si la placa configura el receptor (GPS_CONFIG_CONSTELLATIONS)
    bool    aidWrite(const uint8_t* data, size_t length);   // A-GPS desde la app: mensajes UBX-MGA en bruto al receptor (solo u-blox) (EA7JRS)
    size_t  aidRoom();                                       // hueco en el buffer de A-GPS
    void    aidPump();                                       // pasa el buffer a la UART sin bloquear (cada vuelta del bucle)
    void    aidClear();

    // Constelaciones y SBAS (solo con GPS_CONFIG_CONSTELLATIONS); ajustables en caliente desde la pantalla GPS y guardadas en la configuracion (Config.gps)
    enum : uint8_t { GNSS_GPS = 1, GNSS_GALILEO = 2, GNSS_BEIDOU = 4, GNSS_GLONASS = 8, GNSS_QZSS = 16, GNSS_SBAS = 32 };
    #ifdef GPS_CONFIG_CONSTELLATIONS
        uint8_t gnssMask();
        bool    gnssMaskValid(uint8_t mask);
        int8_t  gnssSet(uint8_t newMask);       // 1 aplicado, 0 combinacion no valida, -1 sin respuesta del receptor
        int     satsInView();                   // -1 si aun no hay frases GSV
    #endif

}

#endif