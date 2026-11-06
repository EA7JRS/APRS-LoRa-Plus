/*
 * Control por Bluetooth LE: segundo servicio GATT (junto al KISS) con ordenes y eventos JSON para la app APRS LoRa+ de Android.
 * Estado, sensores, GNSS, historial DAPNET, salida NMEA y cambio de modo / perfil. Protocolo en docs/BLE_CONTROL.md
 *
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#ifndef BLE_CONTROL_H_
#define BLE_CONTROL_H_

#include <Arduino.h>

class NimBLEServer;

namespace BLE_Control {

    void setup(NimBLEServer* server);                   // crea el servicio (llamar desde BLE_Utils::setup, antes de anunciar)
    void onConnect();
    void onMtu(uint16_t mtu);                           // MTU negociado: tamano de los trozos de las notificaciones
    void onDisconnect();
    void loop();                                        // al final de cada vuelta del bucle principal: ordenes, envios periodicos, DAPNET
    void nmea(const char* text, size_t length);         // frase NMEA para el movil (solo si esta suscrito)
    int  takeBootMode();                                // modo pedido por el movil para este arranque (-1 = ninguno); lo borra

    // Manipulador remoto (modo MORSE): el movil manda cada pulsar / soltar con su marca de tiempo; el tracker lo reproduce con
    // un retardo fijo que absorbe las variaciones del enlace BLE, asi sale la manipulacion tal cual
    struct KeyEvent {
        uint32_t t;                                     // milisegundos del movil
        uint32_t arrivalMs;                             // millis() del tracker al llegar
        bool     down;
    };
    bool     takeKeyEvent(KeyEvent& event);             // siguiente evento recibido (false = ninguno)
    bool     keyEventWaiting();
    uint16_t keyDelayMs();                              // retardo de reproduccion pedido por el movil (orden "kcfg")
    bool     linkUp();                                  // movil conectado

}

#endif
