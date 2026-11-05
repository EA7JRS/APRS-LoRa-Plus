/* Copyright (C) 2026 Manuel - EA7JRS
 * CAT por WiFi: servidor TCP compatible con el modelo "NET rigctl" (modelo 2) de Hamlib, para que WSJT-X, fldigi, GPredict, rigctl...
 * traten al tracker como una radio. Usa la misma red WiFi que KISS por WiFi (la de Actualizacion de firmware) y el mismo
 * enlace: kiss_net.cpp lo mantiene y llama a begin()/end()/poll() de este modulo. Protocolo en include/cat_proto.h (probado en PC).
 * Part of AIS/LoRa APRS Tracker - Manuel Canca EA7JRS (based on LoRa APRS Tracker by Ricardo Guzman CA2RXU, GPL-3.0)
 */
#ifndef CAT_NET_H_
#define CAT_NET_H_

#include <Arduino.h>
#include "kiss_net.h"

namespace CAT_NET {

    void begin();                       // el enlace WiFi esta arriba: abre el servidor TCP (si CAT esta activado)
    void end();                         // el enlace se ha perdido o se apaga: cierra el servidor y los clientes
    void poll();                        // cada pasada del bucle principal con el enlace arriba: acepta clientes y atiende comandos
    void onRx(int rssiDbm);             // paquete recibido: guarda el RSSI para el nivel STRENGTH
    void usbPoll();                     // CAT por el puerto serie USB (si Config.cat.usb): llamar desde loop(); tambien atiende "APRS-INFO?"
    bool usbActive();
    uint8_t clientCount();              // clientes conectados ahora

}

#endif
