/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ tracker.
 * Licensed under the GNU General Public License, version 3 or later.
 *
 * Modo SSDV: emision y recepcion de imagenes JPEG en paquetes SSDV (UKHAS) con el SSDVClient de RadioLib 8.0.x.
 * El origen de la imagen a emitir puede ser un patron de prueba, una URL por WiFi, una subida por BLE o una camara OV2640.
 */
#ifndef SSDV_UTILS_H_
#define SSDV_UTILS_H_

#include <Arduino.h>

namespace SSDV_Utils {

    void begin();                                   // entra en el modo SSDV: construye el cliente y arranca la radio (RX)
    void end();                                     // sale del modo SSDV: restaura la radio del perfil
    void loop();                                    // bucle: emite los paquetes pendientes y recibe/decodifica

    bool startTx();                                 // carga la imagen del origen configurado y empieza a emitirla; false si no hay imagen
    void stopTx();                                  // detiene la emision en curso
    bool busy();                                    // true si esta emitiendo

    const char* status();                           // texto de estado para la pantalla / la app
    uint16_t txTotalPackets();                      // paquetes de la imagen en curso
    uint16_t txSentPackets();                       // paquetes ya emitidos
    uint16_t rxImageCount();                        // imagenes recibidas completas en esta sesion
    uint16_t rxPacketCount();                       // paquetes validos recibidos de la imagen en curso

    // Subida de la imagen a emitir desde la app por BLE (origen = 2)
    bool uploadBegin(size_t totalLen);              // reserva el buffer; false si no cabe
    bool uploadChunk(const uint8_t* data, size_t len);  // anade un trozo
    bool uploadEnd();                               // cierra la subida; true si el tamano cuadra
    size_t uploadReceived();                        // bytes recibidos hasta ahora

}

#endif
