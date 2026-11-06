/* Copyright (C) 2026 Manuel - EA7JRS
 * Servidor de rotor por WiFi: protocolo "NET rotctl" de Hamlib (modelo 2) para que GPredict (u otro programa) mande al tracker el
 * acimut y la elevacion. El tracker los reenvia a su rotor (rotctld / GS-232 / app) con los desfases, el montaje movil y la
 * compensacion de inclinacion. Comparte el enlace WiFi con KISS y CAT: kiss_net.cpp lo mantiene y llama a begin()/end()/poll().
 * Protocolo en include/rot_proto.h (probado en PC).
 */
#ifndef ROTOR_NET_H_
#define ROTOR_NET_H_

#include <Arduino.h>

namespace ROTOR_NET {

    void begin();                       // el enlace WiFi esta arriba: abre el servidor TCP (si esta activado)
    void end();
    void poll();
    uint8_t clientCount();

}

#endif
