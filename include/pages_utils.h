/* Copyright (C) 2026 Ricardo Guzman - CA2RXU
 * Copyright (C) 2026 Manuel - EA7JRS (modificaciones y ampliaciones)
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

#ifndef PAGES_UTILS_H_
#define PAGES_UTILS_H_

#include <Arduino.h>
#include <APRSPacketLib.h>
#include "display.h"            // puede anular HAS_APRS_PAGES en pantallas TFT

#ifdef HAS_APRS_PAGES

// Interfaz de pantallas APRS (estilo Meshtastic) en la pantalla principal del Tracker.
// Cabecera: % bateria + titulo centrado. Pie: barra de iconos que se oculta a los 3 s.
// Paginas: MAIN, MESSAGES, MORSE (CW), LAST APRS, STATIONS, GPS, COMPASS, ALTIMETER, TELEMETRY, CLOCK, LORA, SYSTEM.
//   Pulsacion corta = pagina siguiente (o siguiente opcion si el submenu esta abierto)
//   Pulsacion larga = abre el submenu de la pagina (o ejecuta la opcion marcada)
//   Doble pulsacion = cierra el submenu; si no hay submenu abre el menu clasico de configuracion
enum {
    PAGES_CMD_NONE = 0,
    PAGES_CMD_BEACON,           // enviar baliza ahora
    PAGES_CMD_PROFILE,          // siguiente perfil de indicativo
    PAGES_CMD_MENU,             // menu clasico de configuracion
    PAGES_CMD_DISPLAY_OFF,      // apagar la pantalla
    PAGES_CMD_GPS,              // GPS encendido/apagado a mano
    PAGES_CMD_REPLY             // responder con teclado: abre la escritura de mensaje con el indicativo puesto
};

namespace PAGES_Utils {

    void    setup();
    void    update();                   // cada vuelta del bucle: muestreo de altitud, presion y rumbo
    void    show();                     // dibuja la pagina actual (pantalla principal, menuDisplay 0)
    void    showSplash(const String& version, const String& callsign);   // pantalla de bienvenida
    void    showWifiAp(const char* ssid, const char* password, const char* ip, int clients, int idleSecsLeft);   // punto de acceso WEB-CONF (idleSecsLeft < 0: sin cuenta atras)
    uint8_t selectMode();                // tras la bienvenida: APRS LoRa / APRS FSK / MORSE (pulsacion corta = siguiente, larga = aceptar)
    void    next();                     // pulsacion corta
    void    previous();
    int     longPress();                // pulsacion larga: abre submenu o ejecuta; devuelve un PAGES_CMD_*
    bool    back();                     // true si cerro el submenu

    // Modo de baliza manual: no se envian balizas automaticas hasta pedirlas con "Send beacon"
    void    requestBeacon();            // baliza pedida a mano (la deja pasar aunque el modo sea manual)
    bool    beaconAllowed();            // false: hay que descartar el envio automatico
    void    beaconSent();               // se ha transmitido la baliza
    bool    wantsFastRefresh();         // true en la brujula (refresco ~5 Hz)

    uint8_t unreadCount();                      // mensajes recibidos sin leer
    const char* replyCallsign();                // destinatario para PAGES_CMD_REPLY
    void    onMessageReceived(const char* call, const char* text);
    void    onMessageSent(const char* call, const char* text, uint16_t ackId);     // ackId > 0 si se pidio confirmacion
    void    onAckReceived(const char* call, uint16_t ackId);
    void    onAckFailed(const char* call, uint16_t ackId);
    void    onMessagesCleared();

    void    onAprsPacket(const APRSPacket& packet, uint32_t nowMs);     // paquete APRS recibido
    void    onTx(int bytes);            // paquete transmitido (bytes en el aire)
    void    onRx(int bytes);            // trama LoRa recibida (cualquiera), para el tiempo en el aire
    void    onEmergencyGesture();       // 5 pulsaciones cortas o SOS en morse: inicia (o cancela) la baliza de emergencia
    bool    emergencyFlash();           // alarma de emergencia sonando o SOS propio: estroboscopio del flash blanco
    bool    emergencyActive();          // emergencia en curso: no se duerme el GPS
    bool    alarmLed(bool& on);         // true: una alarma de emergencia controla el LED (on = fase del parpadeo)
    void    onDigipeat();               // paquete repetido por el digipeater

}

#endif

#endif
