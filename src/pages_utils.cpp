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

#include "pages_utils.h"

#ifdef HAS_APRS_PAGES

#include <TinyGPS++.h>
#include <TimeLib.h>
#include <WiFi.h>
#include <stdarg.h>
#include <vector>
#include <SPIFFS.h>
#include "battery_utils.h"
#include "baro_utils.h"
#include "configuration.h"
#include "board_pinout.h"
#include "pages_core.h"
#include "power_utils.h"
#include "msg_utils.h"
#include "gps_utils.h"
#include "imu_utils.h"
#include "mag_utils.h"
#include "rtc_utils.h"
#include "wx_utils.h"
#include "utils.h"
#include "lora_utils.h"
#include "cw_morse.h"
#include "wspr.h"
#include "ft_beacon.h"
#include "fst4w.h"
#include "lrfhss.h"
#include "aprs_icons.h"
#include <logger.h>
#include "wmm.h"
#include "notification_utils.h"
#include "dapnet_utils.h"
#include "station_utils.h"
#include "sleep_utils.h"
#include "keypad_fh2.h"

#ifndef PAGES_GYRO_SIGN
    #define PAGES_GYRO_SIGN     -1      // sentido inicial del giroscopio (se corrige solo al girar con la brujula valida)
#endif

extern Configuration        Config;
extern logging::Logger      logger;
extern TinyGPSPlus          gps;
extern HardwareSerial       gpsSerial;
extern int                  menuDisplay;
extern std::vector<String>  loadedAPRSMessages;
extern uint8_t              loraIndex;
extern LoraType             *currentLoRaType;
extern uint32_t             displayTime;
extern bool                 displayState;
extern bool                 bluetoothActive;
extern bool                 bluetoothConnected;
extern bool                 digipeaterActive;
extern bool                 sosActive;
extern bool                 gpsIsActive;
extern bool                 batteryConnected;
extern bool                 messageLed;
extern int                  wxModuleType;
extern Beacon               *currentBeacon;
extern uint8_t              myBeaconsIndex;
extern uint32_t             lastTxTime;
extern uint32_t             txInterval;
extern bool                 smartBeaconActive;
extern bool                 sendUpdate;
extern bool                 keyboardConnected;

using namespace PAGES_Core;

// Grado Celsius: en OLED (Adafruit GFX) el simbolo ° es el glifo 0xF7 de la fuente clasica; en TFT se deja "C"
#ifdef HAS_TFT
    #define DEG_C "C"
#else
    #define DEG_C "\xF7" "C"
#endif

namespace {

    enum Page : uint8_t { PAGE_MAIN, PAGE_MSG, PAGE_CW, PAGE_CWB, PAGE_WSPR, PAGE_LRF, PAGE_DAP, PAGE_LAST, PAGE_LIST, PAGE_GPS, PAGE_COMPASS, PAGE_ALT, PAGE_WX, PAGE_CLOCK, PAGE_LINK, PAGE_BOARD, PAGE_COUNT };

    // FT8, FT4 y FST4W comparten la pagina, el estado y el menu de la baliza WSPR (solo uno de los dos modos esta activo a la vez)
    bool ftMode() { return Config.operatingMode == MODE_FT8 || Config.operatingMode == MODE_FT4 || Config.operatingMode == MODE_FST4W; }

    // El modo MORSE tiene una unica pagina (MORSE); los modos APRS tienen las demas y no la de MORSE
    bool pageVisible(uint8_t p) {
        if (Config.operatingMode == MODE_MORSE) return p == PAGE_CW || p == PAGE_CWB;       // manipulador/escucha y baliza CW
        if (Config.operatingMode == MODE_WSPR || ftMode()) return p == PAGE_WSPR;
        if (Config.operatingMode == MODE_LRFHSS) return p == PAGE_LRF;
        if (Config.operatingMode == MODE_DAPNET) return p == PAGE_DAP;
        return p != PAGE_CW && p != PAGE_CWB && p != PAGE_WSPR && p != PAGE_LRF && p != PAGE_DAP;
    }
    bool aprsMode() { return Config.operatingMode != MODE_MORSE && Config.operatingMode != MODE_WSPR && Config.operatingMode != MODE_LRFHSS && Config.operatingMode != MODE_DAPNET && !ftMode(); }

    const char* const PAGE_TITLES[PAGE_COUNT] = {
        "MAIN", "MESSAGES", "MORSE", "CW BEACON", "WSPR", "LR-FHSS", "DAPNET", "LAST APRS", "STATIONS", "GPS", "COMPASS", "ALTIMETER", "TELEMETRY", "CLOCK", "LORA", "SYSTEM"
    };

    // Opciones del submenu (pulsacion larga). Las acciones del Tracker (baliza, perfil, menu) las ejecuta el teclado.
    enum Command : uint8_t {
        C_BACK, C_BEACON, C_BEACON_MODE, C_PROFILE, C_MENU, C_DISPLAY_OFF, C_GPS, C_LAST_NEXT, C_LAST_PREV, C_LAST_VIEW, C_GOTO, C_LIST_BROWSE, C_LIST_SORT, C_LIST_FILTER, C_LIST_CLEAR,
        C_COMPASS_NEXT, C_COMPASS_MODE, C_COMPASS_NORTH, C_COMPASS_DECL, C_COMPASS_RESET, C_DECL_MODE, C_DECL_UP1, C_DECL_DN1, C_DECL_UP2, C_DECL_DN2,
        C_GPS_COORD, C_GPS_UNITS, C_GPS_SBAS, C_GPS_GNSS, C_G_GPS, C_G_GAL, C_G_BDS, C_G_GLO, C_G_QZSS,
        C_MSG_OPEN, C_MSG_BROWSE, C_MSG_READALL, C_MSG_DELALL, C_MSG_REPLY, C_MSG_QUICK, C_MSG_DELETE,
        C_COMPASS_TARGET, C_ALT_SPAN, C_ALT_RESET, C_ALT_QNH, C_CLK_ZONE, C_CLK_FORMAT, C_CLK_SYNC, C_ZN_MODE, C_ZN_UP, C_ZN_DN, C_Q_AUTO, C_Q_UP1, C_Q_DN1, C_Q_UP2, C_Q_DN2,
        C_SOS_SEND, C_SOS_CANCEL, C_LORA_PROFILE, C_LORA_POWER, C_LORA_RESET, C_LORA_HEARD, C_PW_SHOW, C_PW_UP, C_PW_DN,
        C_QUICK0, C_QUICK1, C_QUICK2, C_QUICK3, C_QUICK4, C_QUICK5, C_QUICK6, C_QUICK7,
        C_CW0, C_CW1, C_CW2, C_CW3, C_CW4, C_CW_LISTEN, C_CW_TRAIN_LISTEN, C_CW_TRAIN_KEY, C_CW_CLEAR, C_CW_BEACON, C_WSPR_TOGGLE, C_LRF_TOGGLE, C_WSPR_TUNE, C_CWB_NOW, C_CWB_TUNE, C_DAP_OLDER, C_DAP_NEWER, C_DAP_CLEAR, C_DAP_MON,
        C_MODE_CHANGE
    };
    struct Item { const char* label; Command cmd; };

    // Respuestas rapidas (se envian con peticion de confirmacion)
    const uint8_t QUICK_COUNT = 8;
    const char* const QUICK_TEXT[QUICK_COUNT] = { "QSL", "73", "TNX", "WAIT", "CALL ME", "QSY CQ VHF", "QSY CQ UHF", "QSY RPT LOCAL" };

    const Item ITEMS_MAIN[]    = { {"Send beacon", C_BEACON}, {"Beacon mode", C_BEACON_MODE}, {"Next profile", C_PROFILE},
                                   {"Display off", C_DISPLAY_OFF}, {"Change mode", C_MODE_CHANGE}, {"Config menu", C_MENU}, {"Back", C_BACK} };
    const Item ITEMS_MSG_EMPTY[] = { {"Back", C_BACK} };
    const Item ITEMS_QUICK[]   = { {QUICK_TEXT[0], C_QUICK0}, {QUICK_TEXT[1], C_QUICK1}, {QUICK_TEXT[2], C_QUICK2}, {QUICK_TEXT[3], C_QUICK3},
                                   {QUICK_TEXT[4], C_QUICK4}, {QUICK_TEXT[5], C_QUICK5}, {QUICK_TEXT[6], C_QUICK6}, {QUICK_TEXT[7], C_QUICK7}, {"Back", C_BACK} };
    const Item ITEMS_LAST[]    = { {"Next view", C_LAST_VIEW}, {"Older station", C_LAST_NEXT}, {"Newer station", C_LAST_PREV}, {"GO TO", C_GOTO}, {"Back", C_BACK} };
    const Item ITEMS_LIST[]    = { {"Browse", C_LIST_BROWSE}, {"Sort", C_LIST_SORT}, {"Filter", C_LIST_FILTER}, {"Clear list", C_LIST_CLEAR}, {"Back", C_BACK} };
    const Item ITEMS_GNSS[]    = { {"GPS", C_G_GPS}, {"Galileo", C_G_GAL}, {"BeiDou", C_G_BDS}, {"GLONASS", C_G_GLO}, {"QZSS", C_G_QZSS}, {"Back", C_BACK} };
    const Item ITEMS_COMPASS[] = { {"Target", C_COMPASS_TARGET}, {"Next station", C_COMPASS_NEXT}, {"Heading", C_COMPASS_MODE}, {"North", C_COMPASS_NORTH},
                                   {"Declination", C_COMPASS_DECL}, {"Reset align", C_COMPASS_RESET}, {"Back", C_BACK} };
    const Item ITEMS_DECL[]    = { {"Mode", C_DECL_MODE}, {"+0.5", C_DECL_UP1}, {"-0.5", C_DECL_DN1}, {"+5", C_DECL_UP2}, {"-5", C_DECL_DN2}, {"Back", C_BACK} };
    #ifdef GPS_BARO_ALTITUDE
        const Item ITEMS_ALT[]     = { {"Span", C_ALT_SPAN}, {"Reset profile", C_ALT_RESET}, {"QNH", C_ALT_QNH}, {"Back", C_BACK} };
        const Item ITEMS_QNH[]     = { {"QNH", C_Q_AUTO}, {"+1", C_Q_UP1}, {"-1", C_Q_DN1}, {"+0.1", C_Q_UP2}, {"-0.1", C_Q_DN2}, {"Back", C_BACK} };
    #else
        const Item ITEMS_ALT[]     = { {"Span", C_ALT_SPAN}, {"Reset profile", C_ALT_RESET}, {"Back", C_BACK} };
        const Item ITEMS_QNH[]     = { {"Back", C_BACK} };
    #endif
    const Item ITEMS_CLOCK[]   = { {"Zone", C_CLK_ZONE}, {"Format", C_CLK_FORMAT}, {"Sync from GPS", C_CLK_SYNC}, {"Back", C_BACK} };
    const Item ITEMS_ZONE[]    = { {"Zone", C_ZN_MODE}, {"+0.5h", C_ZN_UP}, {"-0.5h", C_ZN_DN}, {"Back", C_BACK} };
    const Item ITEMS_LORA[]    = { {"Next profile", C_LORA_PROFILE}, {"Power", C_LORA_POWER}, {"Reset counters", C_LORA_RESET}, {"Heard list", C_LORA_HEARD}, {"Back", C_BACK} };
    const Item ITEMS_POWER[]   = { {"Power", C_PW_SHOW}, {"+1 dBm", C_PW_UP}, {"-1 dBm", C_PW_DN}, {"Back", C_BACK} };
    const Item ITEMS_BACK[]    = { {"Back", C_BACK} };

    bool            listBrowse          = false;    // STATIONS: cursor activo
    uint8_t         listCursor          = 0;        // posicion en la vista ordenada y filtrada
    char            listCursorCall[11]  = "";
    uint8_t         listSort            = SORT_RECENT;
    uint8_t         listFilter          = FILTER_ALL;
    uint8_t         unitSystem          = UNITS_METRIC;     // distancias, velocidades y altitudes en todas las pantallas
    uint8_t         coordFormat         = COORD_DD;
    uint32_t        gpsOffSinceMs       = 0;
    uint8_t         zoneMode            = ZONE_AUTO_EU;     // CLOCK: zona horaria
    int8_t          zoneHalfHours       = 0;                // desfase manual en pasos de 30 min
    bool            clock24h            = true;
    uint32_t        lastGpsTimeMs       = 0;                // ultima vez que el GPS dio hora valida
    uint8_t         altSpanMode         = 0;        // 0 completo, 1 ultima mitad, 2 ultimos 10 min
    bool            compassTarget       = false;
    uint8_t         compassMode         = 0;        // 0 automatico, 1 solo magnetometro, 2 solo GPS
    bool            northTrue           = true;     // true: norte verdadero; false: norte magnetico
    bool            declAuto            = true;     // declinacion por modelo WMM (si no hay modelo o posicion, se usa la manual)
    int16_t         declManualTenths    = 0;        // decimas de grado, positiva al este
    float           declCache           = 0;
    bool            declCacheOk         = false;
    uint32_t        declCacheMs         = 0;
    bool            arrivedNotified     = false;
    Item            gpsItems[8];
    uint8_t         gpsItemCount        = 0;
    bool            beaconManualMode    = false;    // sin balizas automaticas hasta pulsar "Send beacon"
    bool            beaconRequested     = false;

    // Baliza de emergencia: cuenta atras de 5 s y envio cada 30 s hasta cancelar
    enum SosState : uint8_t { SOS_IDLE, SOS_COUNTDOWN, SOS_ACTIVE };
    const uint32_t  SOS_COUNTDOWN_MS    = 5000;
    const uint32_t  SOS_PERIOD_MS       = 30000;
    const uint32_t  SOS_RETRY_MS        = 5000;         // sin posicion todavia: reintenta pronto
    uint8_t         sosState            = SOS_IDLE;
    uint32_t        sosStartMs          = 0;
    uint32_t        sosNextMs           = 0;
    uint8_t         sosLastTick         = 0;
    uint16_t        sosSent             = 0;
    Item            mainItems[10];
    uint8_t         mainItemCount       = 0;

    enum MsgMode : uint8_t { MM_LIST, MM_BROWSE, MM_DETAIL };
    enum SubmenuId : uint8_t { SM_PAGE, SM_QUICK, SM_GNSS, SM_DECL, SM_QNH, SM_ZONE, SM_POWER };

    MessageLog      msgLog;
    uint8_t         msgMode             = MM_LIST;
    uint8_t         msgCursor           = 0;        // 0 = el mas reciente
    uint8_t         submenuId           = SM_PAGE;
    Item            listItems[5];                   // submenu de la lista de mensajes (cambia si hay o no mensajes)
    Item            detailItems[4];                 // submenu del detalle de un mensaje (depende del teclado)
    uint8_t         detailCount         = 0;
    bool            logDirty            = false;
    uint32_t        logDirtyMs          = 0;
    char            toastText[26]       = "";
    uint32_t        toastUntil          = 0;
    char            replyTo[11]         = "";

    Item            wsprItems[5];
    Item            lrfItems[3];
    Item            dapItems[6];
    char            dapMonLabel[24];
    uint8_t         dapIndex = 0;                // 0 = mensaje mas reciente
    uint32_t        dapSeen = 0;                 // contador de mensajes ya atendidos
    uint32_t        dapNewMs = 0;                // millis() del ultimo mensaje nuevo
    char            wsprToggleLabel[20];
    char            lrfToggleLabel[20];
    bool            lrfhssOn = true;             // baliza LR-FHSS activa (modo LR-FHSS propio, independiente del modo WSPR y de su baliza)
    bool            wsprOn = true;               // baliza WSPR activa (en modo WSPR arranca activa)
    int8_t          wsprProgress = -1;           // simbolo en curso (-1 = sin transmitir)
    uint32_t        wsprNextAt = 0;              // millis() del proximo inicio previsto (0 = desconocido)
    uint16_t        wsprTxCount = 0;
    char            wsprStatus[22] = "";
    uint32_t        wsprStatusAt = 0;            // millis() en que se fijo "Sent #n" (se borra a los 10 s para que se vea la cuenta atras)
    uint16_t        lrfhssTxCount = 0;           // balizas LR-FHSS enviadas
    uint32_t        lrfhssNextAt = 0;            // millis() de la proxima baliza LR-FHSS prevista (0 = desconocido)
    uint32_t        lrfhssBlockUntil = 0;        // no volver a emitir antes de este millis() (evita repetir la misma ranura)
    uint32_t        lrfhssStatusUntil = 0;       // millis() hasta el que se muestra lrfhssStatus (0 = permanente mientras dure la causa)
    char            lrfhssStatus[22] = "";
    Item            cwItems[CW_MESSAGE_COUNT + 7];
    char            cwBeaconLabel[20];
    Item            cwbItems[5];
    char            cwbToggleLabel[20];
    uint16_t        cwBeaconTxCount = 0;         // balizas CW completas enviadas desde el arranque
    char            cwBeaconStatus[22] = "";     // ultimo resultado ("Sent #n" / "Aborted")
    uint32_t        cwBeaconStatusAt = 0;
    bool            cwBeaconOn = false;          // baliza CW autonoma activa (solo modo MORSE)
    bool            cwBeaconInit = false;
    uint32_t        cwBeaconNext = 0;
    char            cwLabels[CW_MESSAGE_COUNT][22];

    const Item* itemsFor(uint8_t pageIndex, uint8_t& count) {
        #define PAGES_ITEMS(array) count = sizeof(array) / sizeof(array[0]); return array
        if (submenuId == SM_QUICK) { PAGES_ITEMS(ITEMS_QUICK); }
        if (submenuId == SM_GNSS) { PAGES_ITEMS(ITEMS_GNSS); }
        if (submenuId == SM_DECL) { PAGES_ITEMS(ITEMS_DECL); }
        if (submenuId == SM_QNH) { PAGES_ITEMS(ITEMS_QNH); }
        if (submenuId == SM_ZONE) { PAGES_ITEMS(ITEMS_ZONE); }
        if (submenuId == SM_POWER) { PAGES_ITEMS(ITEMS_POWER); }
        switch (pageIndex) {
            case PAGE_MAIN: {
                mainItemCount = 0;
                if (sosState != SOS_IDLE) mainItems[mainItemCount++] = Item{"Cancel SOS", C_SOS_CANCEL};
                for (uint8_t i = 0; i < sizeof(ITEMS_MAIN) / sizeof(ITEMS_MAIN[0]) - 1; i++) mainItems[mainItemCount++] = ITEMS_MAIN[i];
                if (sosState == SOS_IDLE) mainItems[mainItemCount++] = Item{"Send SOS", C_SOS_SEND};
                mainItems[mainItemCount++] = Item{"Back", C_BACK};
                count = mainItemCount;
                return mainItems;
            }
            case PAGE_MSG:
                if (msgMode == MM_DETAIL) {
                    detailCount = 0;
                    if (keyboardConnected) detailItems[detailCount++] = Item{"Reply", C_MSG_REPLY};
                    detailItems[detailCount++] = Item{"Quick reply", C_MSG_QUICK};
                    detailItems[detailCount++] = Item{"Delete", C_MSG_DELETE};
                    detailItems[detailCount++] = Item{"Back", C_BACK};
                    count = detailCount;
                    return detailItems;
                }
                if (msgLog.count() == 0) { PAGES_ITEMS(ITEMS_MSG_EMPTY); }
                listItems[0] = Item{"Read / reply", C_MSG_OPEN};          // abre ya el mensaje sin leer mas reciente (o el ultimo recibido)
                listItems[1] = Item{"Browse list", C_MSG_BROWSE};
                listItems[2] = Item{"Mark all read", C_MSG_READALL};
                listItems[3] = Item{"Delete all", C_MSG_DELALL};
                listItems[4] = Item{"Back", C_BACK};
                count = 5;
                return listItems;
            case PAGE_WSPR:
                snprintf(wsprToggleLabel, sizeof(wsprToggleLabel), "Beacon: %s", wsprOn ? "ON" : "OFF");
                wsprItems[0] = Item{wsprToggleLabel, C_WSPR_TOGGLE};
                count = 1;
                if (!ftMode()) wsprItems[count++] = Item{"Tune carrier", C_WSPR_TUNE};
                wsprItems[count++] = Item{"Change mode", C_MODE_CHANGE};
                wsprItems[count++] = Item{"Back", C_BACK};
                return wsprItems;
            case PAGE_CWB:
                snprintf(cwbToggleLabel, sizeof(cwbToggleLabel), "Beacon: %s", cwBeaconOn ? "ON" : "OFF");
                cwbItems[0] = Item{cwbToggleLabel, C_CW_BEACON};
                cwbItems[1] = Item{"Send now", C_CWB_NOW};
                cwbItems[2] = Item{"Tune carrier", C_CWB_TUNE};
                cwbItems[3] = Item{"Change mode", C_MODE_CHANGE};
                cwbItems[4] = Item{"Back", C_BACK};
                count = 5;
                return cwbItems;
            case PAGE_DAP:
                dapItems[0] = Item{"Older message", C_DAP_OLDER};
                dapItems[1] = Item{"Newer message", C_DAP_NEWER};
                dapItems[2] = Item{"Clear list", C_DAP_CLEAR};
                snprintf(dapMonLabel, sizeof(dapMonLabel), "Monitor all: %s", Config.dapnet.monitorAll ? "ON" : "OFF");
                dapItems[3] = Item{dapMonLabel, C_DAP_MON};
                dapItems[4] = Item{"Change mode", C_MODE_CHANGE};
                dapItems[5] = Item{"Back", C_BACK};
                count = 6;
                return dapItems;
            case PAGE_LRF:
                snprintf(lrfToggleLabel, sizeof(lrfToggleLabel), "Beacon: %s", lrfhssOn ? "ON" : "OFF");
                lrfItems[0] = Item{lrfToggleLabel, C_LRF_TOGGLE};
                lrfItems[1] = Item{"Change mode", C_MODE_CHANGE};
                lrfItems[2] = Item{"Back", C_BACK};
                count = 3;
                return lrfItems;
            case PAGE_CW:
                for (uint8_t i = 0; i < CW_MESSAGE_COUNT; i++) {
                    {
                        String bare = currentBeacon->callsign;
                        int dash = bare.indexOf('-');
                        if (dash > 0) bare = bare.substring(0, dash);
                        String message = Config.cw.messages[i];
                        message.replace("{CALL}", bare);
                        message.toUpperCase();
                        snprintf(cwLabels[i], sizeof(cwLabels[i]), "%u %.14s", i + 1, message.c_str());
                    }
                    cwItems[i] = Item{cwLabels[i], (Command)(C_CW0 + i)};
                }
                cwItems[CW_MESSAGE_COUNT] = Item{"Listen CW (RF)", C_CW_LISTEN};
                cwItems[CW_MESSAGE_COUNT + 1] = Item{"Trainer: listen", C_CW_TRAIN_LISTEN};
                cwItems[CW_MESSAGE_COUNT + 2] = Item{"Trainer: keying", C_CW_TRAIN_KEY};
                cwItems[CW_MESSAGE_COUNT + 3] = Item{"Clear text", C_CW_CLEAR};
                snprintf(cwBeaconLabel, sizeof(cwBeaconLabel), "CW beacon: %s", cwBeaconOn ? "ON" : "OFF");
                cwItems[CW_MESSAGE_COUNT + 4] = Item{cwBeaconLabel, C_CW_BEACON};
                cwItems[CW_MESSAGE_COUNT + 5] = Item{"Change mode", C_MODE_CHANGE};
                cwItems[CW_MESSAGE_COUNT + 6] = Item{"Back", C_BACK};
                count = CW_MESSAGE_COUNT + 7;
                return cwItems;
            case PAGE_LAST:     PAGES_ITEMS(ITEMS_LAST);
            case PAGE_LIST:     PAGES_ITEMS(ITEMS_LIST);
            case PAGE_GPS:
                gpsItemCount = 0;
                #ifdef HAS_GPS_CTRL
                    gpsItems[gpsItemCount++] = Item{"GPS on/off", C_GPS};
                #endif
                gpsItems[gpsItemCount++] = Item{"Coords", C_GPS_COORD};
                gpsItems[gpsItemCount++] = Item{"Units", C_GPS_UNITS};
                #ifdef GPS_CONFIG_CONSTELLATIONS
                    gpsItems[gpsItemCount++] = Item{"SBAS", C_GPS_SBAS};
                    gpsItems[gpsItemCount++] = Item{"Constellations", C_GPS_GNSS};
                #endif
                gpsItems[gpsItemCount++] = Item{"Back", C_BACK};
                count = gpsItemCount;
                return gpsItems;
            case PAGE_COMPASS:  PAGES_ITEMS(ITEMS_COMPASS);
            case PAGE_ALT:      PAGES_ITEMS(ITEMS_ALT);
            case PAGE_CLOCK:    PAGES_ITEMS(ITEMS_CLOCK);
            case PAGE_LINK:     PAGES_ITEMS(ITEMS_LORA);
            default:            PAGES_ITEMS(ITEMS_BACK);
        }
        #undef PAGES_ITEMS
    }

    float currentDeclination(bool* fromModel);

    // Algunas opciones muestran la accion que haran
    const char* labelOf(const Item& item) {
        switch (item.cmd) {
            case C_BEACON_MODE: return beaconManualMode ? "To auto mode" : "To manual mode";
            case C_GPS:         return gpsIsActive ? "Turn GPS off" : "Turn GPS on";
            case C_PW_SHOW: {
                static char buffer[24];
                snprintf(buffer, sizeof(buffer), "Power: %d dBm", LoRa_Utils::getPower());
                return buffer;
            }
            case C_CLK_FORMAT:  return clock24h ? "Format: 24h" : "Format: 12h";
            case C_ZN_MODE: {
                static char buffer[24];
                if (zoneMode == ZONE_AUTO_EU) snprintf(buffer, sizeof(buffer), "Zone: auto EU");
                else if (zoneMode == ZONE_UTC) snprintf(buffer, sizeof(buffer), "Zone: UTC");
                else snprintf(buffer, sizeof(buffer), "Zone: UTC%+.1f", zoneHalfHours / 2.0f);
                return buffer;
            }
            case C_ALT_SPAN: {
                static const char* const names[3] = { "Span: full", "Span: half", "Span: 10 min" };
                return names[altSpanMode % 3];
            }
            #ifdef GPS_BARO_ALTITUDE
            case C_Q_AUTO: {                                    // muestra el estado y, al ejecutarlo, vuelve a automatico
                static char buffer[24];
                float value = BARO_Utils::getQnh();
                if (value <= 0.0f) snprintf(buffer, sizeof(buffer), "Auto (GPS) ---");
                else snprintf(buffer, sizeof(buffer), BARO_Utils::isManualQnh() ? "Manual %.1f" : "Auto %.1f", value);
                return buffer;
            }
            #endif
            case C_COMPASS_TARGET: return compassTarget ? "Target: on" : "Target: off";
            case C_COMPASS_MODE: {
                static const char* const names[3] = { "Heading: auto", "Heading: MAG", "Heading: GPS" };
                return names[compassMode % 3];
            }
            case C_COMPASS_NORTH: return northTrue ? "North: true" : "North: mag";
            case C_DECL_MODE: {
                static char buffer[24];
                bool viaModel = false;
                float value = currentDeclination(&viaModel);
                if (declAuto && !viaModel) snprintf(buffer, sizeof(buffer), "Auto n/a %+.1f", value);
                else snprintf(buffer, sizeof(buffer), "%s %+.1f", declAuto ? "Auto" : "Manual", value);
                return buffer;
            }
            case C_GPS_COORD: {
                static const char* const names[COORD_COUNT] = { "Coords: DD", "Coords: DDM", "Coords: DMS" };
                return names[coordFormat % COORD_COUNT];
            }
            case C_GPS_UNITS: {
                static const char* const names[UNITS_COUNT] = { "Units: metric", "Units: imperial", "Units: nautical" };
                return names[unitSystem % UNITS_COUNT];
            }
            #ifdef GPS_CONFIG_CONSTELLATIONS
            case C_GPS_SBAS:    return (GPS_Utils::gnssMask() & GPS_Utils::GNSS_SBAS) ? "SBAS: on" : "SBAS: off";
            case C_G_GPS:       return (GPS_Utils::gnssMask() & GPS_Utils::GNSS_GPS) ? "GPS: on" : "GPS: off";
            case C_G_GAL:       return (GPS_Utils::gnssMask() & GPS_Utils::GNSS_GALILEO) ? "Galileo: on" : "Galileo: off";
            case C_G_BDS:       return (GPS_Utils::gnssMask() & GPS_Utils::GNSS_BEIDOU) ? "BeiDou: on" : "BeiDou: off";
            case C_G_GLO:       return (GPS_Utils::gnssMask() & GPS_Utils::GNSS_GLONASS) ? "GLONASS: on" : "GLONASS: off";
            case C_G_QZSS:      return (GPS_Utils::gnssMask() & GPS_Utils::GNSS_QZSS) ? "QZSS: on" : "QZSS: off";
            #endif
            case C_LIST_SORT: {
                static const char* const names[SORT_COUNT] = { "Sort: recent", "Sort: nearest", "Sort: name" };
                return names[listSort % SORT_COUNT];
            }
            case C_LIST_FILTER: {
                static const char* const names[FILTER_COUNT] = { "Filter: all", "Filter: pos", "Filter: mobile", "Filter: fixed", "Filter: wx", "Filter: digi" };
                return names[listFilter % FILTER_COUNT];
            }
            default:            return item.label;
        }
    }

    const uint8_t   LIST_ROWS           = 6;
    const int       ROW_Y0              = 11;
    const int       ROW_PITCH           = 9;

    HeardList       heard;
    AltProfile      altProfile;
    PressureTrend   pressureTrend;
    IaqEstimator    iaq;
    float           frostT18 = 0, frostTd18 = 0;    // T y Td a las 18:00 locales (helada nocturna)
    time_t          frostSampleT = 0;
    HeadingFusion   fusion;

    uint8_t         page                = PAGE_MAIN;
    uint8_t         lastIndex           = 0;        // LAST APRS: 0 = la mas reciente
    uint8_t         lastView            = 0;        // 0 = resumen, 1 = posicion y comentario
    char            lastCall[11]        = "";       // estacion en pantalla cuando no es la mas reciente
    char            targetCall[11]      = "";       // destino de la brujula (vacio = la mas reciente)
    uint32_t        lastSampleMs        = 0;
    uint32_t        lastFusionMs        = 0;
    // Alarma de emergencia (Mic-E Emergency, SOS/EMERGENCY/MAYDAY, MOB / MAN OVERBOARD)
    const uint32_t  ALARM_BEEP_MS       = 2500;
    const uint32_t  ALARM_MAX_MS        = 600000UL;     // el buzzer se calla solo a los 10 min
    const uint32_t  ALARM_REPEAT_MS     = 600000UL;     // misma estacion: no vuelve a sonar hasta pasados 10 min sin alarma suya
    bool            alarmShown          = false;        // pantalla de alarma visible
    bool            alarmSounding       = false;        // buzzer y LED activos
    uint8_t         alarmKind           = ALARM_NONE;
    char            alarmCall[11]       = "";
    char            alarmText[22]       = "";
    uint32_t        alarmMs             = 0;            // ultimo paquete de alarma recibido
    uint32_t        alarmStartMs        = 0;
    uint32_t        alarmBeepMs         = 0;
    uint32_t        alarmWakeMs         = 0;
    AirtimeTracker  airtime;
    uint32_t        rxCount             = 0;        // paquetes APRS validos recibidos desde el ultimo reinicio de contadores
    uint32_t        digiCount           = 0;
    uint32_t        txCount             = 0;
    uint32_t        lastTxMs            = 0;
    bool            haveTx              = false;
    uint32_t        lastRxMs            = 0;
    char            lastRxCall[11]      = "";
    bool            submenuOpen         = false;
    uint8_t         submenuSel          = 0;
    uint32_t        submenuMs           = 0;
    uint32_t        barUntil            = 0;        // la barra de iconos se ve hasta este instante
    const uint32_t  BAR_VISIBLE_MS      = 3000;
    const uint32_t  SUBMENU_TIMEOUT_MS  = 10000;

    // ---------- Dibujo ----------
    bool viewing() {
        return menuDisplay == 0;
    }

    void text(int x, int y, const char* s, uint8_t size = 1) {
        pagesText(x, y, s, size, false);
    }

    int rowLimit = 21;
    uint32_t stillSinceMs = 0;                  // desde cuando el equipo esta parado (0 = en movimiento o desconocido)
    bool     vsStopped = false;                 // parado el tiempo suficiente: la velocidad vertical se muestra como 0

    void row(uint8_t r, const char* format, ...) {
        char line[28];
        va_list args;
        va_start(args, format);
        vsnprintf(line, sizeof(line), format, args);
        va_end(args);
        line[rowLimit] = '\0';              // 21 columnas de 6 px caben en 128 px (17 si hay un icono a la derecha)
        text(0, ROW_Y0 + r * ROW_PITCH, line);
    }

    // Porcentaje de bateria para la cabecera (se refresca cada 2 s)
    const char* batteryText() {
        static char cached[8] = "";
        static uint32_t cachedMs = 0;
        if (cached[0] && millis() - cachedMs < 2000) return cached;
        cachedMs = millis();
        if (!batteryConnected) {
            snprintf(cached, sizeof(cached), "USB");
            return cached;
        }
        String volt = BATTERY_Utils::getBatteryInfoVoltage();
        #ifdef HAS_AXP2101
            String percent = POWER_Utils::getBatteryInfoCurrent();
        #else
            String percent = BATTERY_Utils::getPercentVoltageBattery(volt.toFloat());
        #endif
        percent.trim();
        snprintf(cached, sizeof(cached), "%s%%", percent.c_str());
        return cached;
    }

    // Cabecera: % de bateria a la izquierda, titulo centrado, derecha libre
    void header(const char* title, const char* right = nullptr) {
        pagesFillRect(0, 0, 128, 9, true);
        pagesText(2, 1, batteryText(), 1, true);
        pagesText((128 - 6 * (int)strlen(title)) / 2, 1, title, 1, true);
        if (sosState == SOS_ACTIVE && ((millis() / 500) & 1)) right = "SOS";       // emergencia en curso: parpadea en todas las paginas
        if (right) pagesText(128 - 2 - 6 * (int)strlen(right), 1, right, 1, true);
    }

    // Iconos de 9x8 px de la barra inferior
    void icon(uint8_t i, int x, int y, bool on) {
        switch (i) {
            case PAGE_MAIN:     // casa
                pagesDrawLine(x, y + 3, x + 4, y, on); pagesDrawLine(x + 4, y, x + 8, y + 3, on);
                pagesDrawRect(x + 1, y + 3, 7, 5, on); pagesDrawLine(x + 4, y + 5, x + 4, y + 7, on);
                break;
            case PAGE_MSG:      // sobre
                pagesDrawRect(x, y + 1, 9, 7, on);
                pagesDrawLine(x, y + 1, x + 4, y + 4, on); pagesDrawLine(x + 8, y + 1, x + 4, y + 4, on);
                break;
            case PAGE_CW:       // punto, raya, punto
                pagesFillRect(x, y + 3, 2, 2, on); pagesFillRect(x + 3, y + 3, 4, 2, on); pagesFillRect(x + 8, y + 3, 1, 2, on);
                pagesDrawLine(x, y + 6, x + 8, y + 6, on);
                break;
            case PAGE_CWB:      // punto emisor y dos ondas
                pagesFillCircle(x + 2, y + 4, 1, on);
                pagesDrawLine(x + 5, y + 2, x + 5, y + 6, on);
                pagesDrawLine(x + 8, y, x + 8, y + 8, on);
                break;
            case PAGE_DAP:      // buscapersonas: caja con pantalla y dos botones
                pagesDrawRect(x, y + 1, 9, 7, on);
                pagesDrawLine(x + 2, y + 3, x + 6, y + 3, on);
                pagesDrawLine(x + 2, y + 5, x + 4, y + 5, on);
                break;
            case PAGE_LAST:     // diana
                pagesDrawCircle(x + 4, y + 4, 4, on); pagesFillCircle(x + 4, y + 4, 1, on);
                break;
            case PAGE_LIST:     // lista
                for (int k = 0; k < 3; k++) { pagesFillRect(x, y + 1 + k * 3, 2, 2, on); pagesDrawLine(x + 4, y + 1 + k * 3, x + 8, y + 1 + k * 3, on); }
                break;
            case PAGE_GPS:      // chincheta
                pagesFillCircle(x + 4, y + 3, 3, on); pagesFillTriangle(x + 1, y + 4, x + 7, y + 4, x + 4, y + 7, on);
                pagesFillCircle(x + 4, y + 3, 1, !on);
                break;
            case PAGE_COMPASS:  // brujula
                pagesDrawCircle(x + 4, y + 4, 4, on); pagesFillTriangle(x + 4, y + 1, x + 3, y + 4, x + 5, y + 4, on);
                pagesDrawLine(x + 4, y + 4, x + 4, y + 6, on);
                break;
            case PAGE_ALT:      // montanas
                pagesDrawLine(x, y + 7, x + 3, y + 2, on); pagesDrawLine(x + 3, y + 2, x + 5, y + 5, on);
                pagesDrawLine(x + 5, y + 5, x + 6, y + 4, on); pagesDrawLine(x + 6, y + 4, x + 8, y + 7, on);
                pagesDrawLine(x, y + 7, x + 8, y + 7, on);
                break;
            case PAGE_WX:       // termometro
                pagesDrawRect(x + 3, y, 3, 5, on); pagesFillCircle(x + 4, y + 5, 2, on); pagesFillRect(x + 4, y + 2, 1, 4, on);
                break;
            case PAGE_CLOCK:    // reloj
                pagesDrawCircle(x + 4, y + 4, 4, on); pagesDrawLine(x + 4, y + 4, x + 4, y + 1, on); pagesDrawLine(x + 4, y + 4, x + 6, y + 5, on);
                break;
            case PAGE_LINK:     // barras de senal
                pagesFillRect(x, y + 5, 2, 3, on); pagesFillRect(x + 3, y + 3, 2, 5, on); pagesFillRect(x + 6, y, 2, 8, on);
                break;
            case PAGE_BOARD:    // chip
                pagesDrawRect(x + 2, y + 1, 5, 6, on);
                pagesDrawLine(x, y + 2, x + 1, y + 2, on); pagesDrawLine(x, y + 5, x + 1, y + 5, on);
                pagesDrawLine(x + 7, y + 2, x + 8, y + 2, on); pagesDrawLine(x + 7, y + 5, x + 8, y + 5, on);
                break;
            default: break;
        }
    }

    // Barra inferior de iconos: 12 paginas x 10 px
    void drawBar() {
        pagesFillRect(0, 54, 128, 10, false);
        pagesDrawLine(0, 54, 127, 54, true);
        uint8_t k = 0;
        for (uint8_t i = 0; i < PAGE_COUNT; i++) {
            if (!pageVisible(i)) continue;
            int x = 3 + k * 11;
            int w = 11;
            k++;
            if (i == page) pagesFillRect(x, 55, w, 9, true);
            icon(i, x + 1, 56, i != page);
        }
    }

    void drawSubmenu() {
        uint8_t count;
        const Item* items = itemsFor(page, count);
        const int pitch = 8, w = 104, x = 12;
        const uint8_t maxVisible = 6;
        uint8_t visible = count < maxVisible ? count : maxVisible;
        uint8_t first = 0;
        if (submenuSel >= maxVisible) first = submenuSel - maxVisible + 1;      // ventana que sigue al cursor
        int h = visible * pitch + 4;
        int y = (64 - h) / 2;
        if (y < 10) y = 10;
        pagesFillRect(x, y, w, h, false);
        pagesDrawRect(x, y, w, h, true);
        for (uint8_t i = 0; i < visible; i++) {
            uint8_t index = first + i;
            int rowY = y + 2 + i * pitch;
            if (index == submenuSel) {
                pagesFillRect(x + 2, rowY, w - 4, pitch, true);
                pagesText(x + 5, rowY, labelOf(items[index]), 1, true);
            } else {
                pagesText(x + 5, rowY, labelOf(items[index]), 1, false);
            }
        }
        if (first > 0) pagesText(x + w - 9, y + 2, "^", 1, submenuSel == first);
        if (first + visible < count) pagesText(x + w - 9, y + h - 10, "v", 1, submenuSel == first + visible - 1);
    }

    void polar(int cx, int cy, float radius, float deg, int& x, int& y) {
        float a = deg * DEG_TO_RAD;
        x = cx + (int)lroundf(radius * sinf(a));
        y = cy - (int)lroundf(radius * cosf(a));
    }

    // ---------- Datos ----------
    bool ownPosition(double& lat, double& lon) {
        if (gps.location.isValid()) {
            lat = gps.location.lat();
            lon = gps.location.lng();
            return true;
        }
        if (GPS_Utils::hasLastPosition()) {
            lat = GPS_Utils::getLastLat();
            lon = GPS_Utils::getLastLng();
            return true;
        }
        return false;
    }

    void fmtDist(double km, char* out, size_t size) {
        formatDistanceUnits(km, unitSystem, out, size);
    }

    const char* packetTypeName(int type) {
        switch (type) {
            case 0: return "POS";
            case 1: return "MSG";
            case 2: return "STAT";
            case 3: return "TELM";
            case 4: return "MIC-E";
            case 5: return "OBJ";
            default: return "?";
        }
    }

    char kindLetter(Kind kind) {
        switch (kind) {
            case KIND_FIXED:  return 'F';
            case KIND_MOBILE: return 'M';
            case KIND_OBJECT: return 'O';
            case KIND_WX:     return 'W';
            case KIND_DIGI:   return 'D';
            case KIND_IGATE:  return 'I';
            case KIND_MSG:    return 'm';
            case KIND_STATUS: return 's';
            case KIND_TELEMETRY: return 't';
            default:          return '?';
        }
    }

    bool altitudeAvailable() {
        #ifdef GPS_BARO_ALTITUDE
            if (BARO_Utils::altitudeValid()) return true;
        #endif
        return gps.altitude.isValid() && gps.altitude.age() < 10000 && gps.satellites.isValid() && gps.satellites.value() >= 4;
    }

    const char* altitudeSource() {
        #ifdef GPS_BARO_ALTITUDE
            if (BARO_Utils::altitudeValid()) return "BARO";
        #endif
        return "GPS";
    }

    // ---------- Paginas ----------
    // "2m10s", "45s", "1h05m"
    void formatShort(uint32_t seconds, char* out, size_t size) {
        if (seconds >= 3600) snprintf(out, size, "%uh%02um", (unsigned)(seconds / 3600), (unsigned)((seconds % 3600) / 60));
        else if (seconds >= 60) snprintf(out, size, "%um%02us", (unsigned)(seconds / 60), (unsigned)(seconds % 60));
        else snprintf(out, size, "%us", (unsigned)seconds);
    }

    // Coordenadas con 4 decimales (la pantalla GPS usa 6)
    void formatPosition(double lat, double lon, const char* state, char* out, size_t size) {
        snprintf(out, size, "%.4f %.4f %s", lat, lon, state);
    }

    void drawMain() {
        // modo de baliza (como en los Yaesu): M manual, A automatica, S SmartBeaconing
        const char* mode = beaconManualMode ? "M" : (smartBeaconActive ? "S" : "A");
        header(PAGE_TITLES[PAGE_MAIN], mode);
        char line[40];

        // fila 0: indicativo y simbolo a la izquierda, tiempo hasta la proxima baliza a la derecha
        snprintf(line, sizeof(line), "%.9s %c", currentBeacon->callsign.c_str(), currentBeacon->symbol.length() > 0 ? currentBeacon->symbol[0] : ' ');
        text(2, 11, line);
        char next[16];
        if (beaconManualMode) {
            snprintf(next, sizeof(next), "TX manual");
        } else if (sendUpdate) {
            snprintf(next, sizeof(next), "TX now");
        } else if (lastTxTime == 0) {
            snprintf(next, sizeof(next), "TX ---");
        } else {
            uint32_t interval = smartBeaconActive ? txInterval : (uint32_t)Config.nonSmartBeaconRate * 60UL * 1000UL;
            uint32_t elapsed = millis() - lastTxTime;
            char remaining[10];
            formatShort(elapsed >= interval ? 0 : (interval - elapsed) / 1000, remaining, sizeof(remaining));
            snprintf(next, sizeof(next), "TX%s%s", smartBeaconActive ? "<" : " ", remaining);    // con SmartBeaconing puede salir antes
        }
        text(128 - 2 - 6 * (int)strlen(next), 11, next);

        // fila 1: fecha y hora UTC
        time_t t = now();
        if (year(t) < 2024) snprintf(line, sizeof(line), "--- no time ---");
        else snprintf(line, sizeof(line), "%02d/%02d/%04d %02d:%02d UTC", day(t), month(t), year(t), hour(t), minute(t));
        text(2, 20, line);

        // fila 2: posicion y estado del GPS (FIX, ~ ultima posicion, SLEEP)
        double lat, lon;
        bool have = ownPosition(lat, lon);
        if (!have) {
            snprintf(line, sizeof(line), gpsIsActive ? "WAITING FOR GPS" : "GPS SLEEP");
        } else {
            const char* state = !gpsIsActive ? "SLEEP" : (gps.location.isValid() ? "FIX" : "~");
            formatPosition(lat, lon, state, line, sizeof(line));
        }
        text(2, 29, line);

        // fila 3: locator, banda LoRa y satelites con calidad HDOP
        const char* tag = "--";
        if (LoRa_Utils::isAisActive()) tag = "AIS";
        else switch (loraIndex) { case 0: tag = "Eu"; break; case 1: tag = "PL"; break; case 2: tag = "UK"; break; case 3: tag = "US"; break; default: break; }
        char locator[10] = "------";
        if (have) snprintf(locator, sizeof(locator), "%s", Utils::getMaidenheadLocator(lat, lon, 6));
        char quality = '?';
        if (gps.hdop.isValid()) quality = gps.hdop.hdop() > 5 ? 'X' : (gps.hdop.hdop() > 2 ? '-' : '+');
        if (gpsIsActive) snprintf(line, sizeof(line), "%s %s %02u%c", locator, tag, (unsigned)gps.satellites.value(), quality);
        else snprintf(line, sizeof(line), "%s %s --", locator, tag);
        text(2, 38, line);

        // fila 4: ultima estacion recibida
        if (lastRxCall[0]) {
            char age[8];
            formatAge(millis() - lastRxMs, age, sizeof(age));
            snprintf(line, sizeof(line), "Rx %.9s %s", lastRxCall, age);
        } else {
            snprintf(line, sizeof(line), "Rx ---");
        }
        text(2, 47, line);

        // fila 5: avisos (mensajes, correo Winlink) o altitud, velocidad y rumbo (con brujula en parado)
        if (msgLog.unread() > 0) snprintf(line, sizeof(line), "** NEW MSG: %u **", (unsigned)msgLog.unread());
        else if (MSG_Utils::getNumWLNKMails() > 0) snprintf(line, sizeof(line), "** WLNK MAIL: %d **", MSG_Utils::getNumWLNKMails());
        else if (gpsIsActive) {
            char altText[12], speedText[12];
            formatAltitudeUnits(GPS_Utils::getAltitudeMeters(), unitSystem, altText, sizeof(altText));
            formatSpeedUnits(GPS_Utils::getSpeedKmph(), unitSystem, speedText, sizeof(speedText));
            float courseOrHeading = GPS_Utils::isMoving() ? GPS_Utils::getCourseDeg()
                                  : (MAG_Utils::isHeadingValid() ? MAG_Utils::heading() : GPS_Utils::getCourseDeg());
            snprintf(line, sizeof(line), "A=%s %s %03.0f", altText, speedText, courseOrHeading);
        }
        else snprintf(line, sizeof(line), "A=--- GPS sleeping");
        text(2, 56, line);
    }

    // Brujula pequena hacia la estacion: norte arriba, o rumbo arriba si el GPS da un rumbo fiable
    void miniCompass(int cx, int cy, int r, float bearing) {
        bool headingUp = gpsIsActive && gps.course.isValid() && gps.speed.isValid() && GPS_Utils::getSpeedKmph() >= 3.0f;
        float heading = headingUp ? GPS_Utils::getCourseDeg() : 0.0f;
        pagesDrawCircle(cx, cy, r, true);
        int x, y, xs, ys;
        polar(cx, cy, r - 1, (float)bearing - heading, x, y);
        pagesDrawLine(cx, cy, x, y, true);
        polar(cx, cy, r - 5, (float)bearing - heading - 25, xs, ys);
        pagesDrawLine(x, y, xs, ys, true);
        polar(cx, cy, r - 5, (float)bearing - heading + 25, xs, ys);
        pagesDrawLine(x, y, xs, ys, true);
        text(cx - 2, cy + r + 2, headingUp ? "H" : "N");
    }

    // Icono APRS de 24x24 (tabla primaria o secundaria); con superposicion, la letra o el digito va en el centro
    bool drawAprsIcon(int x, int y, char symbol, char table) {
        if (symbol < APRS_ICON_FIRST || symbol >= APRS_ICON_FIRST + APRS_ICON_COUNT) return false;
        const uint8_t* bits = (table == '/' ? APRS_ICONS_PRIMARY : APRS_ICONS_SECONDARY)[symbol - APRS_ICON_FIRST];
        for (int row = 0; row < APRS_ICON_SIZE; row++) {
            uint32_t v = ((uint32_t)bits[row * 3] << 16) | ((uint32_t)bits[row * 3 + 1] << 8) | bits[row * 3 + 2];
            int col = 0;
            while (col < APRS_ICON_SIZE) {
                if (!((v >> (23 - col)) & 1)) { col++; continue; }
                int run = 1;
                while (col + run < APRS_ICON_SIZE && ((v >> (23 - col - run)) & 1)) run++;
                pagesFillRect(x + col, y + row, run, 1, true);
                col += run;
            }
        }
        if (table != '/' && table != '\\') {
            char o[2] = { table, '\0' };
            pagesFillRect(x + 8, y + 7, 8, 10, false);
            pagesText(x + 9, y + 8, o, 1, false);
        }
        return true;
    }

    void drawLast() {
        uint8_t total = heard.count();
        if (lastIndex >= total) lastIndex = 0;
        const Station* s = heard.byRecent(lastIndex);
        if (!s) {
            header(PAGE_TITLES[PAGE_LAST]);
            row(1, "No station heard yet");
            row(3, "Waiting for packets");
            row(4, "on %.3f MHz", Config.loraTypes[loraIndex].frequency / 1000000.0);
            return;
        }
        char counter[8];
        snprintf(counter, sizeof(counter), "%u/%u", (unsigned)(lastIndex + 1), (unsigned)total);
        header(PAGE_TITLES[PAGE_LAST], counter);

        char age[8];
        formatAge(millis() - s->lastMs, age, sizeof(age));
        row(0, "%-9s%12s", s->call, age);

        double myLat, myLon;
        bool haveMe = ownPosition(myLat, myLon);
        char distance[10] = "---";
        double bearing = 0;
        bool haveBearing = s->hasPosition && haveMe;
        if (haveBearing) {
            fmtDist(distanceKm(myLat, myLon, s->lat, s->lon), distance, sizeof(distance));
            bearing = bearingDeg(myLat, myLon, s->lat, s->lon);
        }

        const bool showIcon = lastView == 0 && s->symbol >= APRS_ICON_FIRST && s->symbol < APRS_ICON_FIRST + APRS_ICON_COUNT;
        if (lastView == 0) {
            if (s->kind == KIND_OBJECT && s->comment[0]) row(1, "OBJ %.17s", s->comment);
            else row(1, "%-7s %.13s", kindName(s->kind), s->device);
            if (!s->hasPosition) {
                row(2, "No position");
                if (showIcon) rowLimit = 17;
                row(3, "Last: %s", packetTypeName(s->type));
            } else {
                if (haveBearing) row(2, "D %s %s %03d", distance, cardinal16(bearing), (int)lround(bearing) % 360);
                else row(2, "D --- (no own GPS)");
                if (showIcon) rowLimit = 17;                           // filas 3 a 5: dejan libre la esquina inferior derecha (x >= 104) para el icono
                char speedText[12], altText[12];
                formatSpeedUnits(s->speed, unitSystem, speedText, sizeof(speedText));
                formatAltitudeUnits(s->altitude, unitSystem, altText, sizeof(altText));
                if (s->kind == KIND_MOBILE) row(3, "%s %03d A%s", speedText, s->course, altText);
                else row(3, "A%s", altText);
            }
            char smeter[10];
            sMeterText(s->rssi, smeter, sizeof(smeter));          // "S8", "S9", "S9+20"
            row(4, "S %s", smeter + 1);                           // S espacio valor
            row(5, "Pkts %u  %s", (unsigned)s->packets, s->status[0] ? s->status : packetTypeName(s->type));
            rowLimit = 21;
            if (showIcon) drawAprsIcon(104, 40, s->symbol, s->table);
            else if (haveBearing) miniCompass(114, 39, 9, (float)bearing);
        } else {
            if (s->hasPosition) {
                row(1, "%s", Utils::getMaidenheadLocator(s->lat, s->lon, 8));
                row(2, "%.4f %.4f", s->lat, s->lon);
            } else {
                row(1, "No position");
            }
            if (s->kind == KIND_WX && s->wx.valid) {
                char line[24];
                int n = 0;
                line[0] = '\0';
                if (s->wx.tempC != WX_NA) n += snprintf(line + n, sizeof(line) - n, "T%dC ", s->wx.tempC);
                if (s->wx.humidity != WX_NA) n += snprintf(line + n, sizeof(line) - n, "Hr%d%% ", s->wx.humidity);
                if (s->wx.pressureHpa != WX_NA) n += snprintf(line + n, sizeof(line) - n, "Pr%d", s->wx.pressureHpa);
                row(3, "%s", line);
                n = 0; line[0] = '\0';
                if (s->wx.windKmh != WX_NA) n += snprintf(line + n, sizeof(line) - n, "W%03d/%d ", s->wx.windDir == WX_NA ? 0 : s->wx.windDir, s->wx.windKmh);
                if (s->wx.gustKmh != WX_NA) n += snprintf(line + n, sizeof(line) - n, "G%d ", s->wx.gustKmh);
                if (s->wx.rainMm10 != WX_NA) n += snprintf(line + n, sizeof(line) - n, "R%.1f", s->wx.rainMm10 / 10.0f);
                row(4, "%s", line);
            } else if (s->comment[0] && s->kind != KIND_OBJECT) {
                char first[22], second[22];
                snprintf(first, sizeof(first), "%.21s", s->comment);
                snprintf(second, sizeof(second), "%.21s", strlen(s->comment) > 21 ? s->comment + 21 : "");
                row(3, "%s", first);
                row(4, "%s", second);
            } else {
                row(3, "(no comment)");
            }
            if (s->path[0]) row(5, "via %.17s", s->path);
            else row(5, "direct");
        }
    }

    // Vista ordenada y filtrada; el cursor se mantiene sobre el mismo indicativo aunque la lista cambie
    uint8_t stationView(uint8_t* view) {
        double myLat = 0, myLon = 0;
        bool haveMe = ownPosition(myLat, myLon);
        uint8_t n = buildStationView(heard, listSort, listFilter, haveMe, myLat, myLon, view);
        if (listBrowse && n > 0) {
            if (listCursor >= n || strcmp(heard.byRecent(view[listCursor])->call, listCursorCall) != 0) {
                for (uint8_t i = 0; i < n; i++) {
                    if (strcmp(heard.byRecent(view[i])->call, listCursorCall) == 0) { listCursor = i; break; }
                }
            }
            if (listCursor >= n) listCursor = n - 1;
        }
        return n;
    }

    void setListCursor(const uint8_t* view, uint8_t n, uint8_t position) {
        if (n == 0) return;
        listCursor = position % n;
        const Station* s = heard.byRecent(view[listCursor]);
        if (s) snprintf(listCursorCall, sizeof(listCursorCall), "%s", s->call);
    }

    void drawList() {
        const uint8_t ROWS = 5;
        uint8_t view[HeardList::CAPACITY];
        uint8_t n = stationView(view);
        static const char* const FILTER_TAGS[FILTER_COUNT] = { "", "POS", "MOB", "FIX", "WX", "DG" };
        char right[10];
        if (listBrowse && n > 0) snprintf(right, sizeof(right), "%u/%u", (unsigned)(listCursor + 1), (unsigned)n);
        else if (listFilter != FILTER_ALL) snprintf(right, sizeof(right), "%s %u", FILTER_TAGS[listFilter % FILTER_COUNT], (unsigned)n);
        else snprintf(right, sizeof(right), "%u", (unsigned)n);
        header(PAGE_TITLES[PAGE_LIST], right);
        if (n == 0) {
            row(1, heard.count() == 0 ? "No stations heard yet" : "No stations match");
            if (heard.count() > 0) row(3, "Long press: Filter");
            return;
        }
        double myLat, myLon;
        bool haveMe = ownPosition(myLat, myLon);
        static uint8_t top = 0;
        if (!listBrowse) top = 0;
        else if (listCursor < top) top = listCursor;
        else if (listCursor >= top + ROWS) top = listCursor - ROWS + 1;
        for (uint8_t i = 0; i < ROWS; i++) {
            const Station* s = (top + i < n) ? heard.byRecent(view[top + i]) : nullptr;
            if (!s) break;
            char distance[10] = "---", age[8], line[28];
            if (s->hasPosition && haveMe) fmtDist(distanceKm(myLat, myLon, s->lat, s->lon), distance, sizeof(distance));
            formatAge(millis() - s->lastMs, age, sizeof(age));
            snprintf(line, sizeof(line), "%c %-9.9s%6s%4s", kindLetter(s->kind), s->call, distance, age);
            int y = ROW_Y0 + i * ROW_PITCH;
            bool selected = listBrowse && top + i == listCursor;
            if (selected) pagesFillRect(0, y - 1, 128, 9, true);
            pagesText(0, y, line, 1, selected);
        }
        char line[28];
        if (listBrowse) snprintf(line, sizeof(line), "1P dn LP goto 2P exit");
        else snprintf(line, sizeof(line), "%u heard  %s", (unsigned)heard.count(), listSort == SORT_NEAREST ? "nearest" : listSort == SORT_NAME ? "by name" : "recent");
        text(2, 56, line);
    }

    uint32_t epochNow() {
        time_t t = now();
        return year(t) < 2024 ? 0 : (uint32_t)t;
    }

    // Icono de estado de 8x8: sobre cerrado (sin leer), sobre abierto (leido), flecha (enviado),
    // flecha con linea (esperando confirmacion), marca (confirmado), cruz (sin confirmar)
    void messageIcon(int x, int y, MsgState state, bool on) {
        switch (state) {
            case MSTATE_UNREAD:
                pagesFillRect(x, y + 1, 7, 6, on);
                pagesDrawLine(x, y + 1, x + 3, y + 4, !on); pagesDrawLine(x + 6, y + 1, x + 3, y + 4, !on);
                break;
            case MSTATE_READ:
                pagesDrawRect(x, y + 1, 7, 6, on);
                pagesDrawLine(x, y + 1, x + 3, y + 4, on); pagesDrawLine(x + 6, y + 1, x + 3, y + 4, on);
                break;
            case MSTATE_SENT:
            case MSTATE_WAITING:
                pagesDrawLine(x, y + 3, x + 6, y + 3, on);
                pagesDrawLine(x + 4, y + 1, x + 6, y + 3, on); pagesDrawLine(x + 4, y + 5, x + 6, y + 3, on);
                if (state == MSTATE_WAITING) pagesDrawLine(x, y + 7, x + 6, y + 7, on);
                break;
            case MSTATE_ACKED:
                pagesDrawLine(x, y + 4, x + 2, y + 6, on); pagesDrawLine(x + 2, y + 6, x + 7, y + 1, on);
                pagesDrawLine(x + 1, y + 4, x + 2, y + 5, on);
                break;
            case MSTATE_FAILED:
                pagesDrawLine(x, y + 1, x + 6, y + 7, on); pagesDrawLine(x + 6, y + 1, x, y + 7, on);
                break;
        }
    }

    const char* stateLabel(MsgState state) {
        switch (state) {
            case MSTATE_UNREAD:  return "NEW";
            case MSTATE_READ:    return "";
            case MSTATE_SENT:    return "SENT";
            case MSTATE_WAITING: return "WAIT ACK";
            case MSTATE_ACKED:   return "ACK OK";
            case MSTATE_FAILED:  return "NO ACK";
        }
        return "";
    }

    void drawMessageList() {
        const uint8_t ROWS = 5;
        uint8_t total = msgLog.count();
        if (total == 0) {
            row(1, "No messages");
            row(3, "Long press: menu");
            return;
        }
        if (msgCursor >= total) msgCursor = total - 1;
        static uint8_t top = 0;
        if (msgMode != MM_BROWSE) top = 0;
        else if (msgCursor < top) top = msgCursor;
        else if (msgCursor >= top + ROWS) top = msgCursor - ROWS + 1;
        uint32_t nowEpoch = epochNow();
        for (uint8_t i = 0; i < ROWS; i++) {
            const MsgEntry* e = msgLog.byRecent(top + i);
            if (!e) break;
            int y = ROW_Y0 + i * ROW_PITCH;
            bool selected = (msgMode == MM_BROWSE && top + i == msgCursor);
            if (selected) pagesFillRect(0, y - 1, 128, 9, true);
            messageIcon(1, y, MessageLog::stateOf(*e), !selected);
            pagesText(12, y, e->call, 1, selected);
            char when[8];
            formatMessageTime(e->epoch, nowEpoch, when, sizeof(when));
            pagesText(128 - 2 - 6 * 5, y, when, 1, selected);
        }
        char line[28];
        if (msgMode == MM_BROWSE) snprintf(line, sizeof(line), "1P dn LP open 2P exit");
        else if (msgLog.unread() > 0) snprintf(line, sizeof(line), "%u new  Hold: read", (unsigned)msgLog.unread());   // el menu abre el mensaje sin leer mas reciente
        else snprintf(line, sizeof(line), "Hold: read / reply");
        text(2, 56, line);
    }

    void drawMessageDetail() {
        const MsgEntry* e = msgLog.byRecent(msgCursor);
        if (!e) return;
        MsgState state = MessageLog::stateOf(*e);
        messageIcon(1, 11, state, true);
        char line[40];
        snprintf(line, sizeof(line), "%s %.9s", e->dir == 'R' ? "RX" : "TX", e->call);
        text(12, 11, line);
        const char* label = stateLabel(state);
        text(128 - 2 - 6 * (int)strlen(label), 11, label);
        formatMessageDateTime(e->epoch, line, sizeof(line));
        text(2, 20, line);
        size_t position = 0;
        char piece[32];
        int lines = 0;
        for (int r = 0; r < 4 && wrapNext(e->text, position, 21, piece, sizeof(piece)); r++) { text(0, 30 + r * 8, piece); lines++; }
        if (lines <= 3) text(2, 56, keyboardConnected ? "Hold: reply" : "Hold: quick reply");           // si el texto deja libre la ultima linea
    }

    void drawMessages() {
        char index[10];
        if (msgMode == MM_DETAIL && msgLog.count() > 0) {
            snprintf(index, sizeof(index), "%u/%u", (unsigned)(msgCursor + 1), (unsigned)msgLog.count());
            header(PAGE_TITLES[PAGE_MSG], index);
            drawMessageDetail();
        } else {
            header(PAGE_TITLES[PAGE_MSG]);
            drawMessageList();
        }
    }

    // ---------- Registro en memoria flash ----------
    const char* LOG_FILE = "/aprsMsgLog.txt";

    void saveSettings() {
        File file = SPIFFS.open("/pagesCfg.dat", FILE_WRITE);
        if (!file) return;
        char line[64];
        snprintf(line, sizeof(line), "%u %u %u %u %u %d %u %d %u", (unsigned)unitSystem, (unsigned)coordFormat, (unsigned)compassMode, northTrue ? 1u : 0u, declAuto ? 1u : 0u, (int)declManualTenths,
                 (unsigned)zoneMode, (int)zoneHalfHours, clock24h ? 1u : 0u);
        file.println(line);
        file.close();
    }

    void loadSettings() {
        File file = SPIFFS.open("/pagesCfg.dat", "r");
        if (!file) return;
        String content = file.readStringUntil('\n');
        file.close();
        int units = 0, coords = 0, mode = 0, north = 1, automatic = 1, declTenths = 0;
        int zone = 0, zoneHalf = 0, format24 = 1;
        int fields = sscanf(content.c_str(), "%d %d %d %d %d %d %d %d %d", &units, &coords, &mode, &north, &automatic, &declTenths, &zone, &zoneHalf, &format24);
        if (fields >= 2) {
            if (units >= 0 && units < UNITS_COUNT) unitSystem = (uint8_t)units;
            if (coords >= 0 && coords < COORD_COUNT) coordFormat = (uint8_t)coords;
        }
        if (fields >= 6) {
            if (mode >= 0 && mode < 3) compassMode = (uint8_t)mode;
            northTrue = north != 0;
            declAuto = automatic != 0;
            if (declTenths >= -300 && declTenths <= 300) declManualTenths = (int16_t)declTenths;
        }
        if (fields == 9) {
            if (zone >= 0 && zone < ZONE_COUNT) zoneMode = (uint8_t)zone;
            if (zoneHalf >= -24 && zoneHalf <= 28) zoneHalfHours = (int8_t)zoneHalf;
            clock24h = format24 != 0;
        }
    }

    void saveLog() {
        File file = SPIFFS.open(LOG_FILE, FILE_WRITE);
        if (!file) return;
        char line[128];
        for (int i = msgLog.count() - 1; i >= 0; i--) {         // del mas antiguo al mas reciente
            MessageLog::format(*msgLog.byRecent(i), line, sizeof(line));
            file.println(line);
        }
        file.close();
    }

    void loadLog() {
        msgLog.clear();
        SPIFFS.begin(true);
        File file = SPIFFS.open(LOG_FILE);
        if (file) {
            while (file.available()) {
                String line = file.readStringUntil('\n');
                line.trim();
                MsgEntry entry;
                if (MessageLog::parse(line.c_str(), entry)) msgLog.append(entry);
            }
            file.close();
            return;
        }
        // primera vez: se importan los mensajes que ya guardaba el firmware (sin hora, como leidos)
        File legacy = SPIFFS.open("/aprsMessages.txt");
        if (!legacy) return;
        while (legacy.available()) {
            String line = legacy.readStringUntil('\n');
            line.trim();
            int comma = line.indexOf(',');
            if (comma <= 0) continue;
            msgLog.addReceived(line.substring(0, comma).c_str(), line.substring(comma + 1).c_str(), 0);
            msgLog.markRead(0);
        }
        legacy.close();
        logDirty = true;
        logDirtyMs = millis();
    }

    void touchLog() {
        logDirty = true;
        logDirtyMs = millis();
    }

    void showToast(const char* message) {
        snprintf(toastText, sizeof(toastText), "%s", message);
        toastUntil = millis() + 2500;
    }

    void sosCancel() {
        bool was = sosState != SOS_IDLE;
        sosState = SOS_IDLE;
        if (was) showToast("SOS cancelled");
    }

    void sosStart() {
        if (!aprsMode()) { showToast("SOS: APRS modes only"); return; }     // MORSE, WSPR y LR-FHSS no emiten paquetes APRS
        sosState = SOS_COUNTDOWN;
        sosStartMs = millis();
        sosLastTick = 0;
        sosSent = 0;
        displayToggle(true);
        displayTime = millis();
    }

    void drawToast() {
        int w = 6 * (int)strlen(toastText) + 10;
        int x = (128 - w) / 2;
        pagesFillRect(x, 24, w, 16, false);
        pagesDrawRect(x, 24, w, 16, true);
        pagesText(x + 5, 28, toastText, 1, false);
    }

    // Si ya no quedan mensajes sin leer se apaga el aviso LED
    void afterReadChange() {
        if (msgLog.unread() == 0 && Config.notification.ledMessage) messageLed = false;
    }

    // Letras de las constelaciones activas (G E B R Q) y "+S" si el SBAS esta activo; vacio si la placa no las configura
    void gnssTags(char* out, size_t size) {
        out[0] = '\0';
        #ifdef GPS_CONFIG_CONSTELLATIONS
            uint8_t mask = GPS_Utils::gnssMask();
            size_t n = 0;
            if (mask & GPS_Utils::GNSS_GPS)     n += snprintf(out + n, size - n, "G");
            if (mask & GPS_Utils::GNSS_GALILEO) n += snprintf(out + n, size - n, "E");
            if (mask & GPS_Utils::GNSS_BEIDOU)  n += snprintf(out + n, size - n, "B");
            if (mask & GPS_Utils::GNSS_GLONASS) n += snprintf(out + n, size - n, "R");
            if (mask & GPS_Utils::GNSS_QZSS)    n += snprintf(out + n, size - n, "Q");
            if (mask & GPS_Utils::GNSS_SBAS)    snprintf(out + n, size - n, "+S");
        #else
            (void)size;
        #endif
    }

    void drawGps() {
        bool fix = gpsIsActive && gps.location.isValid();
        const char* state = !gpsIsActive ? "OFF" : (!fix ? "NO FIX" : (gps.altitude.isValid() ? "3D" : "2D"));
        header(PAGE_TITLES[PAGE_GPS], state);

        double lat, lon;
        bool have = ownPosition(lat, lon);
        char text1[24], text2[24];
        if (have) {
            formatCoordinate(lat, true, coordFormat, text1, sizeof(text1), 6);
            formatCoordinate(lon, false, coordFormat, text2, sizeof(text2), 6);
            row(0, "Lat %s%s", fix ? "" : "~", text1);
            row(1, "Lon %s%s", fix ? "" : "~", text2);
        } else {
            row(0, "Lat  waiting for fix");
            row(1, "Lon  ---");
        }

        if (!gpsIsActive) {
            char locator[10] = "--------";
            if (have) snprintf(locator, sizeof(locator), "%s", Utils::getMaidenheadLocator(lat, lon, 8));
            row(2, "%s", locator);
            char off[8];
            formatAge(gpsOffSinceMs ? millis() - gpsOffSinceMs : 0, off, sizeof(off));
            row(3, "GPS off for %s", off);
            row(4, "~ = last position");
            return;
        }

        char locator[10] = "--------";
        if (have) snprintf(locator, sizeof(locator), "%s", Utils::getMaidenheadLocator(lat, lon, 8));
        char altText[12] = "---";
        if (altitudeAvailable() || fix) formatAltitudeUnits(GPS_Utils::getAltitudeMeters(), unitSystem, altText, sizeof(altText));
        char line[48];
        row(2, "%s A=%s", locator, altText);

        unsigned used = gps.satellites.isValid() ? (unsigned)gps.satellites.value() : 0u;
        int inView = -1;
        #ifdef GPS_CONFIG_CONSTELLATIONS
            inView = GPS_Utils::satsInView();
        #endif
        char sats[24];
        if (inView >= (int)used) snprintf(sats, sizeof(sats), "%u/%d", used, inView);
        else snprintf(sats, sizeof(sats), "%u", used);
        if (gps.hdop.isValid()) {
            float hdop = gps.hdop.hdop();
            snprintf(line, sizeof(line), "Sat %s HDOP%.1f %s", sats, hdop, hdopQuality(hdop));
            if (strlen(line) > 21) snprintf(line, sizeof(line), "Sat %s H%.1f %s", sats, hdop, hdopQuality(hdop));
            row(3, "%s", line);
        } else {
            row(3, "Sat %s HDOP ---", sats);
        }

        if (fix) {
            char speedText[12];
            formatSpeedUnits(GPS_Utils::getSpeedKmph(), unitSystem, speedText, sizeof(speedText));
            row(4, "%s %03.0f %s", speedText, GPS_Utils::getCourseDeg(), cardinal16(GPS_Utils::getCourseDeg()));
        } else {
            row(4, "NO FIX (~=last pos)");
        }

        char timeText[24] = "--:--:--Z";
        if (gps.time.isValid()) snprintf(timeText, sizeof(timeText), "%02u:%02u:%02uZ", (unsigned)gps.time.hour(), (unsigned)gps.time.minute(), (unsigned)gps.time.second());
        char tags[12];
        gnssTags(tags, sizeof(tags));
        if (fix) row(5, "%s %s fix %us", timeText, tags, (unsigned)(gps.location.age() / 1000));
        else row(5, "%s %s", timeText, tags);
    }

    // Declinacion en uso (grados, positiva al este): modelo WMM con la posicion y la fecha, o el valor manual
    float currentDeclination(bool* fromModel = nullptr) {
        if (fromModel) *fromModel = false;
        if (declAuto && WMM::available()) {
            double lat, lon;
            if (ownPosition(lat, lon)) {
                uint32_t nowMs = millis();
                if (!declCacheOk || nowMs - declCacheMs > 60000UL) {
                    time_t t = now();
                    double decimalYear = (year(t) >= 2024) ? year(t) + (month(t) - 1) / 12.0 + (day(t) - 1) / 365.0 : WMM::epoch() + 1.5;
                    float value;
                    if (WMM::declination(lat, lon, altitudeAvailable() ? GPS_Utils::getAltitudeMeters() / 1000.0 : 0.0, decimalYear, value)) {
                        declCache = value;
                        declCacheOk = true;
                    }
                    declCacheMs = nowMs;
                }
                if (declCacheOk) {
                    if (fromModel) *fromModel = true;
                    return declCache;
                }
            }
        }
        return declManualTenths / 10.0f;
    }

    // Estacion elegida como destino de la brujula (nullptr si no hay)
    const Station* compassStation() {
        if (!compassTarget) return nullptr;
        int targetIndex = heard.indexOf(targetCall);
        return heard.byRecent(targetIndex >= 0 ? (uint8_t)targetIndex : 0);
    }

    void drawCompass() {
        const int cx = 100, cy = 37, radius = 22;
        bool valid = fusion.valid();
        float decl = currentDeclination();
        float trueHeading = valid ? fusion.heading() : 0.0f;
        float heading = northTrue ? trueHeading : wrap360(trueHeading - decl);     // referido al norte elegido
        float shift = northTrue ? 0.0f : decl;                                     // para pasar rumbos verdaderos al norte elegido

        // rosa: gira para que el rumbo actual quede arriba
        pagesDrawCircle(cx, cy, radius, true);
        for (int a = 0; a < 360; a += 30) {
            int x1, y1, x2, y2;
            float tick = (a % 90 == 0) ? 5.0f : 3.0f;
            polar(cx, cy, radius, a - heading, x1, y1);
            polar(cx, cy, radius - tick, a - heading, x2, y2);
            pagesDrawLine(x1, y1, x2, y2, true);
        }
        static const char* const letters[4] = {"N", "E", "S", "W"};
        for (int i = 0; i < 4; i++) {
            int x, y;
            polar(cx, cy, radius - 11, i * 90 - heading, x, y);
            text(x - 2, y - 3, letters[i]);
        }
        // marca fija del rumbo (arriba)
        pagesFillTriangle(cx, cy - radius + 1, cx - 3, cy - radius - 4, cx + 3, cy - radius - 4, true);

        // destino
        double myLat = 0, myLon = 0;
        const Station* target = compassStation();
        bool targetOk = target && target->hasPosition && ownPosition(myLat, myLon);

        // cabecera: destino a la izquierda del titulo y fuente del rumbo a la derecha ("!" = el magnetometro no sirve)
        char title[20], right[8];
        if (target) snprintf(title, sizeof(title), "TO %.9s", target->call);
        else snprintf(title, sizeof(title), "%s", PAGE_TITLES[PAGE_COMPASS]);
        const char* source = "---";
        switch (fusion.source()) {
            case HSRC_MAG:  source = "MAG";  break;
            case HSRC_GPS:  source = "GPS";  break;
            case HSRC_GYRO: source = "GYRO"; break;
            default: break;
        }
        bool magProblem = compassMode != 2 && !MAG_Utils::isHeadingValid();
        snprintf(right, sizeof(right), "%s%s", source, magProblem ? "!" : "");
        header(title, right);

        double bearing = 0;
        if (targetOk) {
            bearing = wrap360((float)bearingDeg(myLat, myLon, target->lat, target->lon) - shift);
            int x, y, xs, ys;
            polar(cx, cy, radius - 4, (float)bearing - heading, x, y);
            pagesDrawLine(cx, cy, x, y, true);
            polar(cx, cy, radius - 9, (float)bearing - heading - 18, xs, ys);
            pagesDrawLine(x, y, xs, ys, true);
            polar(cx, cy, radius - 9, (float)bearing - heading + 18, xs, ys);
            pagesDrawLine(x, y, xs, ys, true);
        } else {
            pagesFillCircle(cx, cy, 1, true);
        }

        // lectura numerica
        char label[8];
        snprintf(label, sizeof(label), "HDG %c", northTrue ? 'T' : 'M');
        text(2, 11, label);
        char value[8];
        if (valid) snprintf(value, sizeof(value), "%03d", (int)lroundf(heading) % 360);
        else snprintf(value, sizeof(value), "---");
        if (!target) {
            text(2, 20, value, 3);
            if (valid) text(2, 46, cardinal16(heading), 2);
            else text(2, 52, "no heading");
            return;
        }
        text(2, 20, value, 2);
        if (valid) text(42, 24, cardinal16(heading));
        if (!targetOk) {
            text(2, 38, ownPosition(myLat, myLon) ? "no target pos" : "no own pos");
            return;
        }
        double km = distanceKm(myLat, myLon, target->lat, target->lon);
        char line[24], distance[12];
        fmtDist(km, distance, sizeof(distance));
        if (valid) {
            float delta = wrap180((float)bearing - heading);
            if (fabsf(delta) < 5.0f) snprintf(line, sizeof(line), "B%03d ON COURSE", (int)lround(bearing) % 360);
            else snprintf(line, sizeof(line), "B%03d %+d%c", (int)lround(bearing) % 360, (int)lroundf(delta), delta > 0 ? '>' : '<');
        } else {
            snprintf(line, sizeof(line), "B%03d", (int)lround(bearing) % 360);
        }
        text(2, 38, line);
        if (km < 0.05) {
            text(2, 47, "ARRIVED");
            return;
        }
        text(2, 47, distance);
        float speed = GPS_Utils::getSpeedKmph();
        if (gpsIsActive && speed >= 2.0f && km / speed < 99.0) {
            char eta[12];
            formatShort((uint32_t)(km / speed * 3600.0), eta, sizeof(eta));
            snprintf(line, sizeof(line), "ETA %s", eta);
        } else {
            snprintf(line, sizeof(line), "ETA ---");
        }
        text(2, 56, line);
    }

    void drawAltimeter() {
        #ifdef GPS_BARO_ALTITUDE
            const bool hasBaro = true;
        #else
            const bool hasBaro = false;
        #endif
        header(PAGE_TITLES[PAGE_ALT], altitudeAvailable() ? altitudeSource() : nullptr);
        uint8_t count = altProfile.count();
        if (!altitudeAvailable() && count == 0) {
            row(2, "No altitude source");
            row(4, "Wait for GPS fix");
            return;
        }
        char big[12];
        if (altitudeAvailable()) snprintf(big, sizeof(big), "%.0f", altitudeValue(GPS_Utils::getAltitudeMeters(), unitSystem));
        else snprintf(big, sizeof(big), "---");
        text(0, 11, big, 2);
        text(strlen(big) * 12 + 2, 18, altitudeUnit(unitSystem));

        char line[24];
        if (vsStopped) snprintf(line, sizeof(line), unitSystem == UNITS_IMPERIAL ? "VS 0%s/m" : "VS 0.0%s/m", altitudeUnit(unitSystem));
        else snprintf(line, sizeof(line), unitSystem == UNITS_IMPERIAL ? "VS %+.0f%s/m" : "VS %+.1f%s/m", altitudeValue(altProfile.verticalSpeedMpm(), unitSystem), altitudeUnit(unitSystem));
        text(58, 11, line);
        if (hasBaro) {
            #ifdef GPS_BARO_ALTITUDE
                float qnhValue = BARO_Utils::getQnh();
                if (qnhValue <= 0.0f) snprintf(line, sizeof(line), "QNH ---");
                else if (BARO_Utils::isManualQnh()) snprintf(line, sizeof(line), "QNH %.1f*", qnhValue);
                else snprintf(line, sizeof(line), "QNH %.0f", qnhValue);
                text(58, 20, line);
            #endif
        }

        if (count < 2) {
            text(0, 40, "Collecting profile...");
            return;
        }
        // tramo visible segun el span: completo, ultima mitad o ultimos 10 minutos
        uint8_t first = 0;
        if (altSpanMode == 1) first = count / 2;
        else if (altSpanMode == 2) {
            uint32_t wanted = 600000UL / altProfile.stepMs();            // puntos que caben en 10 min
            if (wanted < 2) wanted = 2;
            first = count > wanted ? (uint8_t)(count - wanted) : 0;
        }
        if (count - first < 2) first = 0;
        float lo = altProfile.at(first), hi = lo;
        uint8_t hiIndex = first;
        for (uint8_t i = first; i < count; i++) {
            float v = altProfile.at(i);
            if (v < lo) lo = v;
            if (v > hi) { hi = v; hiIndex = i; }
        }
        snprintf(line, sizeof(line), "+%.0f -%.0f Max%.0f Min%.0f", altitudeValue(altProfile.ascentM(), unitSystem), altitudeValue(altProfile.descentM(), unitSystem),
                 altitudeValue(hi, unitSystem), altitudeValue(lo, unitSystem));
        if (strlen(line) > 21) snprintf(line, sizeof(line), "+%.0f -%.0f H%.0f L%.0f", altitudeValue(altProfile.ascentM(), unitSystem), altitudeValue(altProfile.descentM(), unitSystem),
                                        altitudeValue(hi, unitSystem), altitudeValue(lo, unitSystem));
        text(0, 29, line);

        const int gx = 0, gy = 38, gw = 128, gh = 17;
        pagesDrawRect(gx, gy, gw, gh, true);
        float range = hi - lo;
        if (range < 10.0f) { float mid = (hi + lo) * 0.5f; lo = mid - 5.0f; range = 10.0f; }
        uint8_t points = count - first;
        int prevX = 0, prevY = 0, maxX = 0, maxY = 0;
        for (uint8_t i = 0; i < points; i++) {
            int x = gx + 2 + (int)lroundf((float)i * (gw - 5) / (float)(points - 1));
            int y = gy + gh - 3 - (int)lroundf((altProfile.at(first + i) - lo) * (gh - 6) / range);
            if (i > 0) pagesDrawLine(prevX, prevY, x, y, true);
            if (first + i == hiIndex) { maxX = x; maxY = y; }
            prevX = x; prevY = y;
        }
        pagesDrawCircle(maxX, maxY, 2, true);                       // maximo del tramo
        pagesFillCircle(prevX, prevY, 2, true);                     // ahora

        // eje temporal bajo la grafica
        char span[10], axis[14];
        formatAge((uint32_t)(points - 1) * altProfile.stepMs(), span, sizeof(span));
        snprintf(axis, sizeof(axis), "-%s", span);
        text(1, 56, axis);
        text(128 - 1 - 3 * 6, 56, "now");
    }

    void drawWx() {
        header(PAGE_TITLES[PAGE_WX]);
        WX_Utils::refreshForScreen();
        WX_Utils::Reading r;
        if (!WX_Utils::getReading(r)) {
            row(1, "Sensor: none/no data");
            row(3, "Enable Telemetry in");
            row(4, "WebConfig & connect");
            row(5, "BME280/BMP280/BME680");
            return;
        }
        // filas cada 9 px: T/Td/Hr, presion y tendencia, gas, pronostico y motivo
        char line[32];
        if (r.hasHumidity) snprintf(line, sizeof(line), "%.1f" DEG_C " %.1f" DEG_C " %.0f%%", r.tempC, dewPointC(r.tempC, r.humidity), r.humidity);
        else snprintf(line, sizeof(line), "%.1f" DEG_C " --- ---", r.tempC);
        line[24] = '\0';
        int visible = 0;
        for (const char* c = line; *c; c++) if (!((*c & 0xC0) == 0x80)) visible++;      // columnas reales (UTF-8 en TFT)
        text((128 - 6 * visible) / 2, 11, line);                                        // primera linea centrada

        // filas cada 9 px a partir de y=20; la de gas solo aparece con BME680
        int y = 20;
        if (r.hasPressure) {
            snprintf(line, sizeof(line), "%.1fhPa %s", r.pressureHpa, PressureTrend::trendShort(pressureTrend.trend()));
        } else {
            snprintf(line, sizeof(line), "--- (no barometer)");
        }
        line[21] = '\0';
        text(0, y, line);
        y += 9;

        if (r.hasGas) {
            if (iaq.ready()) snprintf(line, sizeof(line), "IAQ %.0f %s", iaq.index(), iaq.label());
            else snprintf(line, sizeof(line), "IAQ wait %um", (unsigned)((iaq.warmupRemainingSec(millis()) + 59) / 60));
            line[21] = '\0';
            text(0, y, line);
            y += 9;
        }

        // pronostico local: presion (a nivel del mar si se conoce), tendencia de 3 h, humedad y punto de rocio
        if (!r.hasPressure) {
            text(0, y, "Fcst: needs barometer");
            return;
        }
        float pressure = r.pressureHpa;
        #ifdef GPS_BARO_ALTITUDE
            float reduced = BARO_Utils::reducedPressure(r.pressureHpa, r.tempC);
            if (reduced > 800.0f && reduced < 1100.0f) pressure = reduced;
        #endif
        float delta = 0;
        bool hasDelta = pressureTrend.delta3h(delta);
        float dew = r.hasHumidity ? dewPointC(r.tempC, r.humidity) : 0.0f;
        char forecast[20];
        forecastText(pressure, hasDelta, pressureTrend.trend(), r.tempC, r.hasHumidity ? r.humidity : -1.0f, dew, forecast, sizeof(forecast),
                     hasDelta ? delta : NAN);
        snprintf(line, sizeof(line), "Fcst: %s", forecast);
        line[21] = '\0';
        text(0, y, line);
        y += 9;
        char because[32];
        if (hasDelta) snprintf(because, sizeof(because), "%+.1f hPa/3h", delta);       // solo la tendencia de 3 h
        else snprintf(because, sizeof(because), "3h trend: wait");
        because[21] = '\0';
        text(0, y, because);
        y += 9;

        if (r.hasHumidity) {
            // helada nocturna (desde las 18:00 hasta media manana) o base de nubes (Hennig) y tipo de aire (T-Td)
            time_t t = now();
            bool frostValid = frostSampleT != 0 && t >= frostSampleT && t - frostSampleT < 16UL * 3600UL;
            float tmin = frostValid ? frostMinC(frostT18, frostTd18) : 99.0f;
            if (frostValid && tmin <= 0.0f) snprintf(line, sizeof(line), "FROST risk Tmin %+.0f" DEG_C, tmin);
            else {
                float base = cloudBaseM(r.tempC, dew);
                if (base < 1000.0f) snprintf(line, sizeof(line), "Cloud %.0fm %s", base, convectionClass(r.tempC, dew));
                else snprintf(line, sizeof(line), "Cloud %.1fkm %s", base / 1000.0f, convectionClass(r.tempC, dew));
            }
            line[21] = '\0';
            text(0, y, line);
            y += 9;
        }
        // 3 h de caida con base de nubes baja: aviso de niebla / lluvia inminente
        if (r.hasHumidity && hasDelta && delta < 0.0f && cloudBaseM(r.tempC, dew) < 600.0f && y <= 56) {
            text(0, y, "Fog/rain likely 80%");
        }
    }

    // Cuenta atras de la baliza de emergencia
    void drawSosCountdown() {
        uint32_t elapsed = millis() - sosStartMs;
        int left = elapsed >= SOS_COUNTDOWN_MS ? 0 : (int)((SOS_COUNTDOWN_MS - elapsed + 999) / 1000);
        pagesFillRect(0, 0, 128, 9, true);
        pagesText((128 - 6 * 10) / 2, 1, "SOS BEACON", 1, true);
        char big[12];
        snprintf(big, sizeof(big), "%d", left);
        text(52, 14, big, 4);
        text(0, 48, "Mic-E Emergency + SOS");
        pagesFillRect(0, 56, 128, 8, true);
        pagesText(2, 56, "Any press = cancel", 1, true);
    }

    // Pantalla de alarma: sustituye a la pagina mientras esta visible
    void drawAlarm() {
        bool blink = (millis() / 500) & 1;
        const char* title = alarmTitle((AlarmKind)alarmKind);
        if (blink) {
            pagesFillRect(0, 0, 128, 9, true);
            pagesText((128 - 6 * (int)strlen(title)) / 2, 1, title, 1, true);
        } else {
            pagesText((128 - 6 * (int)strlen(title)) / 2, 1, title, 1, false);
            pagesDrawRect(0, 0, 128, 9, true);
        }
        text(2, 12, alarmCall, 2);
        text(0, 30, alarmText);
        int index = heard.indexOf(alarmCall);
        const Station* st = index >= 0 ? heard.byRecent((uint8_t)index) : nullptr;
        double myLat, myLon;
        char line[32];
        if (st && st->hasPosition) {
            if (ownPosition(myLat, myLon)) {
                char distance[12];
                double km = distanceKm(myLat, myLon, st->lat, st->lon);
                double bearing = bearingDeg(myLat, myLon, st->lat, st->lon);
                fmtDist(km, distance, sizeof(distance));
                snprintf(line, sizeof(line), "%s %03.0f %s", distance, bearing, cardinal16(bearing));
            } else {
                snprintf(line, sizeof(line), "No own position");
            }
        } else {
            snprintf(line, sizeof(line), "No position known");
        }
        line[21] = '\0';
        text(0, 39, line);
        char age[8];
        formatAge(millis() - alarmMs, age, sizeof(age));
        if (st && st->hasPosition) {
            char since[8];
            formatAge(millis() - st->lastMs, since, sizeof(since));
            snprintf(line, sizeof(line), "Rx %s ago  Pos %s", age, since);
        } else {
            snprintf(line, sizeof(line), "Rx %s ago", age);
        }
        line[21] = '\0';
        text(0, 48, line);
        pagesFillRect(0, 56, 128, 8, true);
        if (alarmSounding) pagesText(2, 56, st && st->hasPosition ? "1P mute  LP GO TO" : "1P mute", 1, true);
        else pagesText(2, 56, st && st->hasPosition ? "1P close LP GO TO" : "1P close", 1, true);
    }

    void drawClock() {
        time_t t = now();
        bool haveTime = year(t) >= 2024;
        uint32_t nowMs = millis();
        bool gpsTime = gpsIsActive && gps.time.isValid() && gps.time.age() < 10000;
        char source[12] = "NONE";
        if (haveTime) {
            if (gpsTime) snprintf(source, sizeof(source), "GPS");
            else if (RTC_Utils::isReady()) snprintf(source, sizeof(source), "RTC");
            else snprintf(source, sizeof(source), "SYS");
            if (!gpsTime && lastGpsTimeMs != 0) {                       // edad de la ultima sincronizacion con el GPS
                char age[8];
                formatAge(nowMs - lastGpsTimeMs, age, sizeof(age));
                snprintf(source + strlen(source), sizeof(source) - strlen(source), " %s", age);
            }
        }
        header(PAGE_TITLES[PAGE_CLOCK], source);
        if (!haveTime) {
            text(4, 24, "NO TIME", 2);
            if (gpsIsActive) text(4, 46, "waiting for GPS/RTC");
            else text(4, 46, RTC_Utils::isReady() ? "RTC not set, GPS off" : "GPS off, no RTC");
            return;
        }

        bool dst = false;
        int offsetMin = zoneOffsetMinutes(zoneMode, zoneHalfHours, (int64_t)t, &dst);
        time_t local = t + (time_t)offsetMin * 60;

        char buffer[48];
        int hours = hour(local);
        if (clock24h) snprintf(buffer, sizeof(buffer), "%02d:%02d", hours, minute(local));
        else snprintf(buffer, sizeof(buffer), "%2d:%02d", hours % 12 == 0 ? 12 : hours % 12, minute(local));
        text(3, 13, buffer, 3);
        if (!clock24h) text(100, 11, hours >= 12 ? "PM" : "AM");
        snprintf(buffer, sizeof(buffer), "%02d", second(local));
        text(98, 20, buffer, 2);

        snprintf(buffer, sizeof(buffer), "%s %02d/%02d/%04d", weekdayName(weekdayIndex((int64_t)local)), day(local), month(local), year(local));
        text(4, 40, buffer);

        // UTC y zona en uso
        char zone[16] = "";
        if (zoneMode == ZONE_AUTO_EU) snprintf(zone, sizeof(zone), "%s %+d", zoneAbbreviation(zoneMode, dst), offsetMin / 60);
        else if (zoneMode == ZONE_MANUAL) {
            if (offsetMin % 60 == 0) snprintf(zone, sizeof(zone), "UTC%+d", offsetMin / 60);
            else snprintf(zone, sizeof(zone), "UTC%+d:30", offsetMin / 60);
        }
        if (zone[0]) snprintf(buffer, sizeof(buffer), "UTC %02d:%02d  %s", hour(t), minute(t), zone);
        else snprintf(buffer, sizeof(buffer), "UTC %02d:%02d", hour(t), minute(t));
        text(4, 48, buffer);

        // orto y ocaso con la posicion propia
        double lat, lon;
        if (ownPosition(lat, lon)) {
            int y, m, d;
            civilFromEpoch((int64_t)t, y, m, d);
            double sunrise, sunset;
            int sun = sunTimes(lat, lon, y, m, d, sunrise, sunset);
            if (sun == 0) {
                int rise = ((int)lround(sunrise) + offsetMin + 2880) % 1440;
                int set  = ((int)lround(sunset) + offsetMin + 2880) % 1440;
                int length = (int)lround(sunset - sunrise);
                if (length < 0) length += 1440;
                snprintf(buffer, sizeof(buffer), "Sun %02d:%02d-%02d:%02d %dh%02d", rise / 60, rise % 60, set / 60, set % 60, length / 60, length % 60);
            } else {
                snprintf(buffer, sizeof(buffer), sun > 0 ? "Sun: polar day" : "Sun: polar night");
            }
            text(4, 56, buffer);
        }
    }

    const char* regionName() {
        if (LoRa_Utils::isAisActive()) return "AIS";
        switch (loraIndex) { case 0: return "EU/WORLD"; case 1: return "POLAND"; case 2: return "UK"; case 3: return "US"; default: return "CUSTOM"; }
    }

    // "3.2s", "41s", "12m"
    void formatAirtime(uint32_t ms, char* out, size_t size) {
        if (ms == 0) snprintf(out, size, "0s");
        else if (ms < 10000UL) snprintf(out, size, "%.1fs", ms / 1000.0f);
        else if (ms < 100000UL) snprintf(out, size, "%us", (unsigned)(ms / 1000UL));
        else snprintf(out, size, "%um", (unsigned)(ms / 60000UL));
    }

    void drawLink() {
        header(PAGE_TITLES[PAGE_LINK], regionName());
        const LoraType& lora = *currentLoRaType;
        int sf = lora.spreadingFactor;
        long bw = lora.signalBandwidth;
        int cr = lora.codingRate4;
        const bool fskNow = Config.operatingMode == MODE_APRS_FSK && !lora.ais && LoRa_Utils::fskAvailable();
        char rate[12];
        if (fskNow) {
            row(0, "%.3fMHz FSK %ddBm", (lora.fskFrequency > 0 ? lora.fskFrequency : lora.frequency) / 1000000.0, LoRa_Utils::getPower());
            row(1, "G3RUH %.1fk dev%.1fk", lora.fskBitRate, lora.fskFreqDev);
        } else {
            row(0, "%.3fMHz SF%d %ddBm", lora.frequency / 1000000.0, sf, LoRa_Utils::getPower());
            double bitrate = loraBitrate(sf, bw, cr);
            if (bitrate < 1000.0) snprintf(rate, sizeof(rate), "%ubps", (unsigned)bitrate);
            else snprintf(rate, sizeof(rate), "%.2fkbps", bitrate / 1000.0);
            row(1, "BW%ld CR4/%d %s", bw / 1000, cr, rate);
        }
        if (digiCount > 0) row(2, "Rx %lu Tx %lu Dig %lu", (unsigned long)rxCount, (unsigned long)txCount, (unsigned long)digiCount);
        else row(2, "Rx %lu  Tx %lu", (unsigned long)rxCount, (unsigned long)txCount);
        const Station* s = heard.byRecent(0);
        if (s) {
            char sm[10];
            sMeterText(s->rssi, sm, sizeof(sm));
            row(3, "%s %d SNR %.1f", sm, s->rssi, s->snr);
        } else {
            row(3, "S- --- SNR ---");
        }
        char age[8];
        if (lastRxCall[0]) {
            formatAge(millis() - lastRxMs, age, sizeof(age));
            row(4, "Last %.9s %s", lastRxCall, age);
        } else {
            row(4, "Last: none");
        }
        uint32_t now = millis();
        char tx[10], rx[10];
        formatAirtime(airtime.txLastHourMs(now), tx, sizeof(tx));
        formatAirtime(airtime.rxLastHourMs(now), rx, sizeof(rx));
        float duty = airtime.txDutyPercent(now);
        if (duty < 1.0f) row(5, "ToA T%s %.2f%% R%s", tx, duty, rx);
        else row(5, "ToA T%s %.1f%% R%s", tx, duty, rx);
    }

    // Fila con barra de nivel: etiqueta, barra de 56 px y porcentaje de uso
    void meter(int y, const char* label, int percent) {
        text(2, y, label);
        pagesDrawRect(40, y, 56, 8, true);
        if (percent < 0) {
            text(100, y, "n/a");
            return;
        }
        if (percent > 100) percent = 100;
        int fill = (54 * percent) / 100;
        if (fill > 0) pagesFillRect(41, y + 1, fill, 6, true);
        char value[8];
        snprintf(value, sizeof(value), "%3d%%", percent);
        text(100, y, value);
    }

    void drawBoard() {
        header(PAGE_TITLES[PAGE_BOARD]);
        uint32_t heapTotal = ESP.getHeapSize();
        int heapUsed = heapTotal > 0 ? (int)(100UL * (heapTotal - ESP.getFreeHeap()) / heapTotal) : -1;
        meter(13, "Heap", heapUsed);

        uint32_t psramTotal = psramFound() ? ESP.getPsramSize() : 0;
        meter(24, "PSRAM", psramTotal > 0 ? (int)(100UL * (psramTotal - ESP.getFreePsram()) / psramTotal) : -1);   // -1 = sin PSRAM: n/a

        uint32_t sketch = ESP.getSketchSize();                      // firmware dentro de su particion de aplicacion
        uint32_t partition = sketch + ESP.getFreeSketchSpace();
        meter(35, "Flash", partition > 0 ? (int)(100UL * sketch / partition) : -1);

        char uptime[12];
        formatUptime(millis() / 1000, uptime, sizeof(uptime));
        char line[32];
        if (batteryConnected) snprintf(line, sizeof(line), "Bat %sV  Up %s", BATTERY_Utils::getBatteryInfoVoltage().c_str(), uptime);
        else snprintf(line, sizeof(line), "USB  Up %s", uptime);
        line[21] = '\0';
        text(2, 47, line);

        const char* bt = !bluetoothActive ? "off" : (bluetoothConnected ? "CON" : "on");
        const char* wifi = (WiFi.getMode() & WIFI_MODE_AP) ? "AP" : "off";
        snprintf(line, sizeof(line), "BT:%s  WiFi:%s", bt, wifi);
        text(2, 56, line);
    }

}   // namespace


namespace PAGES_Utils {

    void setup() {
        heard.clear();
        altProfile.clear();
        pressureTrend.clear();
        iaq.clear();
        fusion.reset();
        fusion.setDefaultGyroSign(PAGES_GYRO_SIGN);
        alarmShown = false;
        alarmSounding = false;
        alarmCall[0] = '\0';
        sosState = SOS_IDLE;
        txCount = 0;
        rxCount = 0;
        digiCount = 0;
        airtime.clear();
        haveTx = false;
        lastRxCall[0] = '\0';
        lastRxMs = 0;
        page = PAGE_MAIN;
        submenuOpen = false;
        msgMode = MM_LIST;
        msgCursor = 0;
        loadSettings();
        loadLog();
        afterReadChange();
        barUntil = millis() + BAR_VISIBLE_MS;
    }

    uint8_t unreadCount() {
        return msgLog.unread();
    }

    const char* replyCallsign() {
        return replyTo;
    }

    // Mensaje recibido (guardado por el firmware en su fichero clasico)
    void onMessageReceived(const char* call, const char* text) {
        msgLog.addReceived(call, text, epochNow());
        if (msgMode != MM_LIST && msgCursor + 1 < msgLog.count()) msgCursor++;      // el cursor sigue en el mismo mensaje
        touchLog();
        char toast[26];
        snprintf(toast, sizeof(toast), "MSG %.9s", call);
        showToast(toast);
    }

    // Mensaje enviado desde el usuario. ackId > 0 si se pidio confirmacion
    void onMessageSent(const char* call, const char* text, uint16_t ackId) {
        msgLog.addSent(call, text, epochNow(), ackId);
        if (msgMode != MM_LIST && msgCursor + 1 < msgLog.count()) msgCursor++;
        touchLog();
    }

    void onAckReceived(const char* call, uint16_t ackId) {
        if (msgLog.markAcked(call, ackId)) touchLog();
    }

    void onAckFailed(const char* call, uint16_t ackId) {
        if (msgLog.markFailed(call, ackId)) touchLog();
    }

    void onMessagesCleared() {
        msgLog.clear();
        msgMode = MM_LIST;
        msgCursor = 0;
        touchLog();
    }

    static void triggerAlarm(const char* call, AlarmKind kind, const char* detail, uint32_t nowMs) {
        bool same = alarmCall[0] && strcmp(alarmCall, call) == 0;
        bool known = same && (uint32_t)(nowMs - alarmMs) < ALARM_REPEAT_MS;
        snprintf(alarmCall, sizeof(alarmCall), "%s", call);
        snprintf(alarmText, sizeof(alarmText), "%s", detail);
        alarmKind = kind;
        alarmMs = nowMs;
        if (known) return;                          // ya avisada: solo se actualizan los datos
        alarmSounding = true;
        alarmShown = true;
        alarmStartMs = nowMs;
        alarmBeepMs = 0;                            // suena en la proxima vuelta
        alarmWakeMs = 0;
        submenuOpen = false;
        submenuId = SM_PAGE;
    }

    void onAprsPacket(const APRSPacket& packet, uint32_t nowMs) {
        bool hasPosition = (packet.type == 0 || packet.type == 4 || packet.type == 5) && (packet.latitude != 0.0f || packet.longitude != 0.0f);
        char symbol = (packet.symbol.length() > 0) ? packet.symbol[0] : ' ';
        char table = '/';                                                   // tabla del simbolo: '/', '\\' o superposicion (A-Z, 0-9; en formato comprimido los digitos viajan como a-j)
        if (packet.overlay.length() > 0) {
            char t = packet.overlay[0];
            if (t >= 'a' && t <= 'j') t = (char)('0' + (t - 'a'));
            if (t == '/' || t == '\\' || (t >= 'A' && t <= 'Z') || (t >= '0' && t <= '9')) table = t;
        }
        // texto asociado: comentario (posicion, Mic-E), estado u objeto; con estado Mic-E se antepone
        char info[80];
        info[0] = '\0';
        if (packet.type == 0 || packet.type == 2 || packet.type == 4 || packet.type == 5) snprintf(info, sizeof(info), "%s", packet.payload.c_str());
        heard.update(packet.sender.c_str(), symbol, packet.type, hasPosition, packet.latitude, packet.longitude,
                     packet.altitude, packet.speed, packet.course, packet.rssi, packet.snr, nowMs,
                     packet.tocall.c_str(), packet.path.c_str(), (packet.type == 1 || packet.type == 3 || packet.type == 6) ? nullptr : info,
                     packet.type == 4 ? miceStatusName(packet.miceType.c_str()) : nullptr, table);
        {   // alarmas: estado Mic-E Emergency o palabras SOS / EMERGENCY / MOB en el mensaje o comentario
            const char* body = (packet.type == 1 || packet.type == 0 || packet.type == 2 || packet.type == 4 || packet.type == 5) ? packet.payload.c_str() : "";
            AlarmKind kind = alarmFromText(body);
            const char* detail = body;
            if (kind == ALARM_NONE && packet.type == 4 && packet.miceType == "000") { kind = ALARM_EMERGENCY; detail = "Mic-E: Emergency!"; }
            if (kind != ALARM_NONE) triggerAlarm(packet.sender.c_str(), kind, detail, nowMs);
        }
        snprintf(lastRxCall, sizeof(lastRxCall), "%s", packet.sender.c_str());
        lastRxCall[sizeof(lastRxCall) - 1] = '\0';
        lastRxMs   = nowMs;
        rxCount++;
        if (lastIndex > 0) {                            // se sigue mirando la misma estacion aunque llegue otra
            int position = heard.indexOf(lastCall);
            lastIndex = position > 0 ? (uint8_t)position : 0;
        }
    }

    void onRx(int bytes) {
        const LoraType& lora = *currentLoRaType;
        airtime.addRx(millis(), (uint32_t)loraTimeOnAirMs(lora.spreadingFactor, lora.signalBandwidth, lora.codingRate4, bytes));
    }

    void onDigipeat() {
        digiCount++;
    }

    void onTx(int bytes) {
        const LoraType& lora = *currentLoRaType;
        airtime.addTx(millis(), (uint32_t)loraTimeOnAirMs(lora.spreadingFactor, lora.signalBandwidth, lora.codingRate4, bytes));
        txCount++;
        lastTxMs = millis();
        haveTx   = true;
    }

    bool emergencyActive() {
        return sosState == SOS_ACTIVE;
    }

    static void changedPage();

    // Gesto del boton: arranca la cuenta atras; repetirlo mientras cuenta o durante la emergencia la cancela
    void onEmergencyGesture() {
        if (!aprsMode() && sosState == SOS_IDLE) {                      // cada modo emite solo lo suyo
            displayToggle(true);
            displayTime = millis();
            showToast("SOS: APRS modes only");
            return;
        }
        displayToggle(true);
        displayTime = millis();
        menuDisplay = 0;
        submenuOpen = false;
        submenuId = SM_PAGE;
        if (sosState != SOS_IDLE) { sosCancel(); return; }
        page = PAGE_MAIN;
        changedPage();
        sosStart();
    }

    bool alarmLed(bool& on) {
        if (!alarmSounding || !Config.notification.ledMessage) return false;
        on = ((millis() / 250) & 1) != 0;
        return true;
    }

    void cwKeyPoll();                       // definida mas abajo (seccion CW)
    void wsprPoll();                        // idem (seccion WSPR)
    void ftPoll();                          // idem (modos FT8 y FT4)
    void drawFtPage();                      // idem
    void lrfhssPoll();                      // idem (modo LR-FHSS)
    void dapnetPoll();                      // idem (modo DAPNET)
    void cwBeaconPoll();                    // idem
    void cwKeypadPoll();                    // idem (teclado Yaesu FH-2)

    void update() {
        uint32_t nowMs = millis();
        cwKeyPoll();
        cwKeypadPoll();
        cwBeaconPoll();
        wsprPoll();
        ftPoll();
        lrfhssPoll();
        dapnetPoll();

        if (sosState == SOS_COUNTDOWN) {
            uint32_t elapsed = nowMs - sosStartMs;
            if (elapsed >= SOS_COUNTDOWN_MS) {
                sosState = SOS_ACTIVE;
                sosNextMs = nowMs;
                if (Config.notification.buzzerActive) NOTIFICATION_Utils::alarmBeep();
            } else {
                uint8_t tick = (uint8_t)(elapsed / 1000) + 1;
                if (tick != sosLastTick) {
                    sosLastTick = tick;
                    if (Config.notification.buzzerActive) NOTIFICATION_Utils::countdownBeep();
                }
            }
        }
        if (sosState == SOS_ACTIVE) {
            if (!gpsIsActive) SLEEP_Utils::gpsWakeUp();
            if ((int32_t)(nowMs - sosNextMs) >= 0) {
                if (STATION_Utils::sendEmergencyBeacon()) {
                    sosSent++;
                    char message[24];
                    snprintf(message, sizeof(message), "SOS sent #%u", (unsigned)sosSent);
                    showToast(message);
                    sosNextMs = millis() + SOS_PERIOD_MS;
                } else {
                    showToast("SOS: waiting GPS fix");
                    sosNextMs = millis() + SOS_RETRY_MS;
                }
            }
        }

        if (alarmSounding) {
            if (nowMs - alarmStartMs > ALARM_MAX_MS) {
                alarmSounding = false;
            } else {
                if (nowMs - alarmWakeMs >= 5000 || alarmWakeMs == 0) {      // la pantalla se enciende y no se duerme mientras suena
                    displayToggle(true);
                    displayTime = nowMs;
                    alarmWakeMs = nowMs ? nowMs : 1;
                }
                if (nowMs - alarmBeepMs >= ALARM_BEEP_MS || alarmBeepMs == 0) {
                    if (Config.notification.buzzerActive) NOTIFICATION_Utils::alarmBeep();
                    alarmBeepMs = millis() ? millis() : 1;
                }
            }
        }

        if (viewing() && page == PAGE_COMPASS) {
            uint32_t elapsed = nowMs - lastFusionMs;
            if (elapsed >= 20) {
                lastFusionMs = nowMs;
                bool gpsGood = gpsIsActive && gps.course.isValid() && gps.speed.isValid() && gps.location.age() < 3000 &&
                               gps.satellites.isValid() && gps.satellites.value() >= 4;
                bool magGood = MAG_Utils::isHeadingValid();
                bool gyroGood = IMU_Utils::isReady() && IMU_Utils::isFlat();
                // Si el magnetometro ya aprendio la autoalineacion con el rumbo GPS, heading() ya es Norte Verdadero;
                // si aun no la aprendio, tomamos el rumbo magnetico puro (restando el offset de mag_utils) y sumamos la declinacion activa de la pagina (auto WMM o manual)
                float magRaw  = wrap360(MAG_Utils::heading() - MAG_Utils::alignmentOffset());
                float magTrue = MAG_Utils::isAligned() ? MAG_Utils::heading() : wrap360(magRaw + currentDeclination(nullptr));
                fusion.update(elapsed / 1000.0f, gyroGood, IMU_Utils::getYawRateDps(), magGood && compassMode != 2, magTrue,
                              gpsGood && compassMode != 1, GPS_Utils::getCourseDeg(), GPS_Utils::getSpeedKmph());
            }
        }

        if (gpsIsActive && gps.time.isValid() && gps.time.age() < 10000) lastGpsTimeMs = nowMs ? nowMs : 1;
        if (gpsIsActive) gpsOffSinceMs = 0;
        else if (gpsOffSinceMs == 0) gpsOffSinceMs = nowMs ? nowMs : 1;

        if (logDirty && nowMs - logDirtyMs > 5000) {        // se agrupan los cambios para no desgastar la flash
            saveLog();
            logDirty = false;
        }

        if (nowMs - lastSampleMs < 1000) return;
        lastSampleMs = nowMs;

        if (altitudeAvailable()) altProfile.add(GPS_Utils::getAltitudeMeters(), nowMs);
        {   // tracker parado (GPS < 1,5 km/h o IMU en reposo, sin indicios de movimiento) durante 10 s: velocidad vertical a 0
            const bool gpsSpeed = gpsIsActive && gps.speed.isValid() && gps.speed.age() < 3000;
            const bool imu      = IMU_Utils::isReady();
            const bool stopped  = (gpsSpeed && GPS_Utils::getSpeedKmph() < 1.5f) || (imu && !IMU_Utils::movingNow());
            const bool moving   = (gpsSpeed && GPS_Utils::getSpeedKmph() >= 1.5f) || (imu && IMU_Utils::movingNow());
            if (stopped && !moving) { if (!stillSinceMs) stillSinceMs = nowMs ? nowMs : 1; }
            else stillSinceMs = 0;
            vsStopped = stillSinceMs != 0 && nowMs - stillSinceMs >= 10000;
            if (vsStopped) altProfile.zeroVerticalSpeed();
        }

        {   // aviso de llegada al destino de la brujula (a menos de 50 m; se rearma al alejarse mas de 80 m)
            const Station* target = compassStation();
            if (target && target->hasPosition && gpsIsActive && gps.location.isValid()) {
                double km = distanceKm(gps.location.lat(), gps.location.lng(), target->lat, target->lon);
                if (km < 0.05 && !arrivedNotified) {
                    arrivedNotified = true;
                    if (Config.notification.buzzerActive) NOTIFICATION_Utils::stationHeardBeep();
                } else if (km > 0.08) {
                    arrivedNotified = false;
                }
            } else {
                arrivedNotified = false;
            }
        }

        WX_Utils::Reading reading;
        if (WX_Utils::getReading(reading)) {
            if (reading.hasPressure) pressureTrend.add(reading.pressureHpa, nowMs);
            if (reading.hasGas) iaq.add(reading.gasKohm, reading.hasHumidity ? reading.humidity : 40.0f, nowMs);
            time_t t = now();
            if (reading.hasHumidity && year(t) >= 2024 && (frostSampleT == 0 || t - frostSampleT > 3600)) {    // muestra de las 18:00 locales para la helada nocturna
                bool dst = false;
                int offsetMin = zoneOffsetMinutes(zoneMode, zoneHalfHours, (int64_t)t, &dst);
                if ((((int64_t)t + offsetMin * 60) / 3600) % 24 == 18) {
                    frostT18 = reading.tempC;
                    frostTd18 = dewPointC(reading.tempC, reading.humidity);
                    frostSampleT = t;
                }
            }
        }
    }

    // ---------- CW / Morse ----------
    String          cwText;                         // texto decodificado del manipulador y de las macros enviadas
    const size_t    CW_TEXT_MAX         = 84;           // 4 filas de 21 columnas
    char            cwPending[10]       = "";           // puntos y rayas de la letra en curso
    bool            cwRxActive          = false;        // escuchando CW por RF
    float           cwRxDbm             = -150.0f;
    float           cwRxSnr             = 0.0f;
    uint8_t         cwRxWpm             = 0;

    void cwAppend(char c) {
        if (c == ' ' && (cwText.length() == 0 || cwText[cwText.length() - 1] == ' ')) return;
        cwText += c;
        while (cwText.length() > CW_TEXT_MAX) cwText.remove(0, 1);
    }

    void cwAppendString(const String& text) {
        for (size_t i = 0; i < text.length(); i++) cwAppend(text[i]);
    }

    const char* cwModeLabel() {
        return Config.cw.keyMode == 0 ? "KEY" : (Config.cw.keyMode == 1 ? "IA" : "IB");
    }

    void drawCwPage() {
        char right[12];
        if (cwRxActive) snprintf(right, sizeof(right), "RX %d", cwRxWpm);
        else            snprintf(right, sizeof(right), "%s %d", cwModeLabel(), Config.cw.wpm);
        header(PAGE_TITLES[PAGE_CW], right);
        for (uint8_t r = 0; r < 4; r++) {
            size_t from = (size_t)r * 21;
            if (from >= cwText.length()) break;
            String line = cwText.substring(from, from + 21);
            text(0, ROW_Y0 + r * ROW_PITCH, line.c_str());
        }
        if (cwPending[0]) row(4, "> %s", cwPending);
        if (cwRxActive) {
            if (cwRxSnr >= CWMorse::Envelope::MIN_SNR_DB) row(5, "%.0fdBm SNR %.0f", cwRxDbm, cwRxSnr);
            else row(5, "%.0fdBm no signal", cwRxDbm);
        }
        else if (Config.cw.keyPin < 0) row(5, "No key pin (WebUI)");
        else row(5, "Key GPIO%d  Hold:macros", Config.cw.keyPin);
    }

    void cwDrawPage() {
        pagesBegin();
        drawCwPage();
        pagesFlush();
    }

    // Portadora continua del SX126x manipulada todo/nada. Bloquea el bucle mientras dura el mensaje; un pulso del boton lo aborta.
    struct CwRun {
        char     text[72];
        uint8_t  wpm;
        uint32_t next;              // instante absoluto del siguiente cambio
        bool     armed;             // el boton se ha visto suelto: a partir de ahi, pulsarlo aborta
        bool     keyDown;
    };

    bool cwButtonDown() {
        #ifdef BUTTON_PIN
            return digitalRead(BUTTON_PIN) == LOW;
        #else
            return false;
        #endif
    }

    void cwDraw(const CwRun& run, size_t done, const char* pattern) {
        pagesBegin();
        char right[10];
        snprintf(right, sizeof(right), "%uWPM", run.wpm);
        header("CW TX", right);
        const int cols = 21;
        size_t len = strlen(run.text);
        for (size_t i = 0; i < len && i < (size_t)cols * 4; i++) {
            char one[2] = { run.text[i], 0 };
            int col = (int)(i % cols), row = (int)(i / cols);
            pagesText(1 + col * 6, 12 + row * 10, one, 1, false);
            if (i < done) pagesDrawLine(1 + col * 6, 12 + row * 10 + 8, 1 + col * 6 + 4, 12 + row * 10 + 8, true);   // ya enviado
        }
        if (pattern && pattern[0] && pattern[0] != ' ') {                   // simbolo que acaba de salir, en puntos y rayas
            int x = 2;
            for (const char* e = pattern; *e && x < 120; e++) {
                if (*e == '-') { pagesFillRect(x, 53, 9, 3, true); x += 12; }
                else           { pagesFillRect(x, 53, 3, 3, true); x += 6; }
            }
        }
        pagesDrawRect(0, 58, 128, 6, true);
        if (len > 0) pagesFillRect(1, 59, (int)(126 * done / len), 4, true);
        pagesFlush();
    }

    void cwKeyHook(void* ctx, bool on) {
        CwRun* run = (CwRun*)ctx;
        run->keyDown = on;
        LoRa_Utils::cwKey(on);
    }

    bool cwWaitHook(void* ctx, uint32_t ms) {
        CwRun* run = (CwRun*)ctx;
        run->next += ms;
        while ((int32_t)(millis() - run->next) < 0) {
            bool down = cwButtonDown();
            if (!down) run->armed = true;
            else if (run->armed) return false;
            delay(1);
        }
        return true;
    }

    void cwProgressHook(void* ctx, size_t done, size_t, const char* pattern, uint8_t) {
        if (done == 0) return;                    // la pantalla inicial ya esta dibujada
        cwDraw(*(CwRun*)ctx, done, pattern);      // la portadora esta apagada: se redibuja dentro de la pausa entre letras
    }

    // Sustituye {CALL} (indicativo sin SSID), {LOC} (locator de 6 caracteres si hay fix) y {VBAT} (voltios, 1 decimal) y pasa a mayusculas
    String cwExpand(String message) {
        String bare = currentBeacon->callsign;
        int dash = bare.indexOf('-');
        if (dash > 0) bare = bare.substring(0, dash);
        message.replace("{CALL}", bare);
        String loc = "";
        if (gps.location.isValid()) loc = Utils::getMaidenheadLocator(gps.location.lat(), gps.location.lng(), 6);
        message.replace("{LOC}", loc);
        String vbat = "";
        if (batteryConnected) {
            float v = BATTERY_Utils::getBatteryInfoVoltage().toFloat();
            if (v > 0.0f) { char tmp[8]; snprintf(tmp, sizeof(tmp), "%.1f", v); vbat = tmp; }
        }
        message.replace("{VBAT}", vbat);
        message.replace("  ", " ");
        message.trim();
        message.toUpperCase();
        return message;
    }

    bool sendCwText(const String& message, bool beacon = false) {
        if (!LoRa_Utils::cwAvailable()) { showToast("CW: SX126x only"); return false; }
        LoRa_Utils::cwSelect(beacon);               // la baliza usa el perfil CW Beacon; mensajes y manipulador, el de Telegrafia
        CwRun run;
        snprintf(run.text, sizeof(run.text), "%s", message.c_str());
        run.wpm     = (uint8_t)constrain(Config.cw.wpm, (int)CWMorse::WPM_MIN, (int)CWMorse::WPM_MAX);
        run.armed   = !cwButtonDown();
        run.keyDown = false;
        if (strlen(run.text) == 0) { showToast("CW: empty message"); return false; }
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "CW Tx", "---> %s (%u WPM)", run.text, run.wpm);
        cwDraw(run, 0, "");
        run.next = millis();
        CWMorse::Hooks hooks = { cwKeyHook, cwWaitHook, cwProgressHook, &run };
        bool finished = CWMorse::send(run.text, run.wpm, hooks);
        LoRa_Utils::cwEnd();
        cwAppendString(String(">") + run.text + " ");
        showToast(finished ? "CW sent" : "CW aborted");
        return finished;
    }

    void sendCw(uint8_t index) {
        if (index >= CW_MESSAGE_COUNT) return;
        sendCwText(cwExpand(Config.cw.messages[index]));
    }

    // Emite la baliza CW una vez (perfil CW Beacon) y reprograma la siguiente
    void cwBeaconRun(uint32_t minutes) {
        const bool done = sendCwText(cwExpand(Config.cw.beaconText), true);
        if (done) cwBeaconTxCount++;
        snprintf(cwBeaconStatus, sizeof(cwBeaconStatus), done ? "Sent #%u" : "Aborted", (unsigned)cwBeaconTxCount);
        cwBeaconStatusAt = millis() ? millis() : 1;
        cwBeaconNext = millis() + minutes * 60000UL;
    }

    // Baliza CW autonoma: cada beaconMin minutos (10 si se activa a mano sin intervalo) emite beaconText. Solo en modo MORSE
    void cwBeaconPoll() {
        if (Config.operatingMode != MODE_MORSE) return;
        if (!cwBeaconInit) { cwBeaconInit = true; cwBeaconOn = Config.cw.beaconMin > 0; cwBeaconNext = millis() + 10000UL; }
        if (!cwBeaconOn || (int32_t)(millis() - cwBeaconNext) < 0) return;
        if (submenuOpen || sosState != SOS_IDLE || alarmShown) return;
        const uint32_t minutes = Config.cw.beaconMin > 0 ? (uint32_t)Config.cw.beaconMin : 10UL;
        cwBeaconRun(minutes);
    }


    // Pantalla propia de la baliza CW (modo MORSE): indicativo, velocidad, potencia, frecuencia, texto y cuenta atras
    void drawCwbPage() {
        header(PAGE_TITLES[PAGE_CWB], cwBeaconOn ? "ON" : "OFF");
        String bare = currentBeacon->callsign;
        int dash = bare.indexOf('-');
        if (dash > 0) bare = bare.substring(0, dash);
        bare.toUpperCase();
        const int  wpm   = constrain(Config.cw.wpm, (int)CWMorse::WPM_MIN, (int)CWMorse::WPM_MAX);
        const int  power = Config.cw.beaconPower > 0 ? Config.cw.beaconPower : currentLoRaType->power;
        const long freq  = Config.cw.beaconFreq > 0 ? Config.cw.beaconFreq : currentLoRaType->frequency;
        const uint32_t minutes = Config.cw.beaconMin > 0 ? (uint32_t)Config.cw.beaconMin : 10UL;
        row(0, "%.9s %uWPM %ddBm", bare.c_str(), (unsigned)wpm, power);
        row(1, "%.4fMHz Every %um", freq / 1e6, (unsigned)minutes);
        String message = cwExpand(Config.cw.beaconText);
        if (message.length() > 21) message = message.substring(0, 20) + "~";
        row(2, "%s", message.length() ? message.c_str() : "(empty text)");
        if (cwBeaconStatus[0] && cwBeaconStatusAt && millis() - cwBeaconStatusAt < 8000UL) row(3, "%s", cwBeaconStatus);
        else if (!cwBeaconOn) row(3, "Beacon OFF");
        else {
            int32_t left = (int32_t)(cwBeaconNext - millis()) / 1000;
            if (left < 0) left = 0;
            row(3, "Next TX %02d:%02d", (int)(left / 60), (int)(left % 60));
        }
        row(4, "Sent %u", (unsigned)cwBeaconTxCount);
        row(5, "Hold: menu");
    }

    // ======================= DAPNET (receptor POCSAG) =======================
    void dapnetPoll() {
        if (Config.operatingMode != MODE_DAPNET) return;
        const uint32_t received = DAPNET_Utils::received();
        if (received == dapSeen) return;
        const bool grew = received > dapSeen;
        dapSeen = received;
        if (!grew) return;
        dapIndex = 0;                                       // se salta al mensaje nuevo
        dapNewMs = millis() ? millis() : 1;
        displayToggle(true);
        displayTime = millis();
        if (DAPNET_Utils::lastWasOwn() && Config.notification.buzzerActive) {
            if (Config.notification.bellRings) NOTIFICATION_Utils::bell(Bell::BELL_MSG_TO_ME);
            else                               NOTIFICATION_Utils::messageBeep();
        }
    }

    void drawDapPage() {
        const uint32_t nowMs = millis();
        const char* right = "RX";
        if (dapNewMs && nowMs - dapNewMs < 6000UL) right = "MSG";
        else if (DAPNET_Utils::lastSyncMs() && nowMs - DAPNET_Utils::lastSyncMs() < 2500UL) right = "SYNC";
        header(PAGE_TITLES[PAGE_DAP], right);
        row(0, "%.4fMHz %ubd", Config.dapnet.freqHz / 1e6, (unsigned)Config.dapnet.baud);
        const DAPNET_Utils::Entry* e = DAPNET_Utils::at(dapIndex);
        if (!e) {
            if (Config.dapnet.monitorAll)             row(1, "Monitor: all messages");
            else if (DAPNET_Utils::ownCount() == 0)   row(1, "No RIC set (RIC 8 only)");
            else                                      row(1, "%u own RIC set", (unsigned)DAPNET_Utils::ownCount());
            row(2, "Listening...");
            if (DAPNET_Utils::lastTimeMs()) row(3, "Time %.8s%s", DAPNET_Utils::lastTimeText(), DAPNET_Utils::timeApplied() ? " (clock)" : "");
        } else {
            const uint32_t age = (nowMs - e->atMs) / 1000UL;
            char ageText[8];
            if (age < 100)         snprintf(ageText, sizeof(ageText), "%us", (unsigned)age);
            else if (age < 6000)   snprintf(ageText, sizeof(ageText), "%um", (unsigned)(age / 60));
            else                   snprintf(ageText, sizeof(ageText), "%uh", (unsigned)(age / 3600));
            row(1, "%c%u/%u %lu F%u %s", e->own ? '*' : '#', (unsigned)dapIndex + 1, (unsigned)DAPNET_Utils::count(), (unsigned long)e->ric, (unsigned)e->func, ageText);
            const size_t len = strlen(e->text);
            for (uint8_t r = 0; r < 3; r++) {
                const size_t from = (size_t)r * 21;
                if (from >= len) break;
                char line[22];
                snprintf(line, sizeof(line), "%.21s", e->text + from);
                if (r == 2 && len > 63) line[20] = '~';
                row(2 + r, "%s", line);
            }
        }
        row(5, "%ddBm B%lu M%lu%s", DAPNET_Utils::lastRssi(), (unsigned long)DAPNET_Utils::batches(), (unsigned long)DAPNET_Utils::messages(), DAPNET_Utils::lastTimeMs() ? " T" : "");
    }

    // ======================= WSPR =======================
    // Callsign del perfil de baliza sin SSID
    void wsprCall(char out[8]) {
        String bare = currentBeacon->callsign;
        int dash = bare.indexOf('-');
        if (dash > 0) bare = bare.substring(0, dash);
        bare.toUpperCase();
        snprintf(out, 8, "%s", bare.c_str());
    }

    // Locator de 4 caracteres: el fijo de la WebUI o el del GPS (la baliza "sigue" la posicion)
    bool wsprGrid(char out[5]) {
        String fixed = Config.wspr.locator;
        fixed.trim();
        fixed.toUpperCase();
        if (fixed.length() == 4 && WSPR::gridValid(fixed.c_str())) { snprintf(out, 5, "%s", fixed.c_str()); return true; }
        double lat, lon;
        if (!ownPosition(lat, lon)) return false;                   // posicion GPS en vivo o la ultima conocida
        const char* loc = Utils::getMaidenheadLocator(lat, lon, 6);
        char g[5] = { (char)toupper(loc[0]), (char)toupper(loc[1]), loc[2], loc[3], '\0' };
        if (!WSPR::gridValid(g)) return false;
        memcpy(out, g, 5);
        return true;
    }

    // Milisegundos del dia UTC segun el GPS (hora de la ultima sentencia mas su antiguedad). Sin hora GPS valida no se emite:
    // el reloj del sistema solo tiene resolucion de 1 s y puede derivar.
    bool wsprMsOfDay(uint32_t& ms) {
        if (!gpsIsActive || !gps.time.isValid() || gps.time.age() > 10000) return false;
        if (gps.date.isValid() && gps.date.year() < 2024) return false;      // fecha absurda (arranque en frio)
        ms = ((uint32_t)gps.time.hour() * 3600UL + (uint32_t)gps.time.minute() * 60UL + gps.time.second()) * 1000UL + gps.time.centisecond() * 10UL + gps.time.age();
        ms %= 86400000UL;
        return true;
    }

    uint16_t wsprInterval() {                       // intervalos que dividen el dia en ciclos exactos (720 ranuras de 2 min)
        static const uint16_t allowed[] = { 2, 4, 6, 8, 10, 12, 20, 30, 60 };
        uint16_t best = 10, dist = 1000;
        for (uint16_t a : allowed) {
            uint16_t d = a > (uint16_t)Config.wspr.intervalMin ? a - (uint16_t)Config.wspr.intervalMin : (uint16_t)Config.wspr.intervalMin - a;
            if (d < dist) { dist = d; best = a; }
        }
        return best;
    }

    // Offset y compensacion de deriva efectivos: los de la tabla por potencia (si esta activada) o los generales
    int wsprCorrHz() {
        if (Config.wspr.perPower) return Config.wspr.ppCorrHz[WSPR::powerIndex(Config.wspr.powerDbm)];
        if (Config.wspr.useGlobalPpm) return (int)LoRa_Utils::ppmCorrectionHz(Config.wspr.freqHz);     // ppm global del oscilador a la frecuencia WSPR
        return Config.wspr.corrHz;
    }
    int wsprDriftTenths() {
        return Config.wspr.perPower ? Config.wspr.ppDriftTenths[WSPR::powerIndex(Config.wspr.powerDbm)] : Config.wspr.driftTenths;
    }

    void drawWsprPage() {
        char right[8];
        snprintf(right, sizeof(right), "%s", wsprProgress >= 0 ? "TX" : (wsprOn ? "ON" : "OFF"));
        header(PAGE_TITLES[PAGE_WSPR], right);
        char call[8], grid[5] = "----";
        wsprCall(call);
        wsprGrid(grid);
        row(0, "%s %s %ddBm", call, grid, WSPR::nearestValidPower(Config.wspr.powerDbm));
        row(1, "%.4fMHz %+dHz", Config.wspr.freqHz / 1e6, wsprCorrHz());
        uint32_t msDay = 0xFFFFFFFFUL;
        if (wsprMsOfDay(msDay)) { uint32_t s = msDay / 1000UL; row(2, "UTC %02u:%02u:%02u  %uSAT", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60), (unsigned)gps.satellites.value()); }
        else row(2, "NO GPS TIME");
        if (wsprProgress >= 0) {
            row(3, "TX %u/%u", (unsigned)wsprProgress + 1, (unsigned)WSPR::SYMBOLS);
            pagesDrawRect(2, ROW_Y0 + 4 * ROW_PITCH, 124, 7, true);
            pagesFillRect(4, ROW_Y0 + 4 * ROW_PITCH + 2, (120 * (wsprProgress + 1)) / WSPR::SYMBOLS, 3, true);
        } else {
            if (wsprStatus[0]) row(3, "%s", wsprStatus);
            else if (!wsprOn) row(3, "Beacon OFF");
            else if (wsprNextAt) {
                int32_t left = (int32_t)(wsprNextAt - millis()) / 1000;
                if (left < 0) left = 0;
                row(3, "Next TX %02d:%02d", (int)(left / 60), (int)(left % 60));
            } else row(3, msDay == 0xFFFFFFFFUL ? "Waiting for GPS time" : "Waiting for slot");
            row(4, "Every %um   Sent %u", (unsigned)wsprInterval(), (unsigned)wsprTxCount);
        }
        row(5, wsprProgress >= 0 ? "Button: abort" : "Hold: menu");
    }

    uint16_t lrfhssInterval();
    void lrfhssCall(char out[11]);
    bool lrfhssGrid(char out[7]);

    void drawLrfPage() {
        header(PAGE_TITLES[PAGE_LRF], lrfhssOn ? "ON" : "OFF");
        char call[11], grid[7] = "------";
        lrfhssCall(call);
        lrfhssGrid(grid);
        row(0, "%s %s %ddBm", call, grid, Config.lrfhss.powerDbm);
        row(1, "%.3fMHz BW%.1fk", Config.lrfhss.freqHz / 1e6, LRFHSS::bwKHz((uint8_t)Config.lrfhss.bwIdx));
        uint32_t msDay = 0xFFFFFFFFUL;
        if (wsprMsOfDay(msDay)) { uint32_t s = msDay / 1000UL; row(2, "UTC %02u:%02u:%02u  %uSAT", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60), (unsigned)gps.satellites.value()); }
        else row(2, "NO GPS TIME");
        if (lrfhssStatus[0] && (lrfhssStatusUntil == 0 || (int32_t)(lrfhssStatusUntil - millis()) > 0)) row(3, "%s", lrfhssStatus);
        else if (!lrfhssOn) row(3, "Beacon OFF");
        else if (lrfhssNextAt) {
            int32_t left = (int32_t)(lrfhssNextAt - millis()) / 1000;
            if (left < 0) left = 0;
            row(3, "Next TX %02d:%02d", (int)(left / 60), (int)(left % 60));
        } else row(3, msDay == 0xFFFFFFFFUL ? "Waiting for GPS time" : "Waiting for slot");
        row(4, "Every %um   Sent %u", (unsigned)lrfhssInterval(), (unsigned)lrfhssTxCount);
        row(5, "Hold: menu");
    }

    // Registra por el puerto serie las temperaturas internas disponibles (BME, IMU, PMU, ESP32) para correlacionarlas con la deriva medida con un SDR
    void wsprLogTemps(uint32_t seconds) {
        WX_Utils::refreshForScreen();
        WX_Utils::Reading r;
        const float bme = WX_Utils::getReading(r) ? r.tempC : NAN;
        IMU_Utils::update();
        const float imu = IMU_Utils::getTemperatureC();
        const float pmu = POWER_Utils::getPmuTemperature();
        const float esp = temperatureRead();
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "WSPR", "T+%lus temp C: bme=%.1f imu=%.1f pmu=%.1f esp=%.1f",
                   (unsigned long)seconds, bme, imu, pmu, esp);
    }

    // Emite una baliza WSPR: ms hasta el inicio (minuto par + 1 s). Bloquea hasta terminar (~110,6 s); el boton aborta
    void wsprTransmit(uint32_t msToStart) {
        char call[8], grid[5];
        wsprCall(call);
        if (!wsprGrid(grid)) return;
        uint8_t symbols[WSPR::SYMBOLS];
        const int dbm = WSPR::nearestValidPower(Config.wspr.powerDbm);
        if (!WSPR::encode(call, grid, dbm, symbols)) { snprintf(wsprStatus, sizeof(wsprStatus), "Callsign not WSPR"); wsprOn = false; return; }
        WSPR::Tones tones;
        tones.begin((double)Config.wspr.freqHz, (double)wsprCorrHz(), Config.wspr.dither, (double)wsprDriftTenths() / 10.0);
        const bool dark = Config.wspr.screenOff;                // pantalla apagada durante la emision (menos consumo y calor junto al TCXO)
        displayToggle(true);
        displayState = true;                                    // el modo ECO solo apaga la pantalla si displayState es cierto
        wsprStatus[0] = '\0';
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "WSPR", "Tx %s %s %ddBm in %lu ms", call, grid, dbm, (unsigned long)msToStart);
        if (!LoRa_Utils::wsprBegin(tones.exact(symbols[0], 0), dbm)) { snprintf(wsprStatus, sizeof(wsprStatus), "Radio error"); wsprOn = false; return; }
        const uint32_t t0 = micros() + msToStart * 1000UL;
        bool armed = !cwButtonDown(), aborted = false, started = false;
        auto poll = [&]() {                                   // vacia la UART del GPS (no se desborda en 110 s) y lee el boton: debe verse suelto antes de que cuente como pulsacion
            while (gpsSerial.available() > 0) gps.encode(gpsSerial.read());
            bool down = cwButtonDown();
            if (!down) armed = true;
            else if (armed) aborted = true;
        };
        wsprProgress = -1;
        snprintf(wsprStatus, sizeof(wsprStatus), "Starting...");
        pagesBegin(); drawWsprPage(); pagesFlush();
        if (dark) displayToggle(false);
        while (!aborted && (int32_t)(micros() - t0) < 0) { poll(); delay(1); }
        uint32_t nextPoll = micros();
        for (uint8_t i = 0; i < WSPR::SYMBOLS && !aborted; i++) {
            const uint32_t symEnd = t0 + (i + 1 < WSPR::SYMBOLS ? WSPR::symbolStartUs(i + 1) : WSPR::symbolStartUs(i) + 682667UL);
            const uint8_t sym = symbols[i];
            const uint32_t symStart = t0 + WSPR::symbolStartUs(i);
            const WSPR::TonePlan plan = tones.plan(sym, i, symEnd - symStart);
            if (!started) { started = true; LoRa_Utils::wsprStart(plan.first); }
            else LoRa_Utils::wsprTone(plan.first);
            wsprProgress = (int8_t)i;
            wsprStatus[0] = '\0';
            if (!dark && i % 6 == 0) { pagesBegin(); drawWsprPage(); pagesFlush(); }   // la pantalla se redibuja justo tras fijar el tono
            if (i % 15 == 0) wsprLogTemps((uint32_t)i * 683UL / 1000UL);               // cada ~10 s: temperaturas internas (serie) para estudiar la deriva
            const bool twoStep = plan.second != plan.first;
            const uint32_t switchAt = symStart + plan.switchUs;
            bool switched = false;
            while (!aborted && (int32_t)(micros() - symEnd) < 0) {
                uint32_t now = micros();
                // un unico cambio de palabra del PLL a mitad del simbolo (afinado de la separacion de tonos): sin escrituras a 1 kHz
                if (twoStep && !switched && (int32_t)(now - switchAt) >= 0) { LoRa_Utils::wsprTone(plan.second); switched = true; }
                if ((int32_t)(now - nextPoll) >= 0) { poll(); nextPoll = now + 20000UL; delay(1); }   // delay(1) cada 20 ms: deja correr la tarea IDLE (watchdog); yield() no lo hace
                yield();
            }
        }
        LoRa_Utils::wsprEnd();
        if (dark) { displayToggle(true); displayState = true; }
        wsprProgress = -1;
        if (aborted) { wsprOn = false; snprintf(wsprStatus, sizeof(wsprStatus), "Aborted: beacon OFF"); }
        else { wsprTxCount++; snprintf(wsprStatus, sizeof(wsprStatus), "Sent #%u", (unsigned)wsprTxCount); }
        wsprNextAt = 0;
        wsprStatusAt = millis();
        displayTime = millis();
    }

    // ======================= FT8 / FT4 / FST4W =======================
    // Solo en placas con SX1262/SX1268 (los modos no se ofrecen con otros chips): en las demas no se enlaza el codificador ni la modulacion
    #if defined(HAS_SX1262) || defined(HAS_SX1268)
    // Comparten con WSPR la pagina, el menu y las variables de estado (wspr*): solo hay un modo activo a la vez
    FTX::Proto ftProto() {
        if (Config.operatingMode == MODE_FT4) return FTX::PROTO_FT4;
        if (Config.operatingMode == MODE_FST4W) return Config.ft.fstSeconds == 300 ? FTX::PROTO_FST4W_300 : FTX::PROTO_FST4W_120;
        return FTX::PROTO_FT8;
    }

    // Frecuencia de DIAL del modo en curso (Hz); 0 = sin definir
    long ftDialHz() {
        if (Config.operatingMode == MODE_FT4) return (long)Config.ft.ft4FreqHz;
        if (Config.operatingMode == MODE_FST4W) return (long)Config.ft.fstFreqHz;
        return (long)Config.ft.ft8FreqHz;
    }

    // Correccion total (Hz) a la frecuencia de la senal: la global del oscilador en ppm mas el desplazamiento propio
    double ftCorrHz() { return LoRa_Utils::ppmCorrectionHzExact(ftDialHz()) + (double)Config.ft.corrHz; }

    // Locator de 4 caracteres: el fijo de la WebUI o el del GPS
    bool ftGrid(char out[5]) {
        String fixed = Config.ft.locator;
        fixed.trim();
        fixed.toUpperCase();
        if (fixed.length() == 4 && WSPR::gridValid(fixed.c_str())) { snprintf(out, 5, "%s", fixed.c_str()); return true; }
        return wsprGrid(out);
    }

    void drawFtPage() {
        const FTX::Proto proto = ftProto();
        char right[8];
        snprintf(right, sizeof(right), "%s", wsprProgress >= 0 ? "TX" : (wsprOn ? "ON" : "OFF"));
        header(FTX::name(proto), right);
        char call[8], grid[5] = "----";
        wsprCall(call);
        ftGrid(grid);
        row(0, "%s %s %ddBm", call, grid, FTX::isFst4w(proto) ? FST4W::nearestValidDbm(Config.ft.powerDbm) : Config.ft.powerDbm);
        row(1, "%.4fMHz %+dHz", ftDialHz() / 1e6, (int)lround(ftCorrHz()));
        uint32_t msDay = 0xFFFFFFFFUL;
        if (wsprMsOfDay(msDay)) { uint32_t s = msDay / 1000UL; row(2, "UTC %02u:%02u:%02u  %uSAT", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60), (unsigned)gps.satellites.value()); }
        else row(2, "NO GPS TIME");
        if (wsprProgress >= 0) {
            row(3, "TX %s", FTX::name(proto));
        } else {
            if (wsprStatus[0]) row(3, "%s", wsprStatus);
            else if (!wsprOn) row(3, "Beacon OFF");
            else if (wsprNextAt) {
                int32_t left = (int32_t)(wsprNextAt - millis()) / 1000;
                if (left < 0) left = 0;
                row(3, "Next TX %02d:%02d", (int)(left / 60), (int)(left % 60));
            } else row(3, msDay == 0xFFFFFFFFUL ? "Waiting for GPS time" : "Waiting for slot");
            row(4, "Every %u   Sent %u", (unsigned)(FTX::isFst4w(proto) ? Config.ft.fstEvery : Config.ft.everyCycles), (unsigned)wsprTxCount);
        }
        row(5, wsprProgress >= 0 ? "Button: abort" : "Hold: menu");
    }

    // Emite una baliza FT8/FT4: ms hasta el inicio de la ranura (+ retardo de WSJT-X). Bloquea hasta terminar (12,64 s FT8; 5,04 s FT4); el boton aborta.
    // La pantalla no se redibuja durante la emision: el bus I2C la detendria varios milisegundos y el PLL perderia la actualizacion de frecuencia.
    void ftTransmit(uint32_t msToStart) {
        const FTX::Proto proto = ftProto();
        char call[8], grid[5], text[FTX::MAX_TEXT + 4];
        wsprCall(call);
        if (!ftGrid(grid)) return;
        uint8_t tones[FTX::MAX_SYMBOLS];
        int dbm = Config.ft.powerDbm < 0 ? 0 : (Config.ft.powerDbm > 22 ? 22 : Config.ft.powerDbm);
        if (FTX::isFst4w(proto)) {
            dbm = FST4W::nearestValidDbm(dbm);                  // el mensaje solo admite 0, 3, 7, 10, 13, 17, 20 ... dBm
            snprintf(text, sizeof(text), "%s %s %d", call, grid, dbm);
            if (!FST4W::encode(call, grid, dbm, tones)) { snprintf(wsprStatus, sizeof(wsprStatus), "Msg not %s", FTX::name(proto)); wsprOn = false; return; }
        } else {
            FTX::buildText(Config.ft.text.c_str(), call, grid, text, sizeof(text));
            if (!FTX::encode(proto, text, tones)) { snprintf(wsprStatus, sizeof(wsprStatus), "Msg not %s", FTX::name(proto)); wsprOn = false; return; }
        }
        FTX::Shaper shaper;
        shaper.begin(proto, tones, (double)ftDialHz() + FTX::tone0AudioHz(proto) + ftCorrHz());
        const uint8_t nsym = FTX::symbols(proto);
        const uint8_t nstep = shaper.steps();
        const uint32_t stepUs = FTX::symbolUs(proto) / nstep;
        const bool dark = Config.ft.screenOff;
        displayToggle(true);
        displayState = true;
        wsprStatus[0] = '\0';
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "FTx", "Tx %s '%s' %ddBm in %lu ms", FTX::name(proto), text, dbm, (unsigned long)msToStart);
        if (!LoRa_Utils::wsprBegin(FTX::pllWord((double)ftDialHz() + FTX::tone0AudioHz(proto) + ftCorrHz()), dbm)) { snprintf(wsprStatus, sizeof(wsprStatus), "Radio error"); wsprOn = false; return; }
        const uint32_t t0 = micros() + msToStart * 1000UL;
        bool armed = !cwButtonDown(), aborted = false, started = false;
        auto poll = [&]() {                                   // vacia la UART del GPS y lee el boton (debe verse suelto antes de contar como pulsacion)
            while (gpsSerial.available() > 0) gps.encode(gpsSerial.read());
            bool down = cwButtonDown();
            if (!down) armed = true;
            else if (armed) aborted = true;
        };
        wsprProgress = -1;
        snprintf(wsprStatus, sizeof(wsprStatus), "Starting...");
        pagesBegin(); drawFtPage(); pagesFlush();
        if (dark) displayToggle(false);
        while (!aborted && (int32_t)(micros() - t0) < 0) { poll(); delay(1); }
        wsprProgress = 0;
        wsprStatus[0] = '\0';
        uint32_t nextPoll = micros();
        uint32_t writes = 0;
        for (uint8_t i = 0; i < nsym && !aborted; i++) {
            for (uint8_t j = 0; j < nstep && !aborted; j++) {
                const uint32_t at = t0 + FTX::stepStartUs(proto, i, j);
                while ((int32_t)(micros() - at) < 0) {
                    if (aborted) break;
                    uint32_t now = micros();
                    if ((int32_t)(now - nextPoll) >= 0) { poll(); nextPoll = now + 20000UL; }
                    yield();
                }
                if (aborted) break;
                const uint32_t w = shaper.nextWord(i, j);
                if (!started) { started = true; LoRa_Utils::wsprStart(w); }
                else LoRa_Utils::wsprTone(w);
                writes++;
                if ((writes & 7U) == 0 && (int32_t)(at + stepUs - micros()) > 1500) delay(1);     // cada ~20 ms deja correr la tarea IDLE (perro guardian); <= 1 ms: cabe en el paso
            }
        }
        LoRa_Utils::wsprEnd();
        if (dark) { displayToggle(true); displayState = true; }
        wsprProgress = -1;
        if (aborted) { wsprOn = false; snprintf(wsprStatus, sizeof(wsprStatus), "Aborted: beacon OFF"); }
        else { wsprTxCount++; snprintf(wsprStatus, sizeof(wsprStatus), "Sent #%u", (unsigned)wsprTxCount); }
        wsprNextAt = 0;
        wsprStatusAt = millis();
        displayTime = millis();
    }

    // Comprueba la hora GPS y entra en transmision cuando falta <= 3 s para la ranura. Solo en los modos FT8 y FT4
    void ftPoll() {
        if (!ftMode()) return;
        if (wsprProgress < 0 && strncmp(wsprStatus, "Sent", 4) == 0 && millis() - wsprStatusAt > 10000UL) wsprStatus[0] = '\0';
        if (!wsprOn || wsprProgress >= 0) return;
        if (submenuOpen || sosState != SOS_IDLE || alarmShown) return;
        if (ftDialHz() <= 0) { snprintf(wsprStatus, sizeof(wsprStatus), "Set %s freq (WebUI)", FTX::name(ftProto())); wsprNextAt = 0; return; }
        uint32_t msDay;
        if (!wsprMsOfDay(msDay)) { wsprNextAt = 0; if (strncmp(wsprStatus, "Sent", 4) != 0 && strncmp(wsprStatus, "Aborted", 7) != 0) wsprStatus[0] = '\0'; return; }
        char call[8], grid[5];
        wsprCall(call);
        if (strlen(call) < 3) { snprintf(wsprStatus, sizeof(wsprStatus), "Callsign not valid"); wsprNextAt = 0; return; }
        if (!ftGrid(grid)) { snprintf(wsprStatus, sizeof(wsprStatus), "WAIT GPS POSITION"); wsprNextAt = 0; return; }
        if (wsprStatus[0] && strncmp(wsprStatus, "Sent", 4) != 0 && strncmp(wsprStatus, "Aborted", 7) != 0) wsprStatus[0] = '\0';
        const int cfgEvery = FTX::isFst4w(ftProto()) ? Config.ft.fstEvery : Config.ft.everyCycles;
        uint16_t every = (uint16_t)(cfgEvery < 2 ? 2 : (cfgEvery > 240 ? 240 : cfgEvery));
        const uint32_t until = FTX::msUntilSlot(ftProto(), msDay, every, FTX::slotOffset(call, every));
        wsprNextAt = millis() + until;
        if (until <= 3000UL) ftTransmit(until);
    }

    #else
    void drawFtPage() {}
    void ftPoll() {}
    #endif

    // ---- Modo LR-FHSS (independiente del WSPR): mensaje "PB1 indicativo locator6 dBm" en el segundo 114 de un minuto par, tras acabar el WSPR ----
    void lrfhssSet(const char* text, uint32_t holdMs) {
        snprintf(lrfhssStatus, sizeof(lrfhssStatus), "%s", text);
        lrfhssStatusUntil = holdMs ? millis() + holdMs : 0;
    }

    uint16_t lrfhssInterval() {                     // intervalos que dividen el dia en ciclos exactos
        static const uint16_t allowed[] = { 2, 4, 6, 8, 10, 12, 20, 30, 60 };
        uint16_t best = 10, dist = 1000;
        for (uint16_t a : allowed) {
            uint16_t d = a > (uint16_t)Config.lrfhss.intervalMin ? a - (uint16_t)Config.lrfhss.intervalMin : (uint16_t)Config.lrfhss.intervalMin - a;
            if (d < dist) { dist = d; best = a; }
        }
        return best;
    }

    void lrfhssCall(char out[11]) {
        String bare = currentBeacon->callsign;
        int dash = bare.indexOf('-');
        if (dash > 0) bare = bare.substring(0, dash);
        bare.toUpperCase();
        snprintf(out, 11, "%s", bare.c_str());
    }

    // Locator de 6 caracteres: el fijo de la WebUI o el del GPS
    bool lrfhssGrid(char out[7]) {
        String fixed = Config.lrfhss.locator;
        fixed.trim();
        fixed.toUpperCase();
        if (fixed.length() == 6 && LRFHSS::gridValid(fixed.c_str())) { snprintf(out, 7, "%s", fixed.c_str()); return true; }
        double lat, lon;
        if (!ownPosition(lat, lon)) return false;
        const char* loc = Utils::getMaidenheadLocator(lat, lon, 6);
        char g[7];
        for (int i = 0; i < 6; i++) g[i] = (char)toupper(loc[i]);
        g[6] = '\0';
        if (!LRFHSS::gridValid(g)) return false;
        memcpy(out, g, 7);
        return true;
    }

    // Comprueba la hora GPS y emite la baliza LR-FHSS en su ranura. Solo en modo LR-FHSS (opcion activada en la WebUI, desactivada por defecto); no depende del modo WSPR ni de su baliza
    void lrfhssPoll() {
        if (Config.operatingMode != MODE_LRFHSS || !Config.lrfhss.enabled || !lrfhssOn) return;
        if (submenuOpen || sosState != SOS_IDLE || alarmShown) return;
        if (!LoRa_Utils::lrfhssAvailable()) { lrfhssSet("LRF: SX126x only", 0); lrfhssNextAt = 0; return; }
        const int fc = LRFHSS::freqCheck((uint32_t)Config.lrfhss.freqHz, (uint8_t)Config.lrfhss.bwIdx);
        if (fc < 0) { lrfhssSet(LRFHSS::freqCheckText(fc), 0); lrfhssNextAt = 0; return; }     // frecuencia no permitida: no se emite
        uint32_t msDay;
        if (!wsprMsOfDay(msDay)) { lrfhssNextAt = 0; return; }
        char call[11], grid[7];
        lrfhssCall(call);
        if (!LRFHSS::callValid(call)) { lrfhssSet("LRF: bad callsign", 0); lrfhssNextAt = 0; return; }
        if (!lrfhssGrid(grid)) { lrfhssSet("LRF: WAIT GPS POS", 0); lrfhssNextAt = 0; return; }
        if (lrfhssStatusUntil == 0) lrfhssStatus[0] = '\0';                       // la causa de un aviso permanente ya no existe
        if (fc == 1 && !lrfhssStatus[0]) lrfhssSet("LRF: BW>125kHz!", 0);          // permitida, pero fuera de la nota (p) del plan IARU R1
        const uint16_t interval = lrfhssInterval();
        const uint32_t until = LRFHSS::msUntilBeacon(msDay, interval, LRFHSS::slotOffset(call, interval));
        lrfhssNextAt = millis() + until;
        if (until > 1500UL || (int32_t)(lrfhssBlockUntil - millis()) > 0) return;
        char msg[40];
        const size_t n = LRFHSS::beaconEncode(call, grid, Config.lrfhss.powerDbm, msg, sizeof(msg));
        if (n == 0) { lrfhssSet("LRF: bad message", 0); lrfhssBlockUntil = millis() + 60000UL; return; }
        if (until > 0) delay(until);                                               // hasta el segundo 114
        displayToggle(true);
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "LR-FHSS", "Tx %s on %d Hz BW %.1f kHz CR %s %ddBm", msg, Config.lrfhss.freqHz, LRFHSS::bwKHz((uint8_t)Config.lrfhss.bwIdx), LRFHSS::crName((uint8_t)Config.lrfhss.crIdx), Config.lrfhss.powerDbm);
        const int state = LoRa_Utils::lrfhssSend((const uint8_t*)msg, n);
        lrfhssBlockUntil = millis() + 20000UL;
        if (state == 0) {                                                          // RADIOLIB_ERR_NONE
            lrfhssTxCount++;
            char t[22];
            snprintf(t, sizeof(t), "LRF sent #%u", (unsigned)lrfhssTxCount);
            lrfhssSet(t, 8000UL);
        } else {
            char t[22];
            snprintf(t, sizeof(t), "LRF error %d", state);
            lrfhssSet(t, 20000UL);
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "LR-FHSS", "Tx failed! State: %d", state);
        }
        displayTime = millis();
    }

    // Comprueba la hora GPS y entra en transmision cuando falta <= 3 s para la ranura. Solo en modo WSPR
    void wsprPoll() {
        if (Config.operatingMode == MODE_WSPR && wsprProgress < 0 && strncmp(wsprStatus, "Sent", 4) == 0 && millis() - wsprStatusAt > 10000UL) wsprStatus[0] = '\0';   // deja ver la cuenta atras
        if (Config.operatingMode != MODE_WSPR || !wsprOn || wsprProgress >= 0) return;
        if (submenuOpen || sosState != SOS_IDLE || alarmShown) return;
        uint32_t msDay;
        if (!wsprMsOfDay(msDay)) { wsprNextAt = 0; if (strncmp(wsprStatus, "Sent", 4) != 0 && strncmp(wsprStatus, "Aborted", 7) != 0) wsprStatus[0] = '\0'; return; }
        char call[8], grid[5];
        wsprCall(call);
        if (!WSPR::callValid(call)) { snprintf(wsprStatus, sizeof(wsprStatus), "Callsign not WSPR"); wsprNextAt = 0; return; }
        if (!wsprGrid(grid)) { snprintf(wsprStatus, sizeof(wsprStatus), "WAIT GPS POSITION"); wsprNextAt = 0; return; }
        if (wsprStatus[0] && strncmp(wsprStatus, "Sent", 4) != 0 && strncmp(wsprStatus, "Aborted", 7) != 0) wsprStatus[0] = '\0';
        const uint16_t interval = wsprInterval();
        const uint32_t until = WSPR::msUntilSlot(msDay, interval, WSPR::slotOffset(call, interval));
        wsprNextAt = millis() + until;
        if (until <= 3000UL) wsprTransmit(until);
    }

    // Portadora sin modular en la frecuencia central (mas la correccion) para medirla con un SDR y ajustar el desplazamiento
    void wsprTune() {
        if (!LoRa_Utils::cwAvailable()) { showToast("WSPR: SX126x only"); return; }
        displayToggle(true);
        const double hz = (double)Config.wspr.freqHz + (double)wsprCorrHz();
        if (!LoRa_Utils::wsprBegin(WSPR::pllWord(hz), WSPR::nearestValidPower(Config.wspr.powerDbm))) { showToast("Radio error"); return; }
        LoRa_Utils::wsprStart(WSPR::pllWord(hz));
        bool armed = !cwButtonDown();
        const uint32_t startMs = millis();
        uint32_t lastDraw = 0, lastLog = 0;
        for (;;) {
            bool down = cwButtonDown();
            if (!down) armed = true;
            else if (armed) break;
            uint32_t elapsed = millis() - startMs;
            if (elapsed >= 30000UL) break;
            if (elapsed - lastLog >= 5000UL || lastLog == 0) { lastLog = elapsed ? elapsed : 1; wsprLogTemps(elapsed / 1000UL); }   // temperaturas (serie) cada 5 s
            if (millis() - lastDraw > 500) {
                lastDraw = millis();
                pagesBegin();
                header("WSPR TUNE", "TX");
                row(1, "%.6f MHz", hz / 1e6);
                row(2, "Carrier on, %us left", (unsigned)(30 - elapsed / 1000));
                row(3, "Measure with an SDR");
                row(4, "offset = nominal-meas.");
                row(5, "Button: stop");
                pagesFlush();
            }
            delay(5);
        }
        LoRa_Utils::wsprEnd();
        displayTime = millis();
        showToast("Tune stopped");
    }

    // Portadora sin modular en la frecuencia de la baliza CW (con la correccion global en ppm) durante 30 s o hasta pulsar el boton:
    // sirve para medir el error del oscilador con un SDR y ajustar «Frequency correction (ppm)» en la WebUI
    void cwTune() {
        if (!LoRa_Utils::cwAvailable()) { showToast("CW: SX126x only"); return; }
        displayToggle(true);
        LoRa_Utils::cwSelect(true);
        const long nominal = Config.cw.beaconFreq > 0 ? Config.cw.beaconFreq : currentLoRaType->frequency;
        const double hz = (double)nominal + LoRa_Utils::ppmCorrectionHzExact(nominal);
        LoRa_Utils::cwKey(true);
        bool armed = !cwButtonDown();
        const uint32_t startMs = millis();
        uint32_t lastDraw = 0;
        for (;;) {
            bool down = cwButtonDown();
            if (!down) armed = true;
            else if (armed) break;
            uint32_t elapsed = millis() - startMs;
            if (elapsed >= 30000UL) break;
            if (millis() - lastDraw > 500) {
                lastDraw = millis();
                pagesBegin();
                header("CW TUNE", "TX");
                row(1, "%.6f MHz", hz / 1e6);
                row(2, "Carrier on, %us left", (unsigned)(30 - elapsed / 1000));
                row(3, "Correction %+.3f ppm", Config.radioPpm1000 / 1000.0);
                row(4, "Measure with an SDR");
                row(5, "Button: stop");
                pagesFlush();
            }
            delay(5);
        }
        LoRa_Utils::cwKey(false);
        LoRa_Utils::cwEnd();
        displayTime = millis();
        showToast("Tune stopped");
    }

    // ---- Manipulador (jack 3,5 mm): vertical o iambico. Punta = GPIO keyPin, anillo = GPIO dahPin, contactos a masa ----
    bool cwPinsReady = false;

    void cwInitPins() {
        if (cwPinsReady) return;
        if (Config.cw.keyPin >= 0) pinMode(Config.cw.keyPin, INPUT_PULLUP);
        if (Config.cw.dahPin >= 0) pinMode(Config.cw.dahPin, INPUT_PULLUP);
        cwPinsReady = true;
    }

    void cwReadPaddles(bool& dit, bool& dah) {
        dit = Config.cw.keyPin >= 0 && digitalRead(Config.cw.keyPin) == LOW;
        dah = Config.cw.dahPin >= 0 && digitalRead(Config.cw.dahPin) == LOW;
        if (Config.cw.keyMode != 0 && Config.cw.swapPaddles) { bool t = dit; dit = dah; dah = t; }
    }

    // Sesion de manipulacion: bloquea el bucle mientras se maneja el manipulador (1 ms de resolucion) y termina tras 2,5 s sin actividad
    void cwKeySession() {
        const bool iambic   = Config.cw.keyMode != 0;
        const bool rf       = Config.cw.keyTx && LoRa_Utils::cwAvailable();
        const bool tone     = Config.cw.sidetone && Config.notification.buzzerActive;
        const uint8_t wpm   = (uint8_t)constrain(Config.cw.wpm, (int)CWMorse::WPM_MIN, (int)CWMorse::WPM_MAX);
        if (rf) LoRa_Utils::cwSelect(false);        // el manipulador usa el perfil de Telegrafia
        CWMorse::Keyer keyer;
        CWMorse::Decoder decoder;
        keyer.begin(wpm, Config.cw.keyMode == 2 ? CWMorse::KEYER_IAMBIC_B : CWMorse::KEYER_IAMBIC_A);
        decoder.begin(wpm, !iambic);                 // con manipulador vertical el decodificador se adapta a la velocidad real
        bool keyed = false, armed = !cwButtonDown();
        uint32_t lastActivity = millis();
        cwPending[0] = '\0';
        displayToggle(true);
        displayTime = millis();
        for (;;) {
            uint32_t now = millis();
            bool dit, dah;
            cwReadPaddles(dit, dah);
            bool out = iambic ? keyer.update(now, dit, dah) : dit;
            if (out != keyed) {
                keyed = out;
                if (rf) LoRa_Utils::cwKey(keyed);
                if (tone) NOTIFICATION_Utils::sidetone(keyed);
                if (keyed) decoder.keyDown(now); else decoder.keyUp(now);
                snprintf(cwPending, sizeof(cwPending), "%s", decoder.pending());
            }
            char c = decoder.tick(now);
            if (c) {
                cwAppend(c);
                snprintf(cwPending, sizeof(cwPending), "%s", decoder.pending());
                if (!keyed && !dit && !dah) cwDrawPage();        // solo se redibuja con el manipulador en reposo para no perder temporizacion
            }
            if (dit || dah || keyed) lastActivity = now;
            if (cwButtonDown()) { if (armed) break; } else armed = true;
            if (now - lastActivity > 2500 && decoder.pending()[0] == '\0') break;
            delay(1);
        }
        if (keyed && rf) LoRa_Utils::cwKey(false);
        if (tone) NOTIFICATION_Utils::sidetone(false);
        if (rf) LoRa_Utils::cwEnd();
        cwPending[0] = '\0';
        displayTime = millis();
        cwDrawPage();
    }

    // Escucha CW por RF: modem FSK de 4,8 kHz y detector de envolvente por RSSI instantaneo (1 kHz). Bloquea el bucle hasta pulsar el boton
    void cwListenSession() {
        if (!LoRa_Utils::cwAvailable()) { showToast("CW: SX126x only"); return; }
        displayToggle(true);
        displayTime = millis();
        if (!LoRa_Utils::cwRxBegin()) {
            LoRa_Utils::cwRxEnd();
            showToast("CW RX failed");
            return;
        }
        const bool tone = Config.cw.sidetone && Config.notification.buzzerActive;
        const uint8_t wpm = (uint8_t)constrain(Config.cw.wpm, (int)CWMorse::WPM_MIN, (int)CWMorse::WPM_MAX);
        CWMorse::Envelope env;
        CWMorse::Decoder decoder;
        env.begin();
        decoder.begin(wpm, true);
        bool keyed = false, dirty = true, armed = !cwButtonDown();
        uint32_t started = millis(), lastDraw = 0;
        cwRxActive = true;
        cwRxWpm = wpm;
        cwPending[0] = '\0';
        for (;;) {
            uint32_t now = millis();
            float level = LoRa_Utils::cwRxLevel();
            bool k = env.update(level);
            if (k != keyed) {
                keyed = k;
                if (keyed) decoder.keyDown(now); else decoder.keyUp(now);
                if (tone) NOTIFICATION_Utils::sidetone(keyed);
                snprintf(cwPending, sizeof(cwPending), "%s", decoder.pending());
            }
            char c = decoder.tick(now);
            if (c) {
                cwAppend(c);
                snprintf(cwPending, sizeof(cwPending), "%s", decoder.pending());
                dirty = true;
            }
            // solo se redibuja con la portadora apagada (el dibujo tarda ~25 ms): tras un espacio entre palabras o cada segundo
            if (!keyed && ((dirty && c == ' ') || now - lastDraw > 1000)) {
                cwRxDbm = env.level();
                cwRxSnr = env.valid() ? env.peak() - env.noise() : 0.0f;
                cwRxWpm = decoder.wpm();
                cwDrawPage();
                lastDraw = millis();
                dirty = false;
                displayTime = lastDraw;
            }
            if (cwButtonDown()) { if (armed) break; } else armed = true;
            if (now - started > 600000UL) break;                     // 10 minutos como maximo
            delay(1);
        }
        if (tone) NOTIFICATION_Utils::sidetone(false);
        LoRa_Utils::cwRxEnd();
        cwRxActive = false;
        cwPending[0] = '\0';
        displayTime = millis();
        cwDrawPage();
        showToast("CW RX stopped");
    }

    // ---- Trainer (metodo Koch): solo zumbador / manipulador, sin emitir RF. Cualquier pulsacion del boton sale ----
    void cwTrainerScreen(const char* right, const char* big, uint8_t bigSize, const char* line1, const char* line2) {
        pagesBegin();
        header("TRAINER", right);
        if (big && big[0]) pagesText((128 - 6 * bigSize * (int)strlen(big)) / 2, 18, big, bigSize, false);
        if (line1) text((128 - 6 * (int)strlen(line1)) / 2, 44, line1);
        if (line2) text((128 - 6 * (int)strlen(line2)) / 2, 54, line2);
        pagesFlush();
    }

    char cwRandomKoch(uint8_t level) {
        return CWMorse::KOCH_ORDER[random(level)];
    }

    void cwTrainKeyHook(void*, bool on) {
        NOTIFICATION_Utils::sidetone(on);
    }

    // Escuchar: grupos de 5 caracteres en el zumbador (velocidad de caracter = WPM, espacios Farnsworth); despues se muestran
    void cwTrainerListen() {
        if (!Config.notification.buzzerActive) { showToast("Trainer needs buzzer"); return; }
        displayToggle(true);
        const uint8_t level = (uint8_t)constrain(Config.cw.trainerLevel, 2, (int)CWMorse::KOCH_COUNT);
        const uint8_t wpm   = (uint8_t)constrain(Config.cw.wpm, (int)CWMorse::WPM_MIN, (int)CWMorse::WPM_MAX);
        const uint8_t gap   = (uint8_t)constrain(Config.cw.trainerGapWpm, 0, 40);
        char right[12];
        snprintf(right, sizeof(right), "L%u %uWPM", level, wpm);
        CwRun run;
        run.text[0] = '\0';
        run.wpm = wpm;
        run.armed = !cwButtonDown();
        run.keyDown = false;
        uint16_t groups = 0;
        char group[6];
        bool aborted = false;
        while (!aborted) {
            for (uint8_t i = 0; i < 5; i++) group[i] = cwRandomKoch(level);
            group[5] = '\0';
            char count[32];
            snprintf(count, sizeof(count), "Group %u (button: exit)", (unsigned)(groups + 1));
            cwTrainerScreen(right, "?????", 2, "Listen...", count);
            run.next = millis();
            CWMorse::Hooks hooks = { cwTrainKeyHook, cwWaitHook, nullptr, &run };
            if (!CWMorse::send(group, wpm, hooks, gap)) { aborted = true; break; }
            if (!cwWaitHook(&run, 1500)) { aborted = true; break; }
            cwTrainerScreen(right, group, 2, "Answer", count);
            groups++;
            if (!cwWaitHook(&run, 3000)) { aborted = true; break; }
        }
        NOTIFICATION_Utils::sidetone(false);
        displayTime = millis();
        char message[24];
        snprintf(message, sizeof(message), "Trainer: %u groups", (unsigned)groups);
        showToast(message);
    }

    // Manipular: se muestra un caracter, se teclea con el manipulador y se corrige
    void cwTrainerKeying() {
        if (Config.cw.keyPin < 0) { showToast("Set key GPIO (WebUI)"); return; }
        cwInitPins();
        displayToggle(true);
        const bool iambic   = Config.cw.keyMode != 0;
        const bool tone     = Config.cw.sidetone && Config.notification.buzzerActive;
        const uint8_t level = (uint8_t)constrain(Config.cw.trainerLevel, 2, (int)CWMorse::KOCH_COUNT);
        const uint8_t wpm   = (uint8_t)constrain(Config.cw.wpm, (int)CWMorse::WPM_MIN, (int)CWMorse::WPM_MAX);
        char right[12];
        uint16_t ok = 0, total = 0;
        bool armed = !cwButtonDown(), quit = false;
        while (!quit) {
            char target = cwRandomKoch(level);
            char one[2] = { target, 0 };
            snprintf(right, sizeof(right), "%u/%u", (unsigned)ok, (unsigned)total);
            cwTrainerScreen(right, one, 3, CWMorse::pattern(target), "Key it (button: exit)");
            CWMorse::Keyer keyer;
            CWMorse::Decoder decoder;
            keyer.begin(wpm, Config.cw.keyMode == 2 ? CWMorse::KEYER_IAMBIC_B : CWMorse::KEYER_IAMBIC_A);
            decoder.begin(wpm, !iambic);
            bool keyed = false;
            char got = 0;
            uint32_t startMs = millis();
            while (!got && !quit) {
                uint32_t now = millis();
                bool dit, dah;
                cwReadPaddles(dit, dah);
                bool out = iambic ? keyer.update(now, dit, dah) : dit;
                if (out != keyed) {
                    keyed = out;
                    if (tone) NOTIFICATION_Utils::sidetone(keyed);
                    if (keyed) decoder.keyDown(now); else decoder.keyUp(now);
                    startMs = now;
                }
                char c = decoder.tick(now);
                if (c && c != ' ') got = c;
                if (cwButtonDown()) { if (armed) quit = true; } else armed = true;
                if (!keyed && !dit && !dah && decoder.pending()[0] == '\0' && now - startMs > 15000) got = '-';   // sin respuesta
                delay(1);
            }
            if (tone) NOTIFICATION_Utils::sidetone(false);
            if (quit) break;
            total++;
            bool right_ = (got == target);
            if (right_) ok++;
            char result[22];
            if (right_) snprintf(result, sizeof(result), "OK!");
            else snprintf(result, sizeof(result), "You keyed %c", got);
            snprintf(right, sizeof(right), "%u/%u", (unsigned)ok, (unsigned)total);
            cwTrainerScreen(right, one, 3, result, CWMorse::pattern(target));
            uint32_t until = millis() + 1800;
            while ((int32_t)(millis() - until) < 0) {
                if (cwButtonDown()) { if (armed) { quit = true; break; } } else armed = true;
                delay(5);
            }
        }
        displayTime = millis();
        char message[24];
        snprintf(message, sizeof(message), "Score %u/%u", (unsigned)ok, (unsigned)total);
        showToast(message);
    }

    // Teclado Yaesu FH-2 en un pin ADC: las teclas 1-5 emiten los mensajes CW 1-5 (una vez por pulsacion; hay que soltar para repetir)
    void cwKeypadPoll() {
        static int     lastPin = -2;
        static int     cand    = -1;
        static uint8_t stable  = 0;
        static bool    latched = false;
        static uint32_t lastMs = 0;
        const int pin = Config.cw.keypadPin;
        if (page != PAGE_CW || submenuOpen || pin < 0 || !FH2::adcPinOk(pin) || sosState != SOS_IDLE || alarmShown) return;
        const uint32_t t = millis();
        if (t - lastMs < 15) return;
        lastMs = t;
        if (pin != lastPin) { pinMode(pin, INPUT); lastPin = pin; }
        const int mv = analogReadMilliVolts(pin);
        const int k = FH2::classify(mv, Config.cw.keypadMv, CW_MESSAGE_COUNT);
        if (k < 0) {
            cand = -1; stable = 0;
            if (mv >= FH2::IDLE_MV) latched = false;                // tecla soltada
            return;
        }
        if (latched) return;
        if (k == cand) {
            if (++stable >= 3) { latched = true; sendCw((uint8_t)k); displayTime = millis(); }
        } else { cand = k; stable = 1; }
    }

    void cwKeyPoll() {
        if (page != PAGE_CW || submenuOpen || Config.cw.keyPin < 0 || sosState != SOS_IDLE || alarmShown) return;
        cwInitPins();
        bool dit, dah;
        cwReadPaddles(dit, dah);
        if (dit || dah) cwKeySession();
    }

    void show() {
        if (!pageVisible(page)) { page = (Config.operatingMode == MODE_MORSE) ? PAGE_CW : ((Config.operatingMode == MODE_WSPR || ftMode()) ? PAGE_WSPR : (Config.operatingMode == MODE_LRFHSS ? PAGE_LRF : (Config.operatingMode == MODE_DAPNET ? PAGE_DAP : PAGE_MAIN))); changedPage(); }
        pagesBegin();
        if (sosState == SOS_COUNTDOWN) {
            drawSosCountdown();
            pagesFlush();
            return;
        }
        if (alarmShown) {
            drawAlarm();
            if ((int32_t)(toastUntil - millis()) > 0 && toastText[0]) drawToast();
            pagesFlush();
            return;
        }
        switch (page) {
            case PAGE_MAIN:    drawMain();      break;
            case PAGE_MSG:     drawMessages();  break;
            case PAGE_CW:      drawCwPage();    break;
            case PAGE_CWB:     drawCwbPage();   break;
            case PAGE_WSPR:    if (ftMode()) drawFtPage(); else drawWsprPage(); break;
            case PAGE_LRF:     drawLrfPage();   break;
            case PAGE_DAP:     drawDapPage();   break;
            case PAGE_LAST:    drawLast();      break;
            case PAGE_LIST:    drawList();      break;
            case PAGE_GPS:     drawGps();       break;
            case PAGE_COMPASS: drawCompass();   break;
            case PAGE_ALT:     drawAltimeter(); break;
            case PAGE_WX:      drawWx();        break;
            case PAGE_CLOCK:   drawClock();     break;
            case PAGE_LINK:    drawLink();      break;
            case PAGE_BOARD:   drawBoard();     break;
            default: break;
        }
        if (submenuOpen && millis() - submenuMs > SUBMENU_TIMEOUT_MS) submenuOpen = false;
        if (submenuOpen) drawSubmenu();
        else if (aprsMode() && (int32_t)(barUntil - millis()) > 0) drawBar();
        if ((int32_t)(toastUntil - millis()) > 0 && toastText[0]) drawToast();
        pagesFlush();
    }

    // Seleccion del modo tras el mensaje de bienvenida: pulsacion corta = siguiente, larga = aceptar, 8 s sin pulsar = el ultimo modo usado
    uint8_t selectMode() {
        #ifdef BUTTON_PIN
            struct Option { uint8_t mode; const char* label; };
            Option options[10];
            uint8_t n = 0;
            options[n++] = Option{ MODE_APRS_LORA, "APRS LoRa" };
            if (LoRa_Utils::fskAvailable()) options[n++] = Option{ MODE_APRS_FSK, "APRS FSK 9600" };
            if (LoRa_Utils::cwAvailable())  options[n++] = Option{ MODE_MORSE, "MORSE / CW" };
            if (LoRa_Utils::cwAvailable())  options[n++] = Option{ MODE_WSPR, "WSPR beacon" };
            if (LoRa_Utils::cwAvailable())  options[n++] = Option{ MODE_FT8, "FT8 beacon" };
            if (LoRa_Utils::cwAvailable())  options[n++] = Option{ MODE_FT4, "FT4 beacon" };
            if (LoRa_Utils::cwAvailable())  options[n++] = Option{ MODE_FST4W, "FST4W beacon" };
            if (Config.lrfhss.enabled && LoRa_Utils::lrfhssAvailable()) options[n++] = Option{ MODE_LRFHSS, "LR-FHSS beacon" };
            if (LoRa_Utils::dapnetAvailable()) options[n++] = Option{ MODE_DAPNET, "DAPNET pager" };
            uint8_t sel = 0;
            for (uint8_t i = 0; i < n; i++) if (options[i].mode == Config.savedMode) sel = i;
            pinMode(BUTTON_PIN, INPUT_PULLUP);
            const uint32_t TIMEOUT_MS = 8000, HOLD_MS = 700;
            uint32_t waitStart = millis();
            while (digitalRead(BUTTON_PIN) == LOW && millis() - waitStart < 3000) delay(10);   // sigue pulsado desde el arranque: se espera a que lo suelte
            uint32_t lastActivity = millis(), downAt = 0, lastShown = 0;
            bool wasDown = false, chosen = false;
            int shownSeconds = -1;
            for (;;) {
                uint32_t now = millis();
                bool down = digitalRead(BUTTON_PIN) == LOW;
                if (down && !wasDown) { wasDown = true; downAt = now; lastActivity = now; }
                if (down && wasDown && now - downAt >= HOLD_MS) { chosen = true; break; }
                if (!down && wasDown) {
                    wasDown = false;
                    if (now - downAt >= 30) { sel = (sel + 1) % n; lastActivity = now; shownSeconds = -1; }
                }
                if (now - lastActivity >= TIMEOUT_MS) break;
                int seconds = (int)((TIMEOUT_MS - (now - lastActivity)) / 1000) + 1;
                if (seconds != shownSeconds || now - lastShown > 500) {
                    shownSeconds = seconds;
                    lastShown = now;
                    pagesBegin();
                    pagesFillRect(0, 0, 128, 9, true);
                    pagesText((128 - 6 * 11) / 2, 1, "SELECT MODE", 1, true);
                    // Lista con ventana de 4 filas (11 px de paso) que sigue a la seleccion: no invade la linea inferior (y = 56) ni el temporizador
                    const uint8_t visibleRows = 4;
                    const uint8_t first = sel >= visibleRows ? (uint8_t)(sel - visibleRows + 1) : 0;
                    for (uint8_t k = 0; k < visibleRows && (uint8_t)(first + k) < n; k++) {
                        const uint8_t i = (uint8_t)(first + k);
                        const int top = 11 + k * 11;
                        if (i == sel) {
                            pagesFillRect(8, top, 108, 10, true);
                            pagesText(12, top + 1, options[i].label, 1, true);
                        } else {
                            pagesText(12, top + 1, options[i].label, 1, false);
                        }
                    }
                    if (first > 0)                      pagesFillTriangle(119, 16, 125, 16, 122, 12, true);       // hay opciones por encima
                    if (first + visibleRows < n)        pagesFillTriangle(119, 46, 125, 46, 122, 50, true);       // y por debajo
                    pagesText(2, 56, "Click:next Hold:OK", 1, false);
                    char left[16];
                    snprintf(left, sizeof(left), "%ds", (int)(seconds > 99 ? 99 : seconds));
                    pagesText(128 - 2 - 6 * (int)strlen(left), 56, left, 1, false);
                    pagesFlush();
                }
                delay(5);
            }
            if (chosen) while (digitalRead(BUTTON_PIN) == LOW) delay(10);                  // espera a soltar para no generar otra pulsacion
            // MORSE solo se activa si se acepta a mano (pulsacion larga) y no se guarda; sin pulsar nada se arranca el ultimo modo APRS
            uint8_t mode = chosen ? options[sel].mode : (uint8_t)Config.savedMode;
            Config.operatingMode = mode;
            if (mode != MODE_MORSE && mode != MODE_WSPR && mode != MODE_LRFHSS && mode != MODE_DAPNET && mode != MODE_FT8 && mode != MODE_FT4 && mode != MODE_FST4W && mode != Config.savedMode) {
                Config.savedMode = mode;
                Config.writeFile();                                                        // el ultimo modo APRS elegido pasa a ser el predeterminado
            }
        #endif
        if (Config.operatingMode != MODE_MORSE && Config.operatingMode != MODE_WSPR && Config.operatingMode != MODE_LRFHSS && Config.operatingMode != MODE_DAPNET && Config.operatingMode != MODE_FT8 && Config.operatingMode != MODE_FT4 && Config.operatingMode != MODE_FST4W && Config.operatingMode != MODE_APRS_FSK) Config.operatingMode = MODE_APRS_LORA;
        if (Config.operatingMode == MODE_WSPR || ftMode()) { beaconManualMode = true; page = PAGE_WSPR; wsprOn = true; }
        if (Config.operatingMode == MODE_LRFHSS) { beaconManualMode = true; page = PAGE_LRF; lrfhssOn = true; }
        if (Config.operatingMode == MODE_DAPNET) { beaconManualMode = true; page = PAGE_DAP; DAPNET_Utils::reset(); }       // solo recepcion: sin balizas APRS
        if (Config.operatingMode == MODE_MORSE) { beaconManualMode = true; page = PAGE_CW; }                   // en MORSE no hay balizas APRS automaticas
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Main", "Mode: %s",
                   Config.operatingMode == MODE_WSPR ? "WSPR" : (Config.operatingMode == MODE_FT8 ? "FT8" : Config.operatingMode == MODE_FT4 ? "FT4" : Config.operatingMode == MODE_FST4W ? "FST4W" : Config.operatingMode == MODE_DAPNET ? "DAPNET" : Config.operatingMode == MODE_LRFHSS ? "LR-FHSS" : Config.operatingMode == MODE_MORSE ? "MORSE" : (Config.operatingMode == MODE_APRS_FSK ? "APRS FSK" : "APRS LoRa")));
        return (uint8_t)Config.operatingMode;
    }

    void showSplash(const String& version, const String& callsign) {
        pagesBegin();
        // arriba a la izquierda el indicativo del autor de esta version (APRS LoRa+); a la derecha la version (solo numeros)
        // y justo debajo el indicativo configurado, sin SSID
        String bare = callsign;
        int dash = bare.indexOf('-');
        if (dash > 0) bare = bare.substring(0, dash);
        pagesText(2, 1, "EA7JRS", 1, false);
        pagesText(128 - 2 - 6 * (int)version.length(), 1, version.c_str(), 1, false);
        pagesText(128 - 2 - 6 * (int)bare.length(), 10, bare.c_str(), 1, false);
        // logo: antena con ondas
        pagesFillCircle(64, 21, 2, true);
        pagesFillTriangle(64, 24, 59, 42, 69, 42, true);
        pagesText(53, 17, "(", 2, false);
        pagesText(63, 17, ")", 2, false);
        pagesText(40, 17, "(", 2, false);
        pagesText(76, 17, ")", 2, false);
        pagesText(4, 47, "APRS LoRa+", 2, false);
        pagesFlush();
    }

    static void changedPage() {
        lastIndex = 0;
        lastView = 0;
        lastCall[0] = '\0';
        listBrowse = false;
        msgMode = MM_LIST;
        msgCursor = 0;
        barUntil = millis() + BAR_VISIBLE_MS;
        if (page == PAGE_COMPASS) { fusion.reset(); lastFusionMs = millis(); }
    }

    // En los mensajes, con el cursor activo, las pulsaciones mueven el cursor en vez de cambiar de pagina
    static bool moveListCursor(int direction) {
        if (page != PAGE_LIST || !listBrowse) return false;
        uint8_t view[HeardList::CAPACITY];
        uint8_t n = stationView(view);
        if (n > 0) setListCursor(view, n, (uint8_t)((listCursor + n + direction) % n));
        return true;
    }

    static bool moveMessageCursor(int direction) {
        if (page != PAGE_MSG || msgMode == MM_LIST) return false;
        uint8_t total = msgLog.count();
        if (total == 0) return true;
        msgCursor = (uint8_t)((msgCursor + total + direction) % total);
        if (msgMode == MM_DETAIL && msgLog.markRead(msgCursor)) { touchLog(); afterReadChange(); }
        return true;
    }

    void next() {
        if (sosState == SOS_COUNTDOWN) { sosCancel(); return; }
        if (alarmShown) {                            // 1P: silencia; la siguiente cierra la pantalla de alarma
            if (alarmSounding) alarmSounding = false;
            else alarmShown = false;
            return;
        }
        if (submenuOpen) {
            uint8_t count;
            itemsFor(page, count);
            submenuSel = (submenuSel + 1) % count;
            submenuMs = millis();
            return;
        }
        if (moveMessageCursor(+1) || moveListCursor(+1)) return;
        do { page = (page + 1) % PAGE_COUNT; } while (!pageVisible(page));
        changedPage();
    }

    void previous() {
        if (sosState == SOS_COUNTDOWN) { sosCancel(); return; }
        if (alarmShown) { next(); return; }
        if (submenuOpen) {
            uint8_t count;
            itemsFor(page, count);
            submenuSel = (submenuSel + count - 1) % count;
            submenuMs = millis();
            return;
        }
        if (moveMessageCursor(-1) || moveListCursor(-1)) return;
        do { page = (page + PAGE_COUNT - 1) % PAGE_COUNT; } while (!pageVisible(page));
        changedPage();
    }

    bool back() {
        if (sosState == SOS_COUNTDOWN) { sosCancel(); return true; }
        if (alarmShown) {                            // 2P: silencia y cierra
            alarmSounding = false;
            alarmShown = false;
            return true;
        }
        if (submenuOpen) {
            submenuOpen = false;
            submenuId = SM_PAGE;
            return true;
        }
        if (page == PAGE_LIST && listBrowse) {
            listBrowse = false;
            return true;
        }
        if (page == PAGE_MSG && msgMode != MM_LIST) {          // detalle -> explorar -> lista
            msgMode = (msgMode == MM_DETAIL) ? MM_BROWSE : MM_LIST;
            return true;
        }
        return false;
    }

    void requestBeacon() {
        beaconRequested = true;
    }

    bool beaconAllowed() {
        return !beaconManualMode || beaconRequested;
    }

    void beaconSent() {
        beaconRequested = false;
    }

    // Envia una respuesta rapida al remitente del mensaje abierto, con peticion de confirmacion
    static void sendQuickReply(uint8_t index) {
        const MsgEntry* entry = msgLog.byRecent(msgCursor);
        if (!entry || index >= QUICK_COUNT) return;
        char to[11];
        snprintf(to, sizeof(to), "%s", entry->call);
        int ackId = MSG_Utils::addToOutputBuffer(1, String(to), String(QUICK_TEXT[index]));
        if (ackId > 0) {
            onMessageSent(to, QUICK_TEXT[index], (uint16_t)ackId);
            char toast[26];
            snprintf(toast, sizeof(toast), "Queued: %.14s", QUICK_TEXT[index]);
            showToast(toast);
        } else {
            showToast("Already queued");
        }
    }

    static void editZone(Command command) {
        if (command == C_ZN_MODE) {
            zoneMode = (zoneMode + 1) % ZONE_COUNT;
        } else {
            if (zoneMode != ZONE_MANUAL) {                       // editar parte del desfase que se estaba usando
                bool dst = false;
                zoneHalfHours = (int8_t)(zoneOffsetMinutes(zoneMode, zoneHalfHours, (int64_t)now(), &dst) / 30);
                zoneMode = ZONE_MANUAL;
            }
            int next = zoneHalfHours + (command == C_ZN_UP ? 1 : -1);
            if (next > 28) next = 28;                            // +14 h
            if (next < -24) next = -24;                          // -12 h
            zoneHalfHours = (int8_t)next;
        }
        saveSettings();
    }

    static void editQnh(Command command) {
        #ifdef GPS_BARO_ALTITUDE
            if (command == C_Q_AUTO) {
                BARO_Utils::clearManualQnh();
                showToast("QNH auto (GPS)");
                return;
            }
            float current = BARO_Utils::getQnh();
            if (current <= 0.0f) current = 1013.2f;
            float step = (command == C_Q_UP1) ? 1.0f : (command == C_Q_DN1) ? -1.0f : (command == C_Q_UP2) ? 0.1f : -0.1f;
            BARO_Utils::setManualQnh(roundf((current + step) * 10.0f) / 10.0f);
        #else
            (void)command;
        #endif
    }

    static void editDeclination(Command command) {
        if (command == C_DECL_MODE) {
            declAuto = !declAuto;
            if (!declAuto) {                                     // el valor manual parte del que se estaba usando
                bool viaModel = false;
                float value = currentDeclination(&viaModel);
                if (viaModel) declManualTenths = (int16_t)lroundf(value * 10.0f);
            }
        } else {
            int step = (command == C_DECL_UP1) ? 5 : (command == C_DECL_DN1) ? -5 : (command == C_DECL_UP2) ? 50 : -50;
            if (declAuto) {                                      // editar el valor pasa a modo manual desde el valor actual
                bool viaModel = false;
                float value = currentDeclination(&viaModel);
                if (viaModel) declManualTenths = (int16_t)lroundf(value * 10.0f);
                declAuto = false;
            }
            int next = declManualTenths + step;
            if (next > 300) next = 300;                          // +-30 grados es mas que suficiente para cualquier lugar habitado
            if (next < -300) next = -300;
            declManualTenths = (int16_t)next;
        }
        declCacheOk = false;
        saveSettings();
    }

    static void toggleConstellation(Command command) {
        #ifdef GPS_CONFIG_CONSTELLATIONS
            uint8_t bit = GPS_Utils::GNSS_GPS;
            const char* name = "GPS";
            switch (command) {
                case C_G_GAL:  bit = GPS_Utils::GNSS_GALILEO; name = "Galileo"; break;
                case C_G_BDS:  bit = GPS_Utils::GNSS_BEIDOU;  name = "BeiDou";  break;
                case C_G_GLO:  bit = GPS_Utils::GNSS_GLONASS; name = "GLONASS"; break;
                case C_G_QZSS: bit = GPS_Utils::GNSS_QZSS;    name = "QZSS";    break;
                default: break;
            }
            uint8_t mask = GPS_Utils::gnssMask();
            uint8_t wanted = mask ^ bit;
            int8_t result = GPS_Utils::gnssSet(wanted);
            char toast[26];
            if (result == 1) {
                snprintf(toast, sizeof(toast), "%s %s", name, (wanted & bit) ? "on" : "off");
            } else if (result == 2) {
                snprintf(toast, sizeof(toast), "Fixed on this GPS");        // L76K: Galileo, SBAS y QZSS no son configurables
            } else if (result == 0) {
                if ((wanted & (GPS_Utils::GNSS_SBAS | GPS_Utils::GNSS_QZSS)) && !(wanted & GPS_Utils::GNSS_GPS)) snprintf(toast, sizeof(toast), "SBAS/QZSS need GPS");
                else if ((wanted & GPS_Utils::GNSS_BEIDOU) && (wanted & GPS_Utils::GNSS_GLONASS)) snprintf(toast, sizeof(toast), "BeiDou xor GLONASS");
                else snprintf(toast, sizeof(toast), "Keep 1 constellation");
            } else {
                snprintf(toast, sizeof(toast), "GNSS: no reply");
            }
            showToast(toast);
        #else
            (void)command;
        #endif
    }

    // Abre la brujula apuntando a una estacion
    static void goToStation(const Station* selected) {
        if (!selected) { showToast("No station"); return; }
        if (!selected->hasPosition) { showToast("Station has no position"); return; }
        snprintf(targetCall, sizeof(targetCall), "%s", selected->call);
        compassTarget = true;
        page = PAGE_COMPASS;
        changedPage();
    }

    int longPress() {
        if (sosState == SOS_COUNTDOWN) { sosCancel(); return PAGES_CMD_NONE; }
        if (alarmShown) {                            // LP: silencia y va hacia la estacion (COMPASS con objetivo)
            alarmSounding = false;
            alarmShown = false;
            int index = heard.indexOf(alarmCall);
            const Station* st = index >= 0 ? heard.byRecent((uint8_t)index) : nullptr;
            if (!st || !st->hasPosition) showToast("No position known");
            else goToStation(st);
            return PAGES_CMD_NONE;
        }
        if (!submenuOpen) {
            if (page == PAGE_LIST && listBrowse) {               // explorando: GO TO la estacion marcada
                uint8_t view[HeardList::CAPACITY];
                uint8_t n = stationView(view);
                if (n > 0) goToStation(heard.byRecent(view[listCursor]));
                return PAGES_CMD_NONE;
            }
            if (page == PAGE_MSG && msgMode == MM_BROWSE) {      // explorando: abre el mensaje marcado
                if (msgLog.count() > 0) {
                    msgMode = MM_DETAIL;
                    if (msgLog.markRead(msgCursor)) { touchLog(); afterReadChange(); }
                }
                return PAGES_CMD_NONE;
            }
            submenuOpen = true;
            submenuId = SM_PAGE;
            submenuSel = 0;
            submenuMs = millis();
            return PAGES_CMD_NONE;
        }
        uint8_t count;
        const Item* items = itemsFor(page, count);
        Command command = items[submenuSel < count ? submenuSel : 0].cmd;
        if (command == C_GPS_GNSS) {                             // segundo nivel: constelaciones
            submenuId = SM_GNSS;
            submenuSel = 0;
            submenuMs = millis();
            return PAGES_CMD_NONE;
        }
        if (command == C_CLK_ZONE) {                             // segundo nivel: zona horaria
            submenuId = SM_ZONE;
            submenuSel = 0;
            submenuMs = millis();
            return PAGES_CMD_NONE;
        }
        if (command >= C_ZN_MODE && command <= C_ZN_DN) {        // edita la zona y sigue en el submenu
            editZone(command);
            submenuMs = millis();
            return PAGES_CMD_NONE;
        }
        if (command == C_LORA_POWER) {                           // segundo nivel: potencia
            submenuId = SM_POWER;
            submenuSel = 0;
            submenuMs = millis();
            return PAGES_CMD_NONE;
        }
        if (command >= C_PW_SHOW && command <= C_PW_DN) {        // ajusta la potencia y sigue en el submenu
            if (command != C_PW_SHOW) {
                int current = LoRa_Utils::getPower();
                int wanted = current + (command == C_PW_UP ? 1 : -1);
                int applied = LoRa_Utils::setProfilePower(wanted);
                if (applied == current) showToast(command == C_PW_UP ? "Max power" : "Min power");
            }
            submenuMs = millis();
            return PAGES_CMD_NONE;
        }
        if (command == C_ALT_QNH) {                              // segundo nivel: QNH
            submenuId = SM_QNH;
            submenuSel = 0;
            submenuMs = millis();
            return PAGES_CMD_NONE;
        }
        if (command >= C_Q_AUTO && command <= C_Q_DN2) {         // ajusta el QNH y sigue en el submenu
            editQnh(command);
            submenuMs = millis();
            return PAGES_CMD_NONE;
        }
        if (command == C_COMPASS_DECL) {                         // segundo nivel: declinacion
            submenuId = SM_DECL;
            submenuSel = 0;
            submenuMs = millis();
            return PAGES_CMD_NONE;
        }
        if (command >= C_DECL_MODE && command <= C_DECL_DN2) {   // edita la declinacion y sigue en el submenu
            editDeclination(command);
            submenuMs = millis();
            return PAGES_CMD_NONE;
        }
        if (command >= C_G_GPS && command <= C_G_QZSS) {         // alterna una constelacion y sigue en el submenu
            toggleConstellation(command);
            submenuMs = millis();
            return PAGES_CMD_NONE;
        }
        if (command == C_MSG_QUICK) {                            // segundo nivel: respuestas rapidas
            submenuId = SM_QUICK;
            submenuSel = 0;
            submenuMs = millis();
            return PAGES_CMD_NONE;
        }
        submenuOpen = false;
        submenuId = SM_PAGE;
        if (command >= C_QUICK0 && command <= C_QUICK7) {
            sendQuickReply((uint8_t)(command - C_QUICK0));
            return PAGES_CMD_NONE;
        }
        if (command >= C_CW0 && command <= C_CW4) {
            sendCw((uint8_t)(command - C_CW0));
            return PAGES_CMD_NONE;
        }
        switch (command) {
            case C_BEACON:      return PAGES_CMD_BEACON;
            case C_PROFILE:     return PAGES_CMD_PROFILE;
            case C_MODE_CHANGE:
                showToast("Restarting...");
                show();
                delay(600);
                ESP.restart();
                break;
            case C_MENU:        return PAGES_CMD_MENU;
            case C_DISPLAY_OFF: return PAGES_CMD_DISPLAY_OFF;
            case C_GPS:         return PAGES_CMD_GPS;
            case C_BEACON_MODE:
                beaconManualMode = !beaconManualMode;
                beaconRequested = false;
                break;
            case C_LAST_VIEW:
                lastView = lastView ? 0 : 1;
                break;
            case C_LAST_NEXT:
            case C_LAST_PREV: {
                uint8_t total = heard.count();
                if (total == 0) break;
                lastIndex = (command == C_LAST_NEXT) ? (lastIndex + 1) % total : (lastIndex + total - 1) % total;
                const Station* selected = heard.byRecent(lastIndex);
                if (lastIndex > 0 && selected) snprintf(lastCall, sizeof(lastCall), "%s", selected->call);
                else lastCall[0] = '\0';
                break;
            }
            case C_GOTO:
                goToStation(heard.byRecent(lastIndex));
                break;
            case C_GPS_COORD:
                coordFormat = (coordFormat + 1) % COORD_COUNT;
                saveSettings();
                break;
            case C_GPS_UNITS:
                unitSystem = (unitSystem + 1) % UNITS_COUNT;
                saveSettings();
                break;
            case C_GPS_SBAS: {
                #ifdef GPS_CONFIG_CONSTELLATIONS
                    uint8_t wanted = GPS_Utils::gnssMask() ^ GPS_Utils::GNSS_SBAS;
                    int8_t result = GPS_Utils::gnssSet(wanted);
                    if (result == 1) showToast((wanted & GPS_Utils::GNSS_SBAS) ? "SBAS on" : "SBAS off");
                    else if (result == 2) showToast("No SBAS on L76K");
                    else if (result == 0) showToast("SBAS needs GPS");
                    else showToast("GNSS: no reply");
                #endif
                break;
            }
            case C_LIST_BROWSE: {
                uint8_t view[HeardList::CAPACITY];
                uint8_t n = stationView(view);
                if (n == 0) { showToast("No stations"); break; }
                listBrowse = true;
                setListCursor(view, n, 0);
                break;
            }
            case C_LIST_SORT:
                listSort = (listSort + 1) % SORT_COUNT;
                break;
            case C_LIST_FILTER:
                listFilter = (listFilter + 1) % FILTER_COUNT;
                break;
            case C_SOS_SEND:
                sosStart();
                break;
            case C_SOS_CANCEL:
                sosCancel();
                break;
            case C_LORA_PROFILE:
                LoRa_Utils::changeFreq();
                break;
            case C_CW_LISTEN:
                cwListenSession();
                break;
            case C_CW_TRAIN_LISTEN:
                cwTrainerListen();
                break;
            case C_CW_TRAIN_KEY:
                cwTrainerKeying();
                break;
            case C_WSPR_TOGGLE:
                wsprOn = !wsprOn;
                wsprNextAt = 0;
                wsprStatus[0] = '\0';
                showToast(ftMode() ? (wsprOn ? "Beacon ON" : "Beacon OFF") : (wsprOn ? "WSPR beacon ON" : "WSPR beacon OFF"));
                break;
            case C_LRF_TOGGLE:
                lrfhssOn = !lrfhssOn;
                lrfhssNextAt = 0;
                lrfhssStatus[0] = '\0';
                showToast(lrfhssOn ? "LR-FHSS beacon ON" : "LR-FHSS beacon OFF");
                break;
            case C_WSPR_TUNE:
                wsprTune();
                break;
            case C_CW_BEACON:
                cwBeaconInit = true;
                cwBeaconOn = !cwBeaconOn;
                if (cwBeaconOn) cwBeaconNext = millis() + 5000UL;
                showToast(cwBeaconOn ? "CW beacon ON" : "CW beacon OFF");
                break;
            case C_CWB_TUNE:
                cwTune();
                break;
            case C_DAP_OLDER:
                if ((uint8_t)(dapIndex + 1) < DAPNET_Utils::count()) dapIndex++;
                break;
            case C_DAP_NEWER:
                if (dapIndex > 0) dapIndex--;
                break;
            case C_DAP_CLEAR:
                DAPNET_Utils::clear();
                dapIndex = 0;
                showToast("List cleared");
                break;
            case C_DAP_MON:
                Config.dapnet.monitorAll = !Config.dapnet.monitorAll;                         // solo hasta el reinicio (la WebUI lo guarda)
                showToast(Config.dapnet.monitorAll ? "Monitor all ON" : "Monitor all OFF");
                break;
            case C_CWB_NOW:
                cwBeaconInit = true;
                cwBeaconRun(Config.cw.beaconMin > 0 ? (uint32_t)Config.cw.beaconMin : 10UL);
                break;
            case C_CW_CLEAR:
                cwText = "";
                showToast("Text cleared");
                break;
            case C_LORA_RESET:
                txCount = 0;
                rxCount = 0;
                digiCount = 0;
                haveTx = false;
                airtime.clear();
                showToast("Counters reset");
                break;
            case C_LORA_HEARD:
                page = PAGE_LIST;
                changedPage();
                break;
            case C_LIST_CLEAR:
                heard.clear();
                lastIndex = 0;
                lastCall[0] = '\0';
                targetCall[0] = '\0';
                listBrowse = false;
                showToast("List cleared");
                break;
            case C_MSG_OPEN: {                                   // abre directamente un mensaje: el recibido sin leer mas reciente, si no el ultimo recibido
                const uint8_t total = msgLog.count();
                int target = -1;
                for (uint8_t i = 0; i < total && target < 0; i++) { const MsgEntry* e = msgLog.byRecent(i); if (e && MessageLog::stateOf(*e) == MSTATE_UNREAD) target = i; }
                for (uint8_t i = 0; i < total && target < 0; i++) { const MsgEntry* e = msgLog.byRecent(i); if (e && e->dir == 'R') target = i; }
                if (target < 0 && total > 0) target = 0;
                if (target >= 0) {
                    msgCursor = (uint8_t)target;
                    msgMode = MM_DETAIL;
                    if (msgLog.markRead(msgCursor)) { touchLog(); afterReadChange(); }
                }
                break;
            }
            case C_MSG_BROWSE:
                if (msgLog.count() > 0) { msgMode = MM_BROWSE; msgCursor = 0; }
                break;
            case C_MSG_READALL:
                if (msgLog.markAllRead()) touchLog();
                afterReadChange();
                break;
            case C_MSG_DELALL:
                MSG_Utils::deleteFile(0);                        // vacia tambien el fichero clasico
                onMessagesCleared();
                afterReadChange();
                showToast("Messages deleted");
                break;
            case C_MSG_DELETE:
                if (msgLog.removeRecent(msgCursor)) touchLog();
                if (msgCursor >= msgLog.count() && msgCursor > 0) msgCursor--;
                if (msgLog.count() == 0) msgMode = MM_LIST;
                else msgMode = MM_BROWSE;
                afterReadChange();
                break;
            case C_MSG_REPLY: {
                const MsgEntry* entry = msgLog.byRecent(msgCursor);
                if (!entry) break;
                snprintf(replyTo, sizeof(replyTo), "%s", entry->call);
                return PAGES_CMD_REPLY;
            }
            case C_COMPASS_TARGET:
                compassTarget = !compassTarget;
                targetCall[0] = '\0';
                arrivedNotified = false;
                break;
            case C_COMPASS_NEXT: {
                uint8_t total = heard.count();
                int current = compassTarget ? heard.indexOf(targetCall) : -1;
                int index = current < 0 ? 0 : current + 1;
                bool found = false;
                for (uint8_t step = 0; step < total && !found; step++, index++) {
                    const Station* s = heard.byRecent((uint8_t)(index % total));
                    if (s && s->hasPosition) {
                        snprintf(targetCall, sizeof(targetCall), "%s", s->call);
                        compassTarget = true;
                        arrivedNotified = false;
                        found = true;
                    }
                }
                if (!found) showToast("No station with pos");
                break;
            }
            case C_COMPASS_MODE:
                compassMode = (compassMode + 1) % 3;
                fusion.reset();
                saveSettings();
                break;
            case C_COMPASS_NORTH:
                northTrue = !northTrue;
                saveSettings();
                break;
            case C_COMPASS_RESET:
                MAG_Utils::resetAlignment();
                showToast("Alignment reset");
                break;
            case C_ALT_SPAN:
                altSpanMode = (altSpanMode + 1) % 3;
                break;
            case C_CLK_FORMAT:
                clock24h = !clock24h;
                saveSettings();
                break;
            case C_CLK_SYNC:
                GPS_Utils::setDateFromData();
                showToast(gps.time.isValid() ? "Clock synced" : "No GPS time");
                break;
            case C_ALT_RESET:
                altProfile.clear();
                showToast("Profile reset");
                break;
            default:
                break;
        }
        return PAGES_CMD_NONE;
    }

    bool wantsFastRefresh() {
        return viewing() && (page == PAGE_COMPASS || sosState == SOS_COUNTDOWN || alarmShown);
    }

}

#endif