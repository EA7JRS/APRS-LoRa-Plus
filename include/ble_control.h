/*
 * Control por Bluetooth LE: segundo servicio GATT (junto al KISS) con ordenes y eventos JSON para la app APRS LoRa+ de Android.
 * Estado, sensores, GNSS, historial DAPNET, salida NMEA y cambio de modo / perfil. Protocolo en docs/BLE_CONTROL.md
 *
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#ifndef BLE_CONTROL_H_
#define BLE_CONTROL_H_

#include <Arduino.h>
#include "ais_core.h"

class NimBLEServer;

namespace BLE_Control {

    void setup(NimBLEServer* server);                   // crea el servicio (llamar desde BLE_Utils::setup, antes de anunciar)
    // Varios equipos a la vez (EA7JRS): cada enlace lleva su identificador de conexion (movil con la app + plotter, p. ej.)
    void onConnect(uint16_t connHandle);
    void onMtu(uint16_t connHandle, uint16_t mtu);                           // MTU negociado: tamano de los trozos de las notificaciones
    void onDisconnect(uint16_t connHandle);
    void onSecurity(uint16_t connHandle, bool authenticated);                // emparejamiento con PIN completado (cifrado + MITM); sin el no se emite nada al movil
    bool authenticated();
    void loop();                                        // al final de cada vuelta del bucle principal: ordenes, envios periodicos, DAPNET
    void nmea(const char* text, size_t length);         // frase NMEA para el movil (solo si esta suscrito)
    int  takeBootMode();                                // modo pedido por el movil para este arranque (-1 = ninguno); lo borra
    void setBootMode(int mode);                         // modo para el siguiente arranque (una vez)

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
    bool     netRequest(const char* what);
    void     aisEvent(const AisCore::Vessel& v);        // barco nuevo o actualizado (receptor AIS): evento "ais"              // pide a la app que baje algo de internet: "wx" (tiempo de TELEMETRY) o "sat" (archivo de satelites); false sin app

    // Escucha CW por RF pedida desde la app (orden "cwrx"): la sesion bloquea el bucle; el texto sale como evento "cwrx" y la
    // app la para con "cwrx" s=stop, que se mira al llegar (la cola de ordenes no se procesa mientras dura) (EA7JRS)
    bool     cwRxStopRequested();
    void     cwRxEvent(bool on, char c, float dbm, float snr, int wpm);
    void     rfEvent(char state, int rssi = 0, float snr = 0);   // luces de la app: 't' empieza a emitir, 'i' termina, 'r' trama recibida

}

#endif
