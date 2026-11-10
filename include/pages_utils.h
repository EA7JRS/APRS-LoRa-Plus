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
#include "pages_core.h"

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
    bool    fmDialActive();             // T-TWR: el encoder sintoniza (pagina FM a la vista, sin submenu)

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
    // Para la app (EA7JRS): alarma recibida (devuelve si la pantalla de alarma esta abierta; kind = PAGES_Core::AlarmKind,
    // msg = llego en un mensaje, seq cambia con cada alarma nueva), silenciarla, y estado de la baliza SOS / EAI remota
    bool    alarmInfo(const char*& call, const char*& text, uint8_t& kind, bool& msg, uint16_t& seq, bool& sounding);
    void    alarmSilence();
    uint8_t sosInfo(uint32_t& leftS, uint16_t& sent, const char*& by);  // 0 nada, 1 cuenta atras, 2 en curso; leftS solo con EAI remota
    void    sosStop();
    void    aisTarget(const char* id, bool hasPos, double lat, double lon, float speedKmh, float course, const char* comment, bool open);   // barco AIS como estacion (brujula, rotor)
    void    aisAlarm(const char* id, const char* detail, bool distress, bool hasPos, double lat, double lon);       // riesgo de colision o socorro AIS
    void    onDigipeat();               // paquete repetido por el digipeater
    void    sendCw(uint8_t index);      // modo MORSE: emite el mensaje CW guardado numero index (0-4)
    bool    cwSendText(const char* text);   // modo MORSE: emite un texto libre (app por BLE)
    void    cwListenSession();              // escucha CW por RF (boton o app por BLE); bloquea hasta salir
    bool    headingForRotor(float& trueHeadingDeg);   // rumbo verdadero del vehiculo: curso GPS en marcha; parado, magnetometro; si no hay, el ultimo curso (30 s)
    bool    aprsTarget(double& lat, double& lon, double& altM, char* name, size_t nameSize);
    bool    lastHeardTarget(double& lat, double& lon, double& altM, char* name, size_t nameSize);    // rotor, fuente 4: ultima estacion oida con posicion (filtro y antiguedad de la WebUI)   // estacion APRS elegida como destino de la brujula (GO TO) con posicion; para el rotor
    // Control remoto completo por BLE (app Android): cualquier opcion de menu por nombre y espejo de la pantalla
    int     remoteAct(const char* name, const char** error);    // devuelve un PAGES_CMD_* o -1 con *error
    void    forEachAction(void (*callback)(uint8_t page, const char* pageTitle, const char* name, const char* label, bool allowed, void* ctx), void* ctx);
    uint8_t     currentPage();
    uint8_t     pageCount();
    const char* pageTitleOf(uint8_t page);
    bool        selectPage(uint8_t page);
    uint32_t    msgSeq();               // sube con cada cambio del registro de mensajes
    uint8_t     msgCount();
    uint8_t     msgUnread();
    const PAGES_Core::MsgEntry* msgAt(uint8_t position);    // 0 = el mas reciente
    uint8_t     msgStateAt(uint8_t position);               // PAGES_Core::MsgState
    uint8_t     heardCount();
    uint32_t    heardPackets();
    const PAGES_Core::Station* heardAt(uint8_t position);   // 0 = la mas reciente
    int         sendMessage(const char* to, const char* text, bool wantAck);   // encola un mensaje APRS; devuelve el ackId (0 sin ack) o -1 si ya estaba en cola
    bool        iaqValue(float& index, const char** label);     // calidad del aire estimada (BME680); false si aun no hay
    uint8_t     screenPage();           // pagina de la ultima pantalla dibujada
    const char* screenTitle();
    const char* screenRight();
    const char* screenRow(uint8_t r);
    uint8_t     screenRows();
    uint32_t    screenAgeMs();
    void    cwRemoteSession();          // modo MORSE: manipulador conectado al movil (eventos BLE_Control::KeyEvent)

}

#endif

#endif
