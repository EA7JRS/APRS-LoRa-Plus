/*
 * Control de rotor de antenas para el modo SAT: el tracker calcula azimut / elevacion del satelite elegido y los envia al rotor.
 *   - WiFi / TCP: cliente de "rotctld" de Hamlib (puerto 4533 por defecto; ordenes "P az el" y "p")
 *   - Bluetooth SPP: controlador con protocolo Yaesu GS-232 ("Waaa eee"), solo en placas con Bluetooth clasico (ESP32 original)
 *   - BLE: la app Android recibe el evento "rot" (az / el) y lo retransmite al rotor (ver docs/BLE_CONTROL.md)
 *
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#ifndef ROTOR_UTILS_H_
#define ROTOR_UTILS_H_

#include <Arduino.h>

namespace ROTOR_Utils {

    enum State : uint8_t { ST_OFF, ST_WAIT, ST_CONNECTING, ST_OK, ST_ERROR };

    void        begin();                    // al entrar en el modo SAT
    void        loop();                     // cada vuelta: calcula el objetivo (1 vez por segundo) y se lo pasa a la tarea de comunicacion
    bool        target(float& az, float& el, bool& tracking);   // ultimo objetivo calculado (tracking = el satelite esta sobre el horizonte)
    State       state();
    const char* stateText();                // "OFF", "WiFi...", "Connecting", "OK", mensaje de error...
    bool        actual(float& az, float& el);                   // posicion real que devuelve el rotor (false = no se conoce)
    bool        enabled();
    uint32_t    targetSeq();
    int         source();                   // fuente activa: 0 satelite, 1 objetivo APRS de la brujula, 2 objetivo fijo (-1 = ninguna activa en este modo)
    const char* targetName();               // nombre del objetivo (satelite, indicativo o nombre guardado)
    void        setAttitude(float pitchDeg, float rollDeg);        // medida del inclinometro del soporte (cabeceo + = morro arriba, alabeo + = lado derecho abajo); caduca a los 2 s
    bool        tiltInfo(float& pitchDeg, float& rollDeg);         // true si la ultima orden se corrigio con esa inclinacion
    bool        mobileInfo(float& absoluteAz, float& headingDeg);   // montaje movil: azimut absoluto y rumbo del vehiculo usados (el azimut ordenado es relativo)
    double      targetDistKm();             // distancia al objetivo terrestre (0 = satelite o desconocida)                // aumenta con cada objetivo nuevo (para los eventos BLE)

}

#endif
