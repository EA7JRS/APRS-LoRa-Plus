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
#include "gnss_sky.h"
#include "logo_splash.h"
#ifdef HAS_TFT
    #include "logo_splash_color.h"      // bienvenida a color (Heltec Wireless Tracker, T-Deck)
    #include "aprs_icons_color.h"       // iconos APRS a color (Heltec Wireless Tracker, T-Deck)
#endif

#ifdef HAS_APRS_PAGES

#include <TinyGPS++.h>
#include <TimeLib.h>
#include <WiFi.h>
#include <stdarg.h>
#include <vector>
#include <SPIFFS.h>
#include "battery_utils.h"
#include "battery_model.h"
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
#include "time_sync.h"
#include "wx_utils.h"
#include "utils.h"
#include "lora_utils.h"
#ifdef HAS_SA868
    #include "sa868_radio.h"
    #include "fm_utils.h"
    #include "fm_core.h"
    #include "ble_control.h"
#include "astro.h"            // lib/aprs_formulas (formulas del plotter): luna
#include "wind.h"             // Beaufort
#endif
#include "cw_morse.h"
#include "bcast_radio.h"
#include "wspr.h"
#include "ft_beacon.h"
#include "fst4w.h"
#include "lrfhss.h"
#include "aprs_icons.h"
#include <logger.h>
#include "wmm.h"
#include "notification_utils.h"
#include "dapnet_utils.h"
#include "sat_config.h"
#include "sat_utils.h"
#include "net_wx.h"
#include "ais_utils.h"
#include "rotor_utils.h"
#include "rotor_follow.h"
#include "sat_rx.h"
#include "sat_tx.h"
#include "station_utils.h"
#include "sleep_utils.h"
#include "keypad_fh2.h"
#include "kiss_net.h"
#include "cat_net.h"
#include "iaru_auto.h"
#include "ble_control.h"
#include "astro.h"            // lib/aprs_formulas (formulas del plotter): luna
#include "wind.h"             // Beaufort

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

    enum Page : uint8_t { PAGE_MAIN, PAGE_MSG, PAGE_CW, PAGE_CWB, PAGE_WSPR, PAGE_LRF, PAGE_DAP, PAGE_SAT, PAGE_ROT, PAGE_LAST, PAGE_LIST, PAGE_GPS, PAGE_COMPASS, PAGE_ALT, PAGE_WX, PAGE_CLOCK, PAGE_LINK, PAGE_BOARD, PAGE_TEAM, PAGE_FM, PAGE_BCAST, PAGE_AIS, PAGE_COUNT };

    // FT8, FT4 y FST4W comparten la pagina, el estado y el menu de la baliza WSPR (solo uno de los dos modos esta activo a la vez)
    bool ftMode() { return Config.operatingMode == MODE_FT8 || Config.operatingMode == MODE_FT4 || Config.operatingMode == MODE_FST4W; }

    // Pagina ROTOR en los modos APRS: solo con el rotor activado y siguiendo un objetivo terrestre (la fuente "satelite" va en el modo SAT)
    bool rotorPageOn() {
    #ifdef HAS_SAT
        return Config.rotor.enabled && Config.rotor.source != 0;
    #else
        return false;
    #endif
    }

    // El modo MORSE tiene una unica pagina (MORSE); los modos APRS tienen las demas y no la de MORSE
    bool pageVisible(uint8_t p) {
        if (p == PAGE_BCAST) return BCAST::chip() != BCAST::CHIP_NONE;      // radio comercial por I2C: en cualquier modo
        if (Config.operatingMode == MODE_MORSE) return p == PAGE_CW || p == PAGE_CWB;       // manipulador/escucha y baliza CW
        if (Config.operatingMode == MODE_WSPR || ftMode()) return p == PAGE_WSPR;
        if (Config.operatingMode == MODE_LRFHSS) return p == PAGE_LRF;
        if (Config.operatingMode == MODE_DAPNET) return p == PAGE_DAP;
        if (Config.operatingMode == MODE_SAT) return p == PAGE_SAT;
        if (Config.operatingMode == MODE_AIS) return p == PAGE_AIS || p == PAGE_GPS || p == PAGE_COMPASS || p == PAGE_CLOCK || p == PAGE_WX;   // receptor AIS de la T-TWR
        if (p == PAGE_AIS) return AIS_Utils::active();                       // con un modulo AIS externo o barcos de la app
        if (p == PAGE_FM) return Config.operatingMode == MODE_FM;           // T-TWR: emisora de voz, junto a las paginas APRS
        return p != PAGE_CW && p != PAGE_CWB && p != PAGE_WSPR && p != PAGE_LRF && p != PAGE_DAP && p != PAGE_SAT && (p != PAGE_ROT || rotorPageOn()) && (p != PAGE_TEAM || Config.team.watchActive);
    }
    // Pagina de entrada de cada modo (al arrancar y si la actual deja de verse)
    uint8_t modeHomePage() {
        switch (Config.operatingMode) {
            case MODE_MORSE:  return PAGE_CW;
            case MODE_LRFHSS: return PAGE_LRF;
            case MODE_DAPNET: return PAGE_DAP;
            case MODE_SAT:    return PAGE_SAT;
            case MODE_AIS:    return PAGE_AIS;
            case MODE_FM:     return PAGE_FM;
            default:          return (Config.operatingMode == MODE_WSPR || ftMode()) ? PAGE_WSPR : PAGE_MAIN;
        }
    }
    bool aprsMode() { return Config.operatingMode != MODE_AIS && Config.operatingMode != MODE_MORSE && Config.operatingMode != MODE_WSPR && Config.operatingMode != MODE_LRFHSS && Config.operatingMode != MODE_DAPNET && Config.operatingMode != MODE_SAT && !ftMode(); }

    // Pin del manipulador / teclado FH-2: -1 (sin usar) si es negativo o si lo ocupa la propia placa (Supreme: radio, GPS, IMU, PMU...)
    int cwPin(int pin) { return (pin >= 0 && !POWER_Utils::pinReserved(pin)) ? pin : -1; }

    const char* const PAGE_TITLES[PAGE_COUNT] = {
        "MAIN", "MESSAGES", "MORSE", "CW BEACON", "WSPR", "LR-FHSS", "DAPNET", "SATELLITE", "ROTOR", "LAST APRS", "STATIONS", "GPS", "COMPASS", "ALTIMETER", "TELEMETRY", "CLOCK", "LORA", "SYSTEM", "GROUP", "FM", "BCAST", "AIS"
    };

    // Opciones del submenu (pulsacion larga). Las acciones del Tracker (baliza, perfil, menu) las ejecuta el teclado.
        // Lista de ordenes del submenu; el nombre en minusculas (sin C_) es el que usa la app por BLE (orden "act")
    #define COMMAND_LIST(X) \
        X(BACK) X(BEACON) X(BEACON_MODE) X(PROFILE) X(MENU) X(DISPLAY_OFF) X(GPS) X(LAST_NEXT) X(LAST_PREV) X(LAST_VIEW) X(GOTO) X(LIST_BROWSE) \
        X(LIST_SORT) X(LIST_FILTER) X(LIST_CLEAR) X(COMPASS_NEXT) X(COMPASS_MODE) X(COMPASS_NORTH) X(COMPASS_DECL) X(COMPASS_RESET) X(DECL_MODE) \
        X(DECL_UP1) X(DECL_DN1) X(DECL_UP2) X(DECL_DN2) X(GPS_COORD) X(GPS_UNITS) X(GPS_SBAS) X(GPS_GNSS) X(G_GPS) X(G_GAL) X(G_BDS) X(G_GLO) \
        X(G_QZSS) X(MSG_OPEN) X(MSG_BROWSE) X(MSG_READALL) X(MSG_DELALL) X(MSG_REPLY) X(MSG_QUICK) X(MSG_DELETE) X(COMPASS_TARGET) X(ALT_SPAN) \
        X(ALT_RESET) X(ALT_QNH) X(CLK_ZONE) X(CLK_FORMAT) X(CLK_SYNC) X(ZN_MODE) X(ZN_UP) X(ZN_DN) X(Q_AUTO) X(Q_UP1) X(Q_DN1) X(Q_UP2) X(Q_DN2) \
        X(SOS_SEND) X(SOS_CANCEL) X(LORA_PROFILE) X(LORA_POWER) X(LORA_RESET) X(LORA_HEARD) X(PW_SHOW) X(PW_UP) X(PW_DN) X(QUICK0) X(QUICK1) \
        X(QUICK2) X(QUICK3) X(QUICK4) X(QUICK5) X(QUICK6) X(QUICK7) X(CW0) X(CW1) X(CW2) X(CW3) X(CW4) X(CW_LISTEN) X(CW_SQUELCH) X(CW_TRAIN_LISTEN) \
        X(CW_TRAIN_KEY) X(CW_CLEAR) X(CW_BEACON) X(WSPR_TOGGLE) X(LRF_TOGGLE) X(WSPR_TUNE) X(CWB_NOW) X(CWB_TUNE) X(DAP_OLDER) X(DAP_NEWER) \
        X(DAP_CLEAR) X(DAP_MON) X(SAT_NEXT) X(SAT_PREV) X(SAT_VIEW) X(SAT_CAT) X(SATRX_TOGGLE) X(SATRX_TX) X(SATRX_FINE_UP) \
        X(SATRX_FINE_DN) X(SATRX_LORA) X(SATRX_CLEAR) X(SATTX_SEND) X(ROT_SOURCE) X(ROT_NEXT) X(ROT_PREV) X(ROT_PARK) X(MODE_CHANGE) X(TEAM_BROWSE) \
        X(TEAM_RADAR) X(TEAM_LIST) X(TEAM_AUTO) X(TEAM_QUICK) X(TEAM_PING) X(TEAM_GOTO) X(SYS_BT) X(SYS_BTPROTO) X(SYS_KISSWIFI) X(SYS_CATWIFI) \
        X(SYS_IARU) X(FM_VM) X(FM_EDIT) X(FM_SHIFT) X(FM_TONEMODE) X(FM_STEP) X(FM_POWER) X(FM_WIDE) X(FM_SQLAUTO) X(FM_NR) \
        X(FM_SCAN) X(FM_STORE) X(FM_ARTS) X(FM_EAI) X(FM_APRS) X(RADIO_TUNE) X(QSY) X(FM_SAT) X(FM_SATNEXT) X(FM_SATTX) X(SAT_FAV) X(SATTX_POS) X(WX_NET) X(SAT_PHONE) X(AIS_VIEW) X(AIS_NEXT) X(AIS_PREV) X(AIS_GOTO) X(AIS_CH) X(AIS_ALARM) X(AIS_CLEAR) \
        X(BC_SEEKUP) X(BC_SEEKDN) X(BC_VOLUP) X(BC_VOLDN) X(BC_MUTE) X(BC_BAND) X(BC_MONO) X(BC_STORE) X(BC_PRESET) X(BC_POWER) X(SYS_THEME)
    enum Command : uint8_t {
    #define X(n) C_##n,
        COMMAND_LIST(X)
    #undef X
    };
    static const char* const COMMAND_NAMES[] = {
    #define X(n) #n,
        COMMAND_LIST(X)
    #undef X
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
#ifdef HAS_SA868
    const Item ITEMS_LAST[]    = { {"Next view", C_LAST_VIEW}, {"Older station", C_LAST_NEXT}, {"Newer station", C_LAST_PREV}, {"GO TO", C_GOTO},
                                   {"QSY (FM voice)", C_QSY}, {"Back", C_BACK} };
    const Item ITEMS_RADIO[]   = { {"Tune carrier", C_RADIO_TUNE}, {"Power", C_LORA_POWER}, {"Reset counters", C_LORA_RESET}, {"Heard list", C_LORA_HEARD}, {"Back", C_BACK} };
    Item            fmItems[24];
    char            fmLabels[12][24];
#else
    const Item ITEMS_LAST[]    = { {"Next view", C_LAST_VIEW}, {"Older station", C_LAST_NEXT}, {"Newer station", C_LAST_PREV}, {"GO TO", C_GOTO}, {"Back", C_BACK} };
#endif
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
    const Item ITEMS_WX[]      = { {"Update from phone", C_WX_NET}, {"Back", C_BACK} };
    // GROUP (estilo Group Monitor de Yaesu): lista, cursor sobre un miembro, o radar tipo APL
    const Item ITEMS_TEAM[]       = { {"Browse", C_TEAM_BROWSE}, {"Radar", C_TEAM_RADAR}, {"Quick msg: all", C_TEAM_QUICK}, {"Ping all", C_TEAM_PING}, {"Back", C_BACK} };
    const Item ITEMS_TEAM_PICK[]  = { {"GO TO", C_TEAM_GOTO}, {"Quick msg", C_TEAM_QUICK}, {"Ping", C_TEAM_PING}, {"Back", C_BACK} };
    const Item ITEMS_TEAM_RADAR[] = { {"Auto scale", C_TEAM_AUTO}, {"List", C_TEAM_LIST}, {"Back", C_BACK} };
    enum TeamMode : uint8_t { TMODE_LIST, TMODE_BROWSE, TMODE_RADAR };
    uint8_t         teamMode            = TMODE_LIST;
    uint8_t         teamCursor          = 0;
    uint8_t         teamScale           = 0;        // 0 = automatica; 1.. = indice en TEAM_SCALES_KM
    uint8_t         boardView           = 0;        // SYSTEM: 0 = sistema, 1 = vista LINK (conexiones activas), 2 = BATTERY (no son paginas aparte)
    uint8_t         gpsView             = 0;        // GPS: 0 = posicion, 1 = GNSS (constelaciones, bandas, cielo) (EA7JRS)
    uint8_t         clockView           = 0;        // CLOCK: 0 = reloj, 1 = SUN (orto y ocaso), 2 = MOON (fase) (EA7JRS)
    Item            sysItems[8];                    // submenu de SYSTEM
    char            sysBtLabel[24];
    char            sysProtoLabel[24];
    char            sysKissLabel[24];
    char            sysCatLabel[24];
    char            sysThemeLabel[24];

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
    uint32_t        sosEndMs            = 0;            // emergencia remota (EAI): instante en que termina
    bool            sosTimed            = false;
    char            sosBy[11]           = "";           // EAI remota: quien la inicio (para la app)
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
    Item            satItems[20];
    Item            aisItems[10];
    char            aisChLabel[24];
    uint8_t         aisView = 0;                 // 0 lista, 1 detalle
    int             aisSel = 0;                  // barco elegido (posicion en la lista por distancia)
    char            satListenLabel[24];
    Item            rotItems[5];
    char            rotSourceLabel[28];
    char            satCatLabel[28];
    char            satFavLabel[20];
    uint8_t         satView = 0;                 // 0 = ahora (az/el/Doppler), 1 = proximo paso, 2 = frecuencias de los transmisores
    uint8_t         satTx = 0;                   // transmisor mostrado en la vista de frecuencias
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
    Item            cwItems[CW_MESSAGE_COUNT + 8];
    char            cwBeaconLabel[20];
    char            cwSquelchLabel[20];
    Item            cwbItems[5];
    char            cwbToggleLabel[20];
    uint16_t        cwBeaconTxCount = 0;         // balizas CW completas enviadas desde el arranque
    char            cwBeaconStatus[22] = "";     // ultimo resultado ("Sent #n" / "Aborted")
    uint32_t        cwBeaconStatusAt = 0;
    bool            cwBeaconOn = false;          // baliza CW autonoma activa (solo modo MORSE)
    bool            cwBeaconInit = false;
    uint32_t        cwBeaconNext = 0;
    char            cwLabels[CW_MESSAGE_COUNT][22];

    Item            bcItems[11];
    char            bcLabels[4][24];

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
            case PAGE_SAT:
                satItems[0] = Item{"Next satellite", C_SAT_NEXT};
                satItems[1] = Item{"Previous satellite", C_SAT_PREV};
                satItems[2] = Item{"Change view", C_SAT_VIEW};
                snprintf(satCatLabel, sizeof(satCatLabel), "List: %s", SAT_Utils::categoryName());
                satItems[3] = Item{satCatLabel, C_SAT_CAT};
                count = 4;
                {
                    const SatCore::Block* fb = SAT_Utils::block();
                    snprintf(satFavLabel, sizeof(satFavLabel), "Favorite: %s", fb && SAT_Utils::isFavorite(fb->noradId) ? "ON" : "OFF");
                    if (fb) satItems[count++] = Item{satFavLabel, C_SAT_FAV};
                }
            #ifdef HAS_SAT
                if (SATRX_Utils::available()) {
                    snprintf(satListenLabel, sizeof(satListenLabel), "Listen: %s", SATRX_Utils::active() ? "ON" : "OFF");
                    satItems[count++] = Item{satListenLabel, C_SATRX_TOGGLE};
                    satItems[count++] = Item{"Next transmitter", C_SATRX_TX};
                    if (Config.sat.txEnabled) satItems[count++] = Item{"Send message (TX)", C_SATTX_SEND};
                    if (Config.sat.txEnabled && SATTX_Utils::canSendPosition()) satItems[count++] = Item{"Send position (TX)", C_SATTX_POS};
                    if (SATRX_Utils::active()) {
                        satItems[count++] = Item{"Fine +100 Hz", C_SATRX_FINE_UP};
                        satItems[count++] = Item{"Fine -100 Hz", C_SATRX_FINE_DN};
                        if (SATRX_Utils::kind() == SATRX_Utils::K_LORA) satItems[count++] = Item{"LoRa SF/BW", C_SATRX_LORA};
                        satItems[count++] = Item{"Clear RX log", C_SATRX_CLEAR};
                    }
                }
            #endif
                if (Config.rotor.enabled) { snprintf(rotSourceLabel, sizeof(rotSourceLabel), "Rotor: %s", RotorFollow::sourceName(Config.rotor.source)); satItems[count++] = Item{rotSourceLabel, C_ROT_SOURCE}; }
                satItems[count++] = Item{"Update data (phone)", C_SAT_PHONE};
                satItems[count++] = Item{"Change mode", C_MODE_CHANGE};
                satItems[count++] = Item{"Back", C_BACK};
                return satItems;
            case PAGE_ROT:
                snprintf(rotSourceLabel, sizeof(rotSourceLabel), "Follow: %s", RotorFollow::sourceName(Config.rotor.source));
                rotItems[0] = Item{rotSourceLabel, C_ROT_SOURCE};
                rotItems[1] = Item{"Next fixed target", C_ROT_NEXT};
                rotItems[2] = Item{"Previous fixed target", C_ROT_PREV};
                rotItems[3] = Item{"Back", C_BACK};
                count = 4;
                return rotItems;
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
                if (Config.cw.squelchDbm == 0) snprintf(cwSquelchLabel, sizeof(cwSquelchLabel), "CW squelch: OFF");
                else                           snprintf(cwSquelchLabel, sizeof(cwSquelchLabel), "CW squelch: %d", Config.cw.squelchDbm);
                cwItems[CW_MESSAGE_COUNT + 1] = Item{cwSquelchLabel, C_CW_SQUELCH};
                cwItems[CW_MESSAGE_COUNT + 2] = Item{"Trainer: listen", C_CW_TRAIN_LISTEN};
                cwItems[CW_MESSAGE_COUNT + 3] = Item{"Trainer: keying", C_CW_TRAIN_KEY};
                cwItems[CW_MESSAGE_COUNT + 4] = Item{"Clear text", C_CW_CLEAR};
                snprintf(cwBeaconLabel, sizeof(cwBeaconLabel), "CW beacon: %s", cwBeaconOn ? "ON" : "OFF");
                cwItems[CW_MESSAGE_COUNT + 5] = Item{cwBeaconLabel, C_CW_BEACON};
                cwItems[CW_MESSAGE_COUNT + 6] = Item{"Change mode", C_MODE_CHANGE};
                cwItems[CW_MESSAGE_COUNT + 7] = Item{"Back", C_BACK};
                count = CW_MESSAGE_COUNT + 8;
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
            case PAGE_BCAST: {
                uint8_t n = 0;
                bcItems[n++] = Item{"Seek up", C_BC_SEEKUP};
                bcItems[n++] = Item{"Seek down", C_BC_SEEKDN};
                bcItems[n++] = Item{"Volume +", C_BC_VOLUP};
                bcItems[n++] = Item{"Volume -", C_BC_VOLDN};
                bcItems[n++] = Item{BCAST::muted() ? "Unmute" : "Mute", C_BC_MUTE};
                snprintf(bcLabels[0], sizeof(bcLabels[0]), "Band: %s", BCAST::bandName(BCAST::band()));
                bcItems[n++] = Item{bcLabels[0], C_BC_BAND};
                bcItems[n++] = Item{Config.bcast.mono ? "Stereo" : "Mono", C_BC_MONO};
                bcItems[n++] = Item{"Store preset", C_BC_STORE};
                bcItems[n++] = Item{"Next preset", C_BC_PRESET};
                bcItems[n++] = Item{BCAST::isOn() ? "Radio off" : "Radio on", C_BC_POWER};
                bcItems[n++] = Item{"Back", C_BACK};
                count = n;
                return bcItems;
            }
            case PAGE_COMPASS:  PAGES_ITEMS(ITEMS_COMPASS);
            case PAGE_ALT:      PAGES_ITEMS(ITEMS_ALT);
            case PAGE_CLOCK:    PAGES_ITEMS(ITEMS_CLOCK);
            case PAGE_WX:       PAGES_ITEMS(ITEMS_WX);
            case PAGE_AIS: {
                uint8_t n = 0;
                aisItems[n++] = Item{aisView ? "View: list" : "View: details", C_AIS_VIEW};
                aisItems[n++] = Item{"Next vessel", C_AIS_NEXT};
                aisItems[n++] = Item{"Previous vessel", C_AIS_PREV};
                aisItems[n++] = Item{"GO TO", C_AIS_GOTO};
                if (Config.operatingMode == MODE_AIS) {
                    snprintf(aisChLabel, sizeof(aisChLabel), "Channel: %s", Config.ais.channel == 2 ? "A/B" : (Config.ais.channel == 1 ? "B 162.025" : "A 161.975"));
                    aisItems[n++] = Item{aisChLabel, C_AIS_CH};
                }
                aisItems[n++] = Item{Config.ais.cpaAlarm ? "Collision alarm: ON" : "Collision alarm: OFF", C_AIS_ALARM};
                aisItems[n++] = Item{"Clear list", C_AIS_CLEAR};
                if (Config.operatingMode == MODE_AIS) aisItems[n++] = Item{"Change mode", C_MODE_CHANGE};
                aisItems[n++] = Item{"Back", C_BACK};
                count = n;
                return aisItems;
            }
#ifdef HAS_SA868
            case PAGE_LINK:     PAGES_ITEMS(ITEMS_RADIO);
            case PAGE_FM: {
                static const char* const EDITS[FM_Utils::ED_COUNT] = { "FREQ", "TONE", "OFFSET", "SQUELCH" };
                static const char* const NR[4] = { "Off", "Low", "Mid", "High" };
                const FmCore::Channel& c = FM_Utils::channel();
                uint8_t n = 0, l = 0;
                auto add = [&](const char* label, Command cmd) { fmItems[n++] = Item{label, cmd}; };
                auto lab = [&](Command cmd, const char* fmt, auto... a) { snprintf(fmLabels[l], sizeof(fmLabels[l]), fmt, a...); add(fmLabels[l++], cmd); };
                add(FM_Utils::memMode() ? "Go to VFO" : "Go to memory", C_FM_VM);
                lab(C_FM_EDIT, "Dial: %s", EDITS[FM_Utils::edit() % FM_Utils::ED_COUNT]);
                lab(C_FM_SHIFT, "Shift: %s", c.shift == 0 ? "simplex" : (c.shift < 0 ? "-" : "+"));
                lab(C_FM_TONEMODE, "Tone: %s", FmCore::toneModeName(c.toneMode));
                lab(C_FM_STEP, "Step: %.2f kHz", FM_Utils::stepHz() / 1000.0);
                lab(C_FM_POWER, "Power: %s", c.high ? "High" : "Low");
                lab(C_FM_WIDE, "Width: %s", c.wide ? "Wide" : "Narrow");
                lab(C_FM_SQLAUTO, "Squelch: %s", Config.fm.squelchAuto ? "Auto" : "Manual");
                lab(C_FM_NR, "Noise red.: %s", NR[Config.fm.nrLevel & 3]);
                add(FM_Utils::scanning() ? "Stop scan" : (Config.fm.scanMemories ? "Scan memories" : "Scan band"), C_FM_SCAN);
                add("Store to memory", C_FM_STORE);
                add(FM_Utils::artsOn() ? "ARTS: stop" : "ARTS: start", C_FM_ARTS);
                add(FM_Utils::eaiOn() ? "EAI: stop" : "EAI: start", C_FM_EAI);
                add(Config.fm.aprsBeacon ? "APRS beacon: ON" : "APRS beacon: OFF", C_FM_APRS);
                #ifdef HAS_SAT
                if (FM_Utils::satOn()) {
                    const SatCore::Block* sb = SAT_Utils::block();
                    lab(C_FM_SAT, "SAT %.10s: OFF", sb ? sb->name : "");
                    add("SAT: next transmitter", C_FM_SATTX);
                    if (FM_Utils::satAprs()) add("SAT: send position", C_SATTX_POS);
                } else {
                    add("Satellite: ON", C_FM_SAT);
                }
                add("SAT: next satellite", C_FM_SATNEXT);
                #endif
                add("Tune carrier", C_RADIO_TUNE);
                add("Change mode", C_MODE_CHANGE);
                add("Back", C_BACK);
                count = n;
                return fmItems;
            }
#else
            case PAGE_LINK:     PAGES_ITEMS(ITEMS_LORA);
#endif
            case PAGE_BOARD: {                                        // Bluetooth, protocolo Bluetooth y KISS por WiFi
                uint8_t n = 0;
                snprintf(sysBtLabel, sizeof(sysBtLabel), "Bluetooth: %s", bluetoothActive ? "ON" : "OFF");
                sysItems[n++] = Item{sysBtLabel, C_SYS_BT};
                snprintf(sysProtoLabel, sizeof(sysProtoLabel), "BT data: %s", Config.bluetooth.useKISS ? "KISS" : "TNC2");
                sysItems[n++] = Item{sysProtoLabel, C_SYS_BTPROTO};
                #if KISS_NET_SUPPORTED
                    snprintf(sysKissLabel, sizeof(sysKissLabel), "WiFi KISS: %s", Config.kissNet.enabled ? "ON" : "OFF");
                    sysItems[n++] = Item{sysKissLabel, C_SYS_KISSWIFI};
                    snprintf(sysCatLabel, sizeof(sysCatLabel), "WiFi CAT: %s", Config.cat.enabled ? "ON" : "OFF");
                    sysItems[n++] = Item{sysCatLabel, C_SYS_CATWIFI};
                #endif
                sysItems[n++] = Item{"IARU: detect now", C_SYS_IARU};     // region IARU segun el GPS (Config.iaruAuto)
                if (pagesHasColor()) {                                    // pantallas a color: tema oscuro / claro / automatico
                    static const char* const THEMES[3] = { "dark", "light", "auto" };
                    snprintf(sysThemeLabel, sizeof(sysThemeLabel), "Theme: %s", THEMES[Config.display.theme % 3]);
                    sysItems[n++] = Item{sysThemeLabel, C_SYS_THEME};
                }
                sysItems[n++] = Item{"Back", C_BACK};
                count = n;
                return sysItems;
            }
            case PAGE_TEAM:
                if (teamMode == TMODE_BROWSE) { PAGES_ITEMS(ITEMS_TEAM_PICK); }
                if (teamMode == TMODE_RADAR)  { PAGES_ITEMS(ITEMS_TEAM_RADAR); }
                PAGES_ITEMS(ITEMS_TEAM);
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
                else snprintf(buffer, sizeof(buffer), BARO_Utils::isManualQnh() ? "Manual %.1f" : BARO_Utils::isNetQnh() ? "Net %.1f" : "Auto %.1f", value);
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
    // Ancho del lienzo (Manuel - EA7JRS): 128 en las OLED y la T-Deck, 160 en la Heltec Wireless Tracker.
    // XR = lo que sobra a la derecha respecto a 128 (los dibujos pegados al borde derecho se desplazan eso); COLS = columnas de 6 px.
    const int       PW                  = PAGES_W;
    const int       XR                  = PAGES_W - 128;
    const int       COLS                = PAGES_W / 6;
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
    bool            alarmFromMsg        = false;        // la alarma llego en un mensaje APRS (para la app)
    uint16_t        alarmSeq            = 0;            // cambia con cada alarma nueva (para la app)
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

    int rowLimit = COLS;
    uint32_t stillSinceMs = 0;                  // desde cuando el equipo esta parado (0 = en movimiento o desconocido)
    bool     vsStopped = false;                 // parado el tiempo suficiente: la velocidad vertical se muestra como 0

    // Espejo de la pantalla: ultimo texto dibujado con row()/header(), para la app Android (BLE screen) (Manuel - EA7JRS)
    const uint8_t MIRROR_ROWS = 8;
    char          mirrorRows[MIRROR_ROWS][28] = {};
    char          mirrorTitle[24] = "";
    char          mirrorRight[12] = "";
    uint8_t       mirrorPage = 0;
    uint32_t      mirrorMs = 0;

    void row(uint8_t r, const char* format, ...) {
        char line[28];
        va_list args;
        va_start(args, format);
        vsnprintf(line, sizeof(line), format, args);
        va_end(args);
        line[rowLimit] = '\0';              // 21 columnas de 6 px caben en 128 px, 26 en 160 (4 menos si hay un icono a la derecha)
        if (r < MIRROR_ROWS) snprintf(mirrorRows[r], sizeof(mirrorRows[r]), "%s", line);
        if (r == 0) pagesInk(INK_ACCENT);   // en color, la primera fila (el dato principal) en ambar
        text(0, ROW_Y0 + r * ROW_PITCH, line);
        if (r == 0) pagesInk(INK_TEXT);
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
        snprintf(mirrorTitle, sizeof(mirrorTitle), "%s", title);
        snprintf(mirrorRight, sizeof(mirrorRight), "%s", right ? right : "");
        pagesZone(PZ_TOP);
        const bool sos = sosState == SOS_ACTIVE && ((millis() / 500) & 1);
        pagesInk(sos ? INK_BAD : INK_HEAD);
        pagesFillRect(0, 0, PW, 9, true);
        pagesText(2, 1, batteryText(), 1, true);
        pagesText((PW - 6 * (int)strlen(title)) / 2, 1, title, 1, true);
        if (sos) right = "SOS";                                 // emergencia en curso: parpadea en todas las paginas
        if (right) pagesText(PW - 2 - 6 * (int)strlen(right), 1, right, 1, true);
        pagesInk(INK_TEXT);
        pagesZone(PZ_BODY);
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
            case PAGE_SAT:      // satelite: cuerpo con dos paneles solares
                pagesFillRect(x + 3, y + 3, 3, 3, on);
                pagesFillRect(x, y + 3, 2, 3, on); pagesFillRect(x + 7, y + 3, 2, 3, on);
                pagesDrawLine(x + 4, y, x + 4, y + 2, on);
                break;
            case PAGE_ROT:      // antena yagi: mastil y dos elementos
                pagesDrawLine(x + 4, y, x + 4, y + 8, on);
                pagesDrawLine(x + 1, y + 2, x + 7, y + 2, on);
                pagesDrawLine(x + 2, y + 5, x + 6, y + 5, on);
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
            case PAGE_TEAM:     // dos personas
                pagesDrawCircle(x + 2, y + 3, 2, on); pagesDrawCircle(x + 6, y + 3, 2, on);
                pagesDrawLine(x, y + 7, x + 8, y + 7, on);
                break;
            case PAGE_AIS:      // barco: casco y mastil
                pagesDrawLine(x, y + 5, x + 8, y + 5, on); pagesDrawLine(x + 1, y + 7, x + 7, y + 7, on); pagesDrawLine(x, y + 5, x + 1, y + 7, on);
                pagesDrawLine(x + 8, y + 5, x + 7, y + 7, on); pagesDrawLine(x + 4, y, x + 4, y + 5, on); pagesDrawLine(x + 4, y, x + 7, y + 4, on);
                break;
            case PAGE_BCAST:    // radio de sobremesa: caja y altavoz
                pagesDrawRect(x, y + 2, 9, 6, on); pagesDrawCircle(x + 6, y + 5, 1, on); pagesDrawLine(x + 2, y, x + 6, y + 2, on);
                break;
            case PAGE_FM:       // emisora de mano: cuerpo y antena
                pagesDrawRect(x + 2, y + 3, 5, 5, on); pagesDrawLine(x + 3, y, x + 3, y + 2, on); pagesFillRect(x + 3, y + 4, 3, 1, on);
                break;
            default: break;
        }
    }

    // Barra inferior de iconos: 12 paginas x 10 px
    void drawBar() {
        pagesZone(PZ_BOTTOM);
        pagesFillRect(0, 54, PW, 10, false);
        pagesInk(INK_MUTED);
        pagesDrawLine(0, 54, PW - 1, 54, true);
        uint8_t visible = 0;
        for (uint8_t i = 0; i < PAGE_COUNT; i++) if (pageVisible(i)) visible++;
        const int w = 11;
        int step = visible > 0 ? (PW - 6) / visible : w;        // con sitio de sobra (160 px) los iconos se reparten a lo ancho
        if (step > 2 * w) step = 2 * w;
        if (step < w) step = w;
        const int x0 = (PW - step * visible) / 2 + (step - w) / 2;
        uint8_t k = 0;
        for (uint8_t i = 0; i < PAGE_COUNT; i++) {
            if (!pageVisible(i)) continue;
            int x = x0 + k * step;
            k++;
            pagesInk(i == page ? INK_SEL : INK_MUTED);
            if (i == page) pagesFillRect(x, 55, w, 9, true);
            icon(i, x + 1, 56, i != page);
        }
        pagesInk(INK_TEXT);
        pagesZone(PZ_BODY);
    }

    void drawSubmenu() {
        uint8_t count;
        const Item* items = itemsFor(page, count);
        const int pitch = 8, w = 104, x = (PW - w) / 2;
        const uint8_t maxVisible = 6;
        uint8_t visible = count < maxVisible ? count : maxVisible;
        uint8_t first = 0;
        if (submenuSel >= maxVisible) first = submenuSel - maxVisible + 1;      // ventana que sigue al cursor
        int h = visible * pitch + 4;
        int y = (64 - h) / 2;
        if (y < 10) y = 10;
        pagesFillRect(x, y, w, h, false);
        pagesInk(INK_FRAME);
        pagesDrawRect(x, y, w, h, true);
        pagesInk(INK_TEXT);
        for (uint8_t i = 0; i < visible; i++) {
            uint8_t index = first + i;
            int rowY = y + 2 + i * pitch;
            if (index == submenuSel) {
                pagesInk(INK_SEL);
                pagesFillRect(x + 2, rowY, w - 4, pitch, true);
                pagesText(x + 5, rowY, labelOf(items[index]), 1, true);
                pagesInk(INK_TEXT);
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

    // Tema de las pantallas a color (Manuel - EA7JRS): Config.display.theme 0 oscuro, 1 claro, 2 automatico.
    // Automatico: claro con el sol por encima de -2 grados (hasta el final del crepusculo civil temprano) y oscuro
    // por debajo de -4 (histeresis para que no parpadee); se mira cada minuto. Sin hora o sin posicion: oscuro.
    bool themeLight() {
        if (!pagesHasColor()) return false;
        if (Config.display.theme == 1) return true;
        if (Config.display.theme != 2) return false;
        static bool     light   = false;
        static bool     have    = false;
        static uint32_t checkMs = 0;
        if (have && millis() - checkMs < 60000UL) return light;
        checkMs = millis();
        const time_t t = now();
        double lat, lon;
        if (year(t) < 2024 || !ownPosition(lat, lon)) {
            have = false;                               // se vuelve a mirar en cuanto haya hora y posicion
            checkMs = millis() - 50000UL;               // (en 10 s)
            return light;
        }
        int y, m, d;
        double hours, alt, az;
        plotter::unixToUtc((long long)t, y, m, d, hours);
        plotter::sunPosition(y, m, d, hours, lat, lon, alt, az);
        light = light ? alt > -4.0 : alt > -2.0;
        have = true;
        return light;
    }

    // Empieza una pantalla con el tema que toque
    void themeBegin() {
        pagesSetLight(themeLight());
        pagesBegin();
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
        text(PW - 2 - 6 * (int)strlen(next), 11, next);

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
        #ifdef HAS_SA868
        tag = SA868::modeM17() ? "M17" : (SA868::mode9600() ? "9k6" : "1k2");     // T-TWR: modem en uso (no hay bandas LoRa)
        #endif
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
        pagesInk(INK_FRAME);
        pagesDrawCircle(cx, cy, r, true);
        int x, y, xs, ys;
        pagesInk(INK_SEL);
        polar(cx, cy, r - 1, (float)bearing - heading, x, y);
        pagesDrawLine(cx, cy, x, y, true);
        polar(cx, cy, r - 5, (float)bearing - heading - 25, xs, ys);
        pagesDrawLine(x, y, xs, ys, true);
        polar(cx, cy, r - 5, (float)bearing - heading + 25, xs, ys);
        pagesDrawLine(x, y, xs, ys, true);
        pagesInk(INK_MUTED);
        text(cx - 2, cy + r + 2, headingUp ? "H" : "N");
        pagesInk(INK_TEXT);
    }

    // Icono APRS de 24x24 (tabla primaria o secundaria); con superposicion, la letra o el digito va en el centro
    bool drawAprsIcon(int x, int y, char symbol, char table) {
        if (symbol < APRS_ICON_FIRST || symbol >= APRS_ICON_FIRST + APRS_ICON_COUNT) return false;
        #ifdef HAS_TFT
            {   // a color, como en la app y en aprs.fi; superposicion (letra o digito) con su propio dibujo (Manuel - EA7JRS)
                const AprsIconColor24& ic = (table == '/' ? APRS_COLOR24_PRIMARY : APRS_COLOR24_SECONDARY)[symbol - APRS_ICON_FIRST];
                pagesIcon4(x, y, 24, ic.c, ic.a, ic.px, false);
                if (table != '/' && table != '\\') {
                    const char* o = strchr(APRS_COLOR_OVERLAYS, table);
                    if (o && table) {
                        const AprsIconColor24& ov = APRS_COLOR24_OVERLAY[o - APRS_COLOR_OVERLAYS];
                        pagesIcon4(x, y, 24, ov.c, ov.a, ov.px, false);
                    } else {
                        char t[2] = { table, '\0' };
                        pagesText(x + 9, y + 8, t, 1, false);
                    }
                }
                return true;
            }
        #endif
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
                if (showIcon) rowLimit = COLS - 4;
                row(3, "Last: %s", packetTypeName(s->type));
            } else {
                if (haveBearing) row(2, "D %s %s %03d", distance, cardinal16(bearing), (int)lround(bearing) % 360);
                else row(2, "D --- (no own GPS)");
                if (showIcon) rowLimit = COLS - 4;                     // filas 3 a 5: dejan libre la esquina inferior derecha (x >= PW - 24) para el icono
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
            rowLimit = COLS;
            if (showIcon) drawAprsIcon(104 + XR, 40, s->symbol, s->table);
            else if (haveBearing) miniCompass(114 + XR, 39, 9, (float)bearing);
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
                char first[28], second[28];
                snprintf(first, sizeof(first), "%.*s", COLS, s->comment);
                snprintf(second, sizeof(second), "%.*s", COLS, strlen(s->comment) > (size_t)COLS ? s->comment + COLS : "");
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
            int y = ROW_Y0 + i * ROW_PITCH;
            bool selected = listBrowse && top + i == listCursor;
            if (selected) { pagesInk(INK_SEL); pagesFillRect(0, y - 1, PW, 9, true); }
            bool iconDrawn = false;
            #ifdef HAS_TFT
                // pantalla a color y ancha (Heltec): el icono APRS pequeno en lugar de la letra del tipo (EA7JRS)
                if (XR >= 16 && s->symbol >= APRS_ICON_FIRST && s->symbol < APRS_ICON_FIRST + APRS_ICON_COUNT) {
                    const AprsIconColor8& ic = (s->table == '/' ? APRS_COLOR8_PRIMARY : APRS_COLOR8_SECONDARY)[s->symbol - APRS_ICON_FIRST];
                    pagesIcon4(0, y - 1, 8, ic.c, ic.a, ic.px, selected);
                    snprintf(line, sizeof(line), "%-9.9s%7s%5s", s->call, distance, age);
                    pagesText(11, y, line, 1, selected);
                    iconDrawn = true;
                }
            #endif
            if (!iconDrawn) {
                snprintf(line, sizeof(line), "%c %-9.9s%6s%4s", kindLetter(s->kind), s->call, distance, age);
                pagesText(0, y, line, 1, selected);
            }
            pagesInk(INK_TEXT);
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
            if (selected) { pagesInk(INK_SEL); pagesFillRect(0, y - 1, PW, 9, true); }
            messageIcon(1, y, MessageLog::stateOf(*e), !selected);
            pagesText(12, y, e->call, 1, selected);
            char when[8];
            formatMessageTime(e->epoch, nowEpoch, when, sizeof(when));
            pagesText(PW - 2 - 6 * 5, y, when, 1, selected);
            const int fit = (PW - 2 - 6 * 5 - 6 - 72) / 6;         // con sitio (160 px), el principio del texto entre el indicativo y la hora
            if (fit >= 5) {
                char head[16];
                snprintf(head, sizeof(head), "%.*s", fit < 15 ? fit : 15, e->text);
                pagesText(72, y, head, 1, selected);
            }
            pagesInk(INK_TEXT);
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
        text(PW - 2 - 6 * (int)strlen(label), 11, label);
        formatMessageDateTime(e->epoch, line, sizeof(line));
        text(2, 20, line);
        size_t position = 0;
        char piece[32];
        int lines = 0;
        for (int r = 0; r < 4 && wrapNext(e->text, position, COLS, piece, sizeof(piece)); r++) { text(0, 30 + r * 8, piece); lines++; }
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

    uint32_t msgChangeSeq = 0;           // sube con cada cambio del registro de mensajes (evento BLE msg)

    void touchLog() {
        logDirty = true;
        logDirtyMs = millis();
        msgChangeSeq++;
    }

    void showToast(const char* message) {
        snprintf(toastText, sizeof(toastText), "%s", message);
        toastUntil = millis() + 2500;
    }

    void sosCancel() {
        bool was = sosState != SOS_IDLE;
        sosState = SOS_IDLE;
        sosTimed = false;
        sosBy[0] = '\0';
        if (was) showToast("SOS cancelled");
    }

    void sosStart() {
        if (!aprsMode()) { showToast("SOS: APRS modes only"); return; }     // MORSE, WSPR y LR-FHSS no emiten paquetes APRS
        sosState = SOS_COUNTDOWN;
        sosTimed = false;
        sosStartMs = millis();
        sosLastTick = 0;
        sosSent = 0;
        displayToggle(true);
        displayTime = millis();
    }

    void drawToast() {
        int w = 6 * (int)strlen(toastText) + 10;
        int x = (PW - w) / 2;
        pagesFillRect(x, 24, w, 16, false);
        pagesInk(INK_FRAME);
        pagesDrawRect(x, 24, w, 16, true);
        pagesInk(INK_TEXT);
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


    // ---------- GPS > GNSS (Manuel - EA7JRS): satelites por constelacion, bandas (L1 / L5), PPS, DOP y cielo ----------
    // Tinta de cada constelacion (como DISP > GPS de la app): GPS verde, Galileo azul, BeiDou rojo, GLONASS gris, el resto claro
    uint8_t sysInk(char sys) {
        switch (sys) {
            case 'G': return INK_GOOD;
            case 'E': return INK_FRAME;
            case 'C': return INK_BAD;
            case 'R': return INK_MUTED;
            default:  return INK_TEXT;
        }
    }

    void drawGnss() {
        const bool fix = gpsIsActive && gps.location.isValid();
        const char* right = !gpsIsActive ? "OFF" : (GPS_Utils::ppsLocked() ? "PPS" : (!fix ? "NO FIX" : (gps.altitude.isValid() ? "3D" : "2D")));
        header("GNSS", right);
        static const char SYS[] = "GECRJIS";               // orden de la lista
        uint8_t seen[7] = {}, used[7] = {}, bands[GNSS_Sky::BAND_COUNT] = {};
        uint8_t total = 0, totalUsed = 0;
        const uint8_t n = GNSS_Sky::count();
        for (uint8_t i = 0; i < n; i++) {
            const GNSS_Sky::Sat* p = GNSS_Sky::sat(i);
            if (!p) continue;
            const char* k = p->sys ? strchr(SYS, p->sys) : nullptr;
            const uint8_t idx = k ? (uint8_t)(k - SYS) : 6;
            seen[idx]++;
            total++;
            if (p->used) { used[idx]++; totalUsed++; }
            for (uint8_t b = 0; b < GNSS_Sky::BAND_COUNT; b++) if (GNSS_Sky::hasBand(*p, b)) bands[b]++;
        }

        // cielo a la derecha: anillos de 0 y 45 grados de elevacion, norte arriba; relleno = usado en el fix
        const int r = 22, cx = PW - 2 - r, cy = 37;
        pagesInk(INK_MUTED);
        pagesDrawCircle(cx, cy, r, true);
        pagesDrawCircle(cx, cy, r / 2, true);
        text(cx - 2, cy - r + 2, "N");
        for (uint8_t i = 0; i < n; i++) {
            const GNSS_Sky::Sat* p = GNSS_Sky::sat(i);
            if (!p || p->elev < 0 || p->azim < 0) continue;
            int x, y;
            polar(cx, cy, r * (90 - p->elev) / 90.0f, (float)p->azim, x, y);
            pagesInk(sysInk(p->sys));
            if (p->used) pagesFillCircle(x, y, 2, true);
            else if (p->snr > 0) pagesDrawCircle(x, y, 1, true);
            else pagesFillRect(x, y, 1, 1, true);
        }
        pagesInk(INK_TEXT);

        rowLimit = COLS - 9;                                // las filas dejan libre el cielo
        if (n == 0) {
            row(0, "No GSV data");
            row(1, gpsIsActive ? "waiting..." : "GPS off");
            rowLimit = COLS;
            return;
        }
        row(0, "Use %u/%u", (unsigned)totalUsed, (unsigned)total);
        // constelaciones ("G8/11") y bandas ("L1 30") repartidas en las filas 1 a 4
        char lines[4][28] = {};
        uint8_t line = 0;
        auto put = [&](const char* token) {
            while (line < 4) {
                const size_t len = strlen(lines[line]);
                const size_t need = strlen(token) + (len ? 1 : 0);
                if (len + need <= (size_t)rowLimit) {
                    if (len) strcat(lines[line], " ");
                    strcat(lines[line], token);
                    return;
                }
                if (len == 0) return;                       // no cabe ni sola
                line++;
            }
        };
        char token[12];
        for (uint8_t i = 0; i < 7; i++) {
            if (!seen[i]) continue;
            snprintf(token, sizeof(token), "%c%u/%u", SYS[i], (unsigned)used[i], (unsigned)seen[i]);
            put(token);
        }
        if (line < 3 && lines[line][0]) line++;             // las bandas empiezan en una fila nueva si hay sitio
        static const char* const BAND_NAMES[GNSS_Sky::BAND_COUNT] = { "L1", "L5", "L2" };
        for (uint8_t b = 0; b < GNSS_Sky::BAND_COUNT; b++) {
            if (!bands[b]) continue;
            snprintf(token, sizeof(token), "%s:%u", BAND_NAMES[b], (unsigned)bands[b]);
            put(token);
        }
        for (uint8_t i = 0; i < 4; i++) if (lines[i][0]) row(1 + i, "%s", lines[i]);
        const float pd = GNSS_Sky::pdop(), hd = GNSS_Sky::hdop(), err = GNSS_Sky::errorM();
        char dop[28] = "";
        int k = 0;
        if (!isnan(pd)) k += snprintf(dop + k, sizeof(dop) - k, "P%.1f ", pd);
        else if (!isnan(hd)) k += snprintf(dop + k, sizeof(dop) - k, "H%.1f ", hd);
        else if (gps.hdop.isValid()) k += snprintf(dop + k, sizeof(dop) - k, "H%.1f ", gps.hdop.hdop());
        if (!isnan(err)) snprintf(dop + k, sizeof(dop) - k, "%.1fm", err);
        if (dop[0]) row(5, "%s", dop);
        rowLimit = COLS;
    }

    void drawGps() {
        if (gpsView == 1) { drawGnss(); return; }
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
            if ((int)strlen(line) > COLS) snprintf(line, sizeof(line), "Sat %s H%.1f %s", sats, hdop, hdopQuality(hdop));
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
        const int cx = 100 + XR, cy = 37, radius = 22;
        bool valid = fusion.valid();
        float decl = currentDeclination();
        float trueHeading = valid ? fusion.heading() : 0.0f;
        float heading = northTrue ? trueHeading : wrap360(trueHeading - decl);     // referido al norte elegido
        float shift = northTrue ? 0.0f : decl;                                     // para pasar rumbos verdaderos al norte elegido

        // rosa: gira para que el rumbo actual quede arriba
        pagesInk(INK_FRAME);
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
            pagesInk(i == 0 ? INK_BAD : INK_TEXT);          // en color, la N en rojo
            text(x - 2, y - 3, letters[i]);
        }
        // marca fija del rumbo (arriba)
        pagesInk(INK_ACCENT);
        pagesFillTriangle(cx, cy - radius + 1, cx - 3, cy - radius - 4, cx + 3, cy - radius - 4, true);
        pagesInk(INK_TEXT);

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
            pagesInk(INK_SEL);
            polar(cx, cy, radius - 4, (float)bearing - heading, x, y);
            pagesDrawLine(cx, cy, x, y, true);
            polar(cx, cy, radius - 9, (float)bearing - heading - 18, xs, ys);
            pagesDrawLine(x, y, xs, ys, true);
            polar(cx, cy, radius - 9, (float)bearing - heading + 18, xs, ys);
            pagesDrawLine(x, y, xs, ys, true);
            pagesInk(INK_TEXT);
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
            pagesInk(INK_ACCENT);
            text(2, 20, value, 3);
            pagesInk(INK_TEXT);
            if (valid) text(2, 46, cardinal16(heading), 2);
            else text(2, 52, "no heading");
            return;
        }
        pagesInk(INK_ACCENT);
        text(2, 20, value, 2);
        pagesInk(INK_TEXT);
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
            if (fabsf(delta) < 5.0f) { snprintf(line, sizeof(line), "B%03d ON COURSE", (int)lround(bearing) % 360); pagesInk(INK_GOOD); }
            else snprintf(line, sizeof(line), "B%03d %+d%c", (int)lround(bearing) % 360, (int)lroundf(delta), delta > 0 ? '>' : '<');
        } else {
            snprintf(line, sizeof(line), "B%03d", (int)lround(bearing) % 360);
        }
        text(2, 38, line);
        pagesInk(INK_TEXT);
        if (km < 0.05) {
            pagesInk(INK_GOOD);
            text(2, 47, "ARRIVED");
            pagesInk(INK_TEXT);
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
                else if (BARO_Utils::isNetQnh()) snprintf(line, sizeof(line), "QNH %.0fN", qnhValue);   // N = de internet (la app)
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
        if ((int)strlen(line) > COLS) snprintf(line, sizeof(line), "+%.0f -%.0f H%.0f L%.0f", altitudeValue(altProfile.ascentM(), unitSystem), altitudeValue(altProfile.descentM(), unitSystem),
                                        altitudeValue(hi, unitSystem), altitudeValue(lo, unitSystem));
        text(0, 29, line);

        const int gx = 0, gy = 38, gw = PW, gh = 17;
        pagesInk(INK_FRAME);
        pagesDrawRect(gx, gy, gw, gh, true);
        pagesInk(INK_GOOD);
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
        pagesInk(INK_TEXT);

        // eje temporal bajo la grafica
        char span[10], axis[14];
        formatAge((uint32_t)(points - 1) * altProfile.stepMs(), span, sizeof(span));
        snprintf(axis, sizeof(axis), "-%s", span);
        text(1, 56, axis);
        text(PW - 1 - 3 * 6, 56, "now");
    }

    // Tiempo de internet (la app lo manda con "wxnet": OpenWeatherMap / IQAir); arriba a la derecha "NET" para no confundirlo con un sensor
    void drawNetWx(const NET_WX::Data& d) {
        header(PAGE_TITLES[PAGE_WX], "NET");
        char age[8];
        const uint32_t a = NET_WX::ageS();
        if (a < 3600) snprintf(age, sizeof(age), "%lum", (unsigned long)(a / 60)); else snprintf(age, sizeof(age), "%luh", (unsigned long)(a / 3600));
        row(0, "%.9s %.10s", d.src, d.place);
        char t[10] = "---", h[6] = "", p[10] = "";
        if (d.hasT) snprintf(t, sizeof(t), "%.1f" DEG_C, d.tempC);
        if (d.hasH) snprintf(h, sizeof(h), "%.0f%%", d.humidity);
        if (d.hasP) snprintf(p, sizeof(p), "%.0fhPa", d.pressureHpa);
        row(1, "%s %s %s", t, h, p);
        if (d.hasWind) {
            char g[10] = "";
            if (d.hasGust) snprintf(g, sizeof(g), " G%.0f", d.gustMs * 3.6f);
            const int bf = plotter::beaufort(d.windMs / 0.514444f);            // fuerza Beaufort (formulas del plotter)
            if (d.windDeg >= 0) row(2, "F%d %.0fkm/h %s%s", bf, d.windMs * 3.6f, cardinal16(d.windDeg), g);
            else                row(2, "F%d Wind %.0fkm/h%s", bf, d.windMs * 3.6f, g);
        }
        char fc[20]; float d3 = NAN;
        const bool haveFc = NET_WX::forecast(fc, sizeof(fc), &d3);   // pronostico con la presion de internet y su tendencia de 3 h
        char sky[24] = "";
        if (d.hasClouds || d.hasRain) {
            char c[12] = "", r[12] = "";
            if (d.hasClouds) snprintf(c, sizeof(c), "Cloud %d%%", d.clouds);
            if (d.hasRain) snprintf(r, sizeof(r), " %.1fmm/h", d.rainMmH);
            snprintf(sky, sizeof(sky), "%s%s", c, r);
        } else if (d.desc[0]) snprintf(sky, sizeof(sky), "%.20s", d.desc);
        if (haveFc) row(3, "Fc %.16s%s", fc, d3 == d3 ? (d3 < -0.5f ? " v" : d3 > 0.5f ? " ^" : " =") : "");
        else if (sky[0]) row(3, "%s", sky);
        if (d.aqiUs >= 0)      row(4, "AQI %d %s%s%s", d.aqiUs, NET_WX::usAqiName(d.aqiUs), d.pollutant[0] ? " " : "", d.pollutant);
        else if (d.aqiOwm > 0) row(4, "AQI %d/5 %s", d.aqiOwm, NET_WX::owmAqiName(d.aqiOwm));
        else if (haveFc && sky[0]) row(4, "%s", sky);
        if (d.hasPm25)         row(5, "PM2.5 %.0f%s %s ago", d.pm25, d.hasPm10 ? (String(" PM10 ") + String(d.pm10, 0)).c_str() : "", age);
        else if (haveFc && sky[0] && (d.aqiUs >= 0 || d.aqiOwm > 0)) row(5, "%.12s %s ago", sky, age);
        else                   row(5, "%s ago (internet)", age);
    }

    void drawWx() {
        WX_Utils::refreshForScreen();
        WX_Utils::Reading r;
        NET_WX::Data nd;
        const bool haveNet = NET_WX::get(nd);
        const bool haveSensor = WX_Utils::getReading(r);
        if (haveNet && (!haveSensor || (millis() / 6000) % 2 == 1)) { drawNetWx(nd); return; }      // con sensor y datos de internet: se alternan cada 6 s
        header(PAGE_TITLES[PAGE_WX]);
        if (!haveSensor) {
            row(1, "Sensor: none/no data");
            row(2, "Enable Telemetry in");
            row(3, "WebConfig & connect");
            row(4, "BME280/BMP280/BME680");
            row(5, "or app: internet WX");
            return;
        }
        // filas cada 9 px: T/Td/Hr, presion y tendencia, gas, pronostico y motivo
        char line[32];
        if (r.hasHumidity) snprintf(line, sizeof(line), "%.1f" DEG_C " %.1f" DEG_C " %.0f%%", r.tempC, dewPointC(r.tempC, r.humidity), r.humidity);
        else snprintf(line, sizeof(line), "%.1f" DEG_C " --- ---", r.tempC);
        line[24] = '\0';
        int visible = 0;
        for (const char* c = line; *c; c++) if (!((*c & 0xC0) == 0x80)) visible++;      // columnas reales (UTF-8 en TFT)
        pagesInk(INK_ACCENT);
        text((PW - 6 * visible) / 2, 11, line);                                         // primera linea centrada
        pagesInk(INK_TEXT);

        // filas cada 9 px a partir de y=20; la de gas solo aparece con BME680
        int y = 20;
        if (r.hasPressure) {
            snprintf(line, sizeof(line), "%.1fhPa %s", r.pressureHpa, PressureTrend::trendShort(pressureTrend.trend()));
        } else if (haveNet && nd.hasP) {                        // sin barometro: la presion de internet (marcada "N")
            float nd3 = 0;
            const bool t3 = NET_WX::delta3h(nd3);
            snprintf(line, sizeof(line), "N %.1fhPa %s", nd.pressureHpa, PressureTrend::trendShort(t3 ? PressureTrend::classifyDelta(nd3) : TREND_UNKNOWN));
        } else {
            snprintf(line, sizeof(line), "--- (no barometer)");
        }
        line[COLS] = '\0';
        text(0, y, line);
        y += 9;

        if (r.hasGas) {
            if (iaq.ready()) snprintf(line, sizeof(line), "IAQ %.0f %s", iaq.index(), iaq.label());
            else snprintf(line, sizeof(line), "IAQ wait %um", (unsigned)((iaq.warmupRemainingSec(millis()) + 59) / 60));
            line[COLS] = '\0';
            text(0, y, line);
            y += 9;
        }

        // pronostico local: presion (a nivel del mar si se conoce), tendencia de 3 h, humedad y punto de rocio. Lo que el tracker no
        // mide lo pone el tiempo de internet: sin barometro, su presion y su tendencia (observaciones con hora); sin higrometro, su
        // temperatura y su humedad (T y Td del mismo sitio, para que T-Td tenga sentido)
        const bool netP = !r.hasPressure && haveNet && nd.hasP;
        const bool netH = !r.hasHumidity && haveNet && nd.hasH && nd.hasT;
        if (!r.hasPressure && !netP) {
            text(0, y, "Fcst: needs barometer");
            return;
        }
        float pressure = netP ? nd.pressureHpa : r.pressureHpa;
        #ifdef GPS_BARO_ALTITUDE
            if (!netP) {
                float reduced = BARO_Utils::reducedPressure(r.pressureHpa, r.tempC);
                if (reduced > 800.0f && reduced < 1100.0f) pressure = reduced;
            }
        #endif
        float delta = 0;
        bool hasDelta = netP ? NET_WX::delta3h(delta) : pressureTrend.delta3h(delta);
        const Trend tr = netP ? (hasDelta ? PressureTrend::classifyDelta(delta) : TREND_UNKNOWN) : pressureTrend.trend();
        const float fT = netH ? nd.tempC : r.tempC;
        const float fH = netH ? nd.humidity : (r.hasHumidity ? r.humidity : -1.0f);
        float dew = fH >= 0 ? dewPointC(fT, fH) : 0.0f;
        char forecast[20];
        forecastText(pressure, hasDelta, tr, fT, fH, dew, forecast, sizeof(forecast), hasDelta ? delta : NAN);
        snprintf(line, sizeof(line), (netP || netH) ? "Fc+N: %s" : "Fcst: %s", forecast);
        line[COLS] = '\0';
        text(0, y, line);
        y += 9;
        char because[32];
        if (hasDelta) snprintf(because, sizeof(because), "%+.1f hPa/3h", delta);       // solo la tendencia de 3 h
        else snprintf(because, sizeof(because), "3h trend: wait");
        because[COLS] = '\0';
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
            line[COLS] = '\0';
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
        pagesZone(PZ_TOP);
        pagesInk(INK_BAD);
        pagesFillRect(0, 0, PW, 9, true);
        pagesText((PW - 6 * 10) / 2, 1, "SOS BEACON", 1, true);
        pagesZone(PZ_BODY);
        char big[12];
        snprintf(big, sizeof(big), "%d", left);
        text(52 + XR / 2, 14, big, 4);
        pagesInk(INK_TEXT);
        text(0, 48, "Mic-E Emergency + SOS");
        pagesZone(PZ_BOTTOM);
        pagesInk(INK_BAD);
        pagesFillRect(0, 56, PW, 8, true);
        pagesText(2, 56, "Any press = cancel", 1, true);
        pagesInk(INK_TEXT);
        pagesZone(PZ_BODY);
    }

    // Pantalla de alarma: sustituye a la pagina mientras esta visible
    void drawAlarm() {
        bool blink = (millis() / 500) & 1;
        const char* title = alarmTitle((AlarmKind)alarmKind);
        pagesZone(PZ_TOP);
        pagesInk(INK_BAD);
        if (blink) {
            pagesFillRect(0, 0, PW, 9, true);
            pagesText((PW - 6 * (int)strlen(title)) / 2, 1, title, 1, true);
        } else {
            pagesText((PW - 6 * (int)strlen(title)) / 2, 1, title, 1, false);
            pagesDrawRect(0, 0, PW, 9, true);
        }
        pagesZone(PZ_BODY);
        pagesInk(INK_ACCENT);
        text(2, 12, alarmCall, 2);
        pagesInk(INK_TEXT);
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
        line[COLS] = '\0';
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
        line[COLS] = '\0';
        text(0, 48, line);
        pagesZone(PZ_BOTTOM);
        pagesInk(INK_BAD);
        pagesFillRect(0, 56, PW, 8, true);
        if (alarmSounding) pagesText(2, 56, st && st->hasPosition ? "1P mute  LP GO TO" : "1P mute", 1, true);
        else pagesText(2, 56, st && st->hasPosition ? "1P close LP GO TO" : "1P close", 1, true);
        pagesInk(INK_TEXT);
        pagesZone(PZ_BODY);
    }

    // ---- Sol y luna (Manuel - EA7JRS): vistas CLOCK > SUN y CLOCK > MOON y los iconos pequenos del reloj ----
    // Hora local "HH:MM" de un instante en minutos UTC del dia (pueden salir < 0 o > 1440)
    static void fmtLocalMin(double utcMin, int offsetMin, char* out, size_t size) {
        const int v = (((int)lround(utcMin) + offsetMin) % 1440 + 1440) % 1440;
        snprintf(out, size, "%02d:%02d", v / 60, v % 60);
    }

    // Elongacion de la luna (grados, 0 nueva .. 180 llena) vista desde la posicion propia
    static double moonElongation(long long unixS, double lat, double lon) {
        int y, m, d;
        double h, sAlt, sAz, mAlt, mAz, dist;
        plotter::unixToUtc(unixS, y, m, d, h);
        plotter::sunPosition(y, m, d, h, lat, lon, sAlt, sAz);
        plotter::moonPosition(y, m, d, h, lat, lon, mAlt, mAz, dist);
        const double a1 = sAlt * DEG_TO_RAD, a2 = mAlt * DEG_TO_RAD;
        double c = sin(a1) * sin(a2) + cos(a1) * cos(a2) * cos((sAz - mAz) * DEG_TO_RAD);
        if (c > 1) c = 1;
        if (c < -1) c = -1;
        return acos(c) * RAD_TO_DEG;
    }

    // Llena (maxima elongacion) o nueva (minima) cerca de "center": malla de 2 h en +-18 h y parabola en el mejor punto
    static long long moonExtreme(long long center, double lat, double lon, bool full) {
        const int N = 19;
        double e[N];
        int best = 0;
        for (int i = 0; i < N; i++) {
            e[i] = moonElongation(center + (long long)(i - N / 2) * 7200, lat, lon);
            if (full ? e[i] > e[best] : e[i] < e[best]) best = i;
        }
        double shift = 0;
        if (best > 0 && best < N - 1) {
            const double den = e[best - 1] - 2 * e[best] + e[best + 1];
            if (fabs(den) > 1e-9) shift = 0.5 * (e[best - 1] - e[best + 1]) / den;
            if (shift > 1) shift = 1;
            if (shift < -1) shift = -1;
        }
        return center + (long long)lround((best - N / 2 + shift) * 7200.0);
    }

    struct MoonInfo {
        bool   ok = false;
        double elong = 0, lit = 0, ageDays = 0;     // edad equivalente (dias desde la nueva) para la fase en 8 partes
        bool   waxing = true;
        double toFullH = 0, toNewH = 0;
        double riseMin = 0, setMin = 0;             // minutos UTC del dia
        bool   hasRise = false, hasSet = false;
    };

    // Datos de la luna, recalculados cada 5 minutos (la busqueda de llena y nueva cuesta unos 40 calculos de posicion)
    static const MoonInfo& moonInfo(time_t t, double lat, double lon) {
        static MoonInfo info;
        static uint32_t calcMs = 0;
        if (info.ok && millis() - calcMs < 300000UL) return info;
        calcMs = millis();
        const long long u = (long long)t;
        info.elong = moonElongation(u, lat, lon);
        info.waxing = moonElongation(u + 3 * 3600, lat, lon) > moonElongation(u - 3 * 3600, lat, lon);
        info.lit = (1 - cos(info.elong * DEG_TO_RAD)) / 2;
        const double syn = 29.530588853;
        info.ageDays = (info.waxing ? info.elong : 360 - info.elong) / 360 * syn;
        int y, m, d;
        double h;
        plotter::unixToUtc(u, y, m, d, h);
        const double age = plotter::moonAgeDays(y, m, d, h);                      // edad media: solo para saber donde buscar
        for (int k = 0; k < 2; k++) {
            const bool full = k == 0;
            double ahead = fmod((full ? syn / 2 : syn) - age + 2 * syn, syn);
            long long when = moonExtreme(u + (long long)(ahead * 86400), lat, lon, full);
            if (when <= u) when = moonExtreme(u + (long long)((ahead + syn) * 86400), lat, lon, full);
            (full ? info.toFullH : info.toNewH) = (double)(when - u) / 3600.0;
        }
        plotter::moonTimes(y, m, d, lat, lon, info.riseMin, info.hasRise, info.setMin, info.hasSet);
        info.ok = true;
        return info;
    }

    // Disco de la luna con su fase; en el hemisferio sur, al reves (la parte iluminada a la izquierda al crecer)
    static void drawMoonDisc(int cx, int cy, int r, const MoonInfo& mi, bool south) {
        const double th = (mi.waxing ? mi.elong : 360 - mi.elong) * DEG_TO_RAD;
        const double c = cos(th);
        const bool color = pagesHasColor();
        for (int dy = -r; dy <= r; dy++) {
            const double w = sqrt((double)(r * r - dy * dy));
            const int W = (int)floor(w);
            int a, b;                                           // tramo iluminado de la fila, en dx
            if (th <= PI) { a = (int)ceil(w * c); b = W; }
            else          { a = -W; b = (int)floor(-w * c); }
            if (a < -W) a = -W;
            if (b > W) b = W;
            if (south) { const int t2 = a; a = -b; b = -t2; }
            if (color) {
                pagesInk(INK_SHADE);
                pagesFillRect(cx - W, cy + dy, 2 * W + 1, 1, true);
                pagesInk(INK_MOON);
            }
            if (b >= a) pagesFillRect(cx + a, cy + dy, b - a + 1, 1, true);
        }
        pagesInk(INK_MUTED);
        pagesDrawCircle(cx, cy, r, true);
        pagesInk(INK_TEXT);
    }

    static void drawSunIcon(int cx, int cy, int r) {
        pagesInk(INK_SUN);
        pagesFillCircle(cx, cy, r, true);
        for (int k = 0; k < 8; k++) {
            const float a = k * (float)PI / 4;
            pagesFillRect(cx + (int)lroundf((r + 2) * cosf(a)), cy + (int)lroundf((r + 2) * sinf(a)), 1, 1, true);
        }
        pagesInk(INK_TEXT);
    }

    // Hora, posicion y desfase de la zona para las vistas de sol y luna; false si falta la hora o la posicion
    static bool skyNow(time_t& t, double& lat, double& lon, int& offsetMin) {
        t = now();
        if (year(t) < 2024 || !ownPosition(lat, lon)) return false;
        bool dst = false;
        offsetMin = zoneOffsetMinutes(zoneMode, zoneHalfHours, (int64_t)t, &dst);
        return true;
    }

    // CLOCK > SUN: duracion del dia o la noche, orto y ocaso con su azimut, mediodia con la altura, crepusculo civil y el
    // recorrido del sol sobre el horizonte (de dia el sol sobre su arco; de noche apagado bajo el horizonte)
    void drawSun() {
        time_t t;
        double lat, lon;
        int off;
        if (!skyNow(t, lat, lon, off)) { header("SUN"); text(4, 24, "Needs time and GPS"); return; }
        int y, m, d;
        double h, alt, az;
        plotter::unixToUtc((long long)t, y, m, d, h);
        plotter::sunPosition(y, m, d, h, lat, lon, alt, az);
        char right[10];
        snprintf(right, sizeof(right), "El %d", (int)lround(alt + plotter::refractionDeg(alt)));
        header("SUN", right);

        double rise, set, dawn, dusk;
        const bool hasRise = plotter::sunTimes(y, m, d, lat, lon, -0.833, rise, set);
        const bool hasDawn = plotter::sunTimes(y, m, d, lat, lon, -6.0, dawn, dusk);
        const double noon = plotter::solarNoonUtcMin(y, m, d, lon);
        double nAlt, nAz;
        plotter::sunPosition(y, m, d, noon / 60.0, lat, lon, nAlt, nAz);
        const bool up = alt > -0.833;
        const double nowMin = h * 60.0;
        char a[6], b[6];

        const bool big = XR >= 16;                                   // 160 px: arco mayor
        const int R = big ? 21 : 15;
        const int cx = PW - (big ? 8 : 2) - R, hy = 46;
        rowLimit = min(COLS, (cx - R - 2) / 6);
        int dayLen = 0;
        if (hasRise) {
            dayLen = (int)lround(set - rise);
            if (dayLen < 0) dayLen += 1440;
            if (up) row(0, "Day %dh%02dm", dayLen / 60, dayLen % 60);
            else    row(0, "Night %dh%02dm", (1440 - dayLen) / 60, (1440 - dayLen) % 60);
            double ra, rz;
            fmtLocalMin(rise, off, a, sizeof(a));
            plotter::sunPosition(y, m, d, rise / 60.0, lat, lon, ra, rz);
            row(1, "Rise %s %3d", a, ((int)lround(rz) + 360) % 360);
            fmtLocalMin(set, off, b, sizeof(b));
            plotter::sunPosition(y, m, d, set / 60.0, lat, lon, ra, rz);
            row(2, "Set  %s %3d", b, ((int)lround(rz) + 360) % 360);
        } else {
            row(0, nAlt > 0 ? "Polar day" : "Polar night");
            row(1, "Rise --:--");
            row(2, "Set  --:--");
        }
        fmtLocalMin(noon, off, a, sizeof(a));
        row(3, "Noon %s El%d", a, (int)lround(nAlt + plotter::refractionDeg(nAlt)));
        if (hasDawn) {
            fmtLocalMin(dawn, off, a, sizeof(a));
            fmtLocalMin(dusk, off, b, sizeof(b));
            row(4, "Dawn %s", a);
            row(5, "Dusk %s", b);
        } else {
            row(4, "Dawn --:--");
            row(5, "Dusk --:--");
        }
        rowLimit = COLS;

        // recorrido: horizonte, arco del dia punteado (en el color del cielo) y arco de la noche bajo el horizonte
        pagesInk(INK_MUTED);
        pagesDrawLine(cx - R - 2, hy, cx + R + 2, hy, true);
        text(cx - R - 2, hy + 3, "E");
        text(min(cx + R - 3, PW - 6), hy + 3, "W");
        pagesInk(INK_HEAD);
        for (int deg = 0; deg <= 180; deg += 6) {
            const float r = deg * DEG_TO_RAD;
            pagesFillRect(cx - (int)lroundf(R * cosf(r)), hy - (int)lroundf(R * 0.8f * sinf(r)), 1, 1, true);
        }
        pagesInk(INK_NIGHT);
        for (int deg = 0; deg <= 180; deg += 12) {
            const float r = deg * DEG_TO_RAD;
            pagesFillRect(cx + (int)lroundf(R * cosf(r)), hy + (int)lroundf(R * 0.25f * sinf(r)), 1, 1, true);
        }
        float frac = 0.5f;
        if (hasRise && dayLen > 0 && dayLen < 1440) {
            if (up) frac = (float)((fmod(nowMin - rise + 2880, 1440)) / dayLen);
            else    frac = (float)((fmod(nowMin - set + 2880, 1440)) / (1440 - dayLen));
        }
        frac = constrain(frac, 0.0f, 1.0f);
        const float ang = frac * (float)PI;
        if (up) {
            drawSunIcon(cx - (int)lroundf(R * cosf(ang)), hy - (int)lroundf(R * 0.8f * sinf(ang)), 4);
        } else {
            pagesInk(INK_NIGHT);
            pagesDrawCircle(cx + (int)lroundf(R * cosf(ang)), hy + (int)lroundf(R * 0.25f * sinf(ang)), 3, true);
            pagesInk(INK_TEXT);
        }
    }

    // CLOCK > MOON: fase, parte iluminada, edad, salida y puesta, y cuanto falta para la llena y la nueva
    void drawMoon() {
        time_t t;
        double lat, lon;
        int off;
        if (!skyNow(t, lat, lon, off)) { header("MOON"); text(4, 24, "Needs time and GPS"); return; }
        const MoonInfo& mi = moonInfo(t, lat, lon);
        char right[8];
        snprintf(right, sizeof(right), "%d%%", (int)lround(mi.lit * 100));
        header("MOON", right);
        const bool big = XR >= 16;
        const int r = big ? 24 : 17;
        const int cx = PW - 2 - r, cy = 37;
        rowLimit = min(COLS, (cx - r - 2) / 6);
        row(0, "%s", plotter::moonPhaseNameEn(plotter::moonPhase8(mi.ageDays)));
        if (big) row(1, "Lit %d%% Age %.1fd", (int)lround(mi.lit * 100), mi.ageDays);
        else     row(1, "Lit %d%% %.1fd", (int)lround(mi.lit * 100), mi.ageDays);
        char a[6] = "--:--";
        if (mi.hasRise) fmtLocalMin(mi.riseMin, off, a, sizeof(a));
        row(2, "Rise %s", a);
        snprintf(a, sizeof(a), "--:--");
        if (mi.hasSet) fmtLocalMin(mi.setMin, off, a, sizeof(a));
        row(3, "Set  %s", a);
        const int fh = (int)lround(mi.toFullH), nh = (int)lround(mi.toNewH);
        row(4, "Full %dd%02dh", fh / 24, fh % 24);
        row(5, "New  %dd%02dh", nh / 24, nh % 24);
        rowLimit = COLS;
        drawMoonDisc(cx, cy, r, mi, lat < 0);
    }

    // CLOCK: 1P pasa a SUN y a MOON (con hora y posicion) y de ahi a la pagina siguiente; 2P vuelve
    static bool moveClockView(int direction) {
        if (page != PAGE_CLOCK) return false;
        time_t t;
        double lat, lon;
        int off;
        if (!skyNow(t, lat, lon, off)) { clockView = 0; return false; }
        const int to = clockView + direction;
        if (to < 0 || to > 2) return false;
        clockView = (uint8_t)to;
        return true;
    }

    void drawClock() {
        if (clockView == 1) { drawSun(); return; }
        if (clockView == 2) { drawMoon(); return; }
        time_t t = now();
        bool haveTime = year(t) >= 2024;
        uint32_t nowMs = millis();
        bool gpsTime = gpsIsActive && gps.time.isValid() && gps.time.age() < 10000;
        char source[12] = "NONE";
        if (haveTime) {
            if (gpsTime) snprintf(source, sizeof(source), "GPS");
            else if (TIME_Utils::source() == TIME_Utils::SRC_NTP || TIME_Utils::source() == TIME_Utils::SRC_DAPNET)
                snprintf(source, sizeof(source), "%s", TIME_Utils::sourceName());     // NTP (ntp.roa.es) o DAPNET
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
        pagesInk(INK_ACCENT);
        text(3 + XR / 2, 13, buffer, 3);
        pagesInk(INK_TEXT);
        if (!clock24h) text(100 + XR / 2, 11, hours >= 12 ? "PM" : "AM");
        snprintf(buffer, sizeof(buffer), "%02d", second(local));
        text(98 + XR / 2, 20, buffer, 2);

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

        // orto y ocaso con la posicion propia; cada 4 s se alterna con la luna (fase, iluminada, salida y puesta); en
        // pantallas de 160 px, el sol o la luna pequenos a la derecha
        double lat, lon;
        if (ownPosition(lat, lon) && (millis() / 4000) % 2 == 1) {
            const MoonInfo& mi = moonInfo(t, lat, lon);
            char a[6] = "--:--", b[6] = "--:--";
            if (mi.hasRise) fmtLocalMin(mi.riseMin, offsetMin, a, sizeof(a));
            if (mi.hasSet) fmtLocalMin(mi.setMin, offsetMin, b, sizeof(b));
            snprintf(buffer, sizeof(buffer), "Moon %d%%%c %s-%s", (int)lround(mi.lit * 100), mi.waxing ? '+' : '-', a, b);   // + creciente, - menguante
            text(4, 56, buffer);
            if (XR >= 16) drawMoonDisc(PW - 10, 47, 6, mi, lat < 0);
        } else if (ownPosition(lat, lon)) {
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
            if (XR >= 16) drawSunIcon(PW - 10, 47, 4);
        }
    }

    const char* regionName() {
        #ifdef HAS_SA868
        static const char* const R[] = { "R1", "R1", "R2", "R3" };
        return R[constrain(Config.sa868.iaruRegion, 1, 3)];                         // T-TWR: region IARU de la radio FM
        #endif
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

    #ifdef HAS_SA868
    // ---- FM (voz), T-TWR: frecuencia grande, canal, tono, silenciador, estado -----------------------------------------
    void drawFmPage() {
        const FmCore::Channel& c = FM_Utils::channel();
        char right[8];
        if (FM_Utils::satOn()) snprintf(right, sizeof(right), "SAT");
        else if (FM_Utils::memMode()) snprintf(right, sizeof(right), "M%02d", FM_Utils::memIndex() + 1);
        else snprintf(right, sizeof(right), "VFO");
        header("FM", right);
        const bool tx = FM_Utils::ptt();
        char f[12];
        FmCore::formatFreq(tx ? FmCore::txHz(c) : c.rxHz, f, sizeof(f));
        pagesInk(INK_ACCENT);
        text(0, 12, f, 2);                                  // 8 caracteres de 12 px
        pagesInk(tx ? INK_BAD : INK_GOOD);
        text(104 + XR, 12, tx ? "TX" : (FM_Utils::busy() ? "RX" : "  "));
        pagesInk(INK_TEXT);
        text(104 + XR, 20, FM_Utils::edit() == FM_Utils::ED_FREQ ? (FM_Utils::memMode() ? "CH" : "<>") : "  ");
        char sh[12], tone[12], line[28];
        if (c.shift == 0) snprintf(sh, sizeof(sh), "S");
        else snprintf(sh, sizeof(sh), "%c%.2f", c.shift > 0 ? '+' : '-', c.offsetHz / 1e6);
        switch (c.toneMode) {
            case FmCore::TONE_ENC:  snprintf(tone, sizeof(tone), "T%.1f", c.tone / 10.0); break;
            case FmCore::TONE_TSQL: snprintf(tone, sizeof(tone), "TS%.1f", c.tone / 10.0); break;
            case FmCore::TONE_DCS:  snprintf(tone, sizeof(tone), "D%03u%c", (unsigned)c.dcs, c.dcsInv ? 'I' : 'N'); break;
            case FmCore::TONE_1750: snprintf(tone, sizeof(tone), "1750"); break;
            default:                snprintf(tone, sizeof(tone), "-"); break;
        }
        snprintf(line, sizeof(line), "%s %s %c %c", sh, tone, c.wide ? 'W' : 'N', c.high ? 'H' : 'L');
        text(0, 29, line);
        // nombre de la memoria o paso; silenciador a la derecha
        char sq[10];
        snprintf(sq, sizeof(sq), "SQL%s%d", Config.fm.squelchAuto ? "A" : " ", FM_Utils::squelch());
        if (FM_Utils::satOn()) snprintf(line, sizeof(line), "%.8s F%+d", c.name, FM_Utils::satFineHz());
        else if (FM_Utils::memMode() && c.name[0]) snprintf(line, sizeof(line), "%.8s", c.name);
        else snprintf(line, sizeof(line), "%.2fk", FM_Utils::stepHz() / 1000.0);
        text(0, 38, line);
        text(PW - 6 * (int)strlen(sq), 38, sq);
        // estado
        if (tx) snprintf(line, sizeof(line), "TX %lus", (unsigned long)FM_Utils::txSeconds());
        else if (FM_Utils::eaiOn()) snprintf(line, sizeof(line), "EAI %lu:%02lu", (unsigned long)(FM_Utils::eaiRemainingS() / 60), (unsigned long)(FM_Utils::eaiRemainingS() % 60));
        else if (FM_Utils::scanning()) snprintf(line, sizeof(line), "SCAN %s", FM_Utils::scanPaused() ? "HOLD" : ">>");
        else if (FM_Utils::artsOn()) snprintf(line, sizeof(line), "ARTS %s", FM_Utils::artsState() == FmCore::ARTS_IN ? "IN RANGE" : (FM_Utils::artsState() == FmCore::ARTS_OUT ? "OUT RANGE" : "..."));
        #ifdef HAS_SAT
        else if (FM_Utils::satOn() && !FM_Utils::busy()) {
            SatCore::Look lk;
            if (SAT_Utils::live(lk)) snprintf(line, sizeof(line), "El%+d D%+.1fk%s", (int)lround(lk.elDeg), FM_Utils::satDopplerHz() / 1000.0, FM_Utils::satListenUplink() ? " UP" : "");
            else snprintf(line, sizeof(line), "SAT: no time/pos");
        }
        #endif
        else if (FM_Utils::busy()) {
            const int r = FM_Utils::rssi();
            if (r != 0) { char sm[10]; sMeterText(r, sm, sizeof(sm)); snprintf(line, sizeof(line), "%s %d dBm", sm, r); }
            else snprintf(line, sizeof(line), "BUSY");
        } else line[0] = 0;
        text(0, 47, line);
        if (Config.fm.aprsBeacon) text(PW - 24, 47, "APRS");
        // aviso o lo que mueve el encoder
        const char* n = FM_Utils::notice();
        const char* const EDITS[FM_Utils::ED_COUNT] = { "frequency", "tone", "offset", "squelch" };
        if (n[0]) snprintf(line, sizeof(line), "%s", n);
        else snprintf(line, sizeof(line), "Dial: %s", FM_Utils::edit() == FM_Utils::ED_FREQ ? (FM_Utils::satOn() ? "fine 100Hz" : (FM_Utils::memMode() ? "channel" : "frequency")) : EDITS[FM_Utils::edit()]);
        text(0, 56, line);
    }

    bool tuneButtonDown() {
        #ifdef BUTTON_PIN
            return digitalRead(BUTTON_PIN) == LOW;
        #else
            return false;
        #endif
    }

    // Portadora sin modular de 30 s en la frecuencia de emision en uso, para medir el error con un SDR (Frequency calibration)
    void radioTuneCarrier() {
        if (!SA868::carrier(true)) { showToast("Radio busy"); return; }
        bool armed = !tuneButtonDown();
        const uint32_t startMs = millis();
        uint32_t lastDraw = 0;
        const long hz = SA868::txFreqHz();
        for (;;) {
            const bool down = tuneButtonDown();
            if (!down) armed = true;
            else if (armed) break;
            const uint32_t elapsed = millis() - startMs;
            if (elapsed >= 30000UL) break;
            if (millis() - lastDraw > 500) {
                lastDraw = millis();
                themeBegin();
                header("TUNE", "TX");
                row(1, "%.4f MHz", hz / 1e6);
                row(2, "Carrier on, %us left", (unsigned)(30 - elapsed / 1000));
                row(3, "Correction %+.3f ppm", Config.radioPpm1000 / 1000.0);
                row(4, "Measure with an SDR");
                row(5, "Button: stop");
                pagesFlush();
            }
            delay(5);
        }
        SA868::carrier(false);
        displayTime = millis();
        showToast("Tune stopped");
    }

    // QSY a lo que anuncia la estacion (frecuencia en el comentario o en el nombre del objeto): en FM al momento; si no, se
    // deja en el VFO y se arranca en FM
    void qsyToStation(const Station* s) {
        if (!s) { showToast("No station"); return; }
        FmCore::Qsy q;
        if (!FmCore::parseQsy(s->call, s->comment, q)) { showToast("No frequency"); return; }
        FmCore::Channel ch;
        FmCore::qsyToChannel(q, Config.sa868.iaruRegion, ch);
        if (!SA868::validFreq(ch.rxHz) || !SA868::validFreq(FmCore::txHz(ch))) { showToast("Not in this band"); return; }
        if (Config.operatingMode == MODE_FM) { FM_Utils::qsy(q); return; }
        Config.fm.vfo.rxHz = (int)ch.rxHz; Config.fm.vfo.shift = ch.shift; Config.fm.vfo.offsetHz = (int)ch.offsetHz;
        Config.fm.vfo.toneMode = ch.toneMode; Config.fm.vfo.tone = ch.tone; Config.fm.vfo.dcs = ch.dcs; Config.fm.vfo.wide = ch.wide;
        Config.fm.vfo.name = "";
        Config.fm.memMode = false;
        Config.writeFile();
        BLE_Control::setBootMode(MODE_FM);
        showToast("QSY: to FM...");
        PAGES_Utils::show();
        delay(600);
        ESP.restart();
    }
    #endif

    // ---- Radio comercial (receptor I2C) -----------------------------------------------------------------------------
    void drawBcastPage() {
        header("BCAST", BCAST::bandName(BCAST::band()));
        if (!BCAST::isOn()) { row(1, "%s: off", BCAST::chipName()); row(3, "Long press: Radio on"); return; }
        char f[16];
        const uint32_t k = BCAST::freqKHz();
        if (BCAST::band() == BCAST::BAND_FM) snprintf(f, sizeof(f), "%3lu.%02lu", (unsigned long)(k / 1000), (unsigned long)((k % 1000) / 10));
        else snprintf(f, sizeof(f), "%5lu", (unsigned long)k);
        text(0, 12, f, 2);
        text(76, 20, BCAST::band() == BCAST::BAND_FM ? "MHz" : "kHz");
        const int pr = BCAST::presetIndexNow();
        if (pr >= 0) { char p[6]; snprintf(p, sizeof(p), "P%d", pr + 1); text(104 + XR, 12, p); }
        if (BCAST::stereo()) text(104 + XR, 20, "ST");
        char line[28];
        const char* ps = BCAST::stationName();
        snprintf(line, sizeof(line), "%s", ps[0] ? ps : (BCAST::seeking() ? "Seeking..." : ""));
        text(0, 29, line);
        char rt[70];
        BCAST::radioText(rt, sizeof(rt));
        const size_t len = strlen(rt);
        if (len > (size_t)COLS) {                           // texto largo: se desplaza
            const size_t off = (millis() / 400) % (len - COLS + 1);
            snprintf(line, sizeof(line), "%.*s", COLS, rt + off);
        } else snprintf(line, sizeof(line), "%s", rt);
        text(0, 38, line);
        snprintf(line, sizeof(line), "RSSI %d", BCAST::rssi());
        if (BCAST::snr()) snprintf(line + strlen(line), sizeof(line) - strlen(line), " SNR %d", BCAST::snr());
        text(0, 47, line);
        char v[10];
        if (BCAST::muted()) snprintf(v, sizeof(v), "MUTE"); else snprintf(v, sizeof(v), "Vol %d", BCAST::volume());
        text(PW - 6 * (int)strlen(v), 47, v);
        int y, mo, d, h, mi, off;
        if (BCAST::rdsClock(y, mo, d, h, mi, off)) snprintf(line, sizeof(line), "RDS %02d:%02d UTC%+d", h, mi, off / 2);
        else snprintf(line, sizeof(line), "%s", BCAST::chipName());
        text(0, 56, line);
    }

    void drawLink() {
        #ifdef HAS_SA868
        header("RADIO", regionName());
        #else
        header(PAGE_TITLES[PAGE_LINK], regionName());
        #endif
        const LoraType& lora = *currentLoRaType;
        int sf = lora.spreadingFactor;
        long bw = lora.signalBandwidth;
        int cr = lora.codingRate4;
        const bool fskNow = Config.operatingMode == MODE_APRS_FSK && !lora.ais && LoRa_Utils::fskAvailable();
        char rate[12];
        #ifdef HAS_SA868                                // T-TWR: radio FM SA868 (AFSK 1200, G3RUH 9600 o M17)
        (void)fskNow; (void)rate; (void)sf; (void)bw; (void)cr;
        row(0, "%.4fMHz %ddBm", LoRa_Utils::liveFreqHz() / 1000000.0, LoRa_Utils::getPower());
        row(1, "%s %s", SA868::modemName(), (Config.sa868.wide || SA868::mode9600() || SA868::modeM17()) ? "25k" : "12.5k");
        #else
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
        #endif
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
        const int bw = 56 + XR;                             // la barra usa el ancho que sobra
        pagesInk(INK_FRAME);
        pagesDrawRect(40, y, bw, 8, true);
        pagesInk(INK_TEXT);
        if (percent < 0) {
            text(100 + XR, y, "n/a");
            return;
        }
        if (percent > 100) percent = 100;
        int fill = ((bw - 2) * percent) / 100;
        pagesInk(percent >= 90 ? INK_BAD : (percent >= 75 ? INK_WARN : INK_GOOD));
        if (fill > 0) pagesFillRect(41, y + 1, fill, 6, true);
        pagesInk(INK_TEXT);
        char value[8];
        snprintf(value, sizeof(value), "%3d%%", percent);
        text(100 + XR, y, value);
    }

    // Hay algo que mostrar en la vista LINK: Bluetooth encendido, WiFi KISS activo o punto de acceso de configuracion
    bool linkAvailable() {
        return bluetoothActive || KISS_NET::state() != 0 || (WiFi.getMode() & WIFI_MODE_AP);
    }

    // Vista LINK dentro de SYSTEM: conexiones activas (Bluetooth y WiFi) con la IP del enlace
    void drawBoardLink() {
        header("LINK", "SYSTEM");
        if (bluetoothActive) {
            row(0, "BT %s %s  %s", Config.bluetooth.useBLE ? "BLE" : "Classic", Config.bluetooth.useKISS ? "KISS" : "TNC2", bluetoothConnected ? "CON" : "wait");
            row(1, "  %.19s", Config.bluetooth.deviceName.c_str());
        } else {
            row(0, "BT: off");
        }
        const uint8_t kiss = KISS_NET::state();
        if (WiFi.getMode() & WIFI_MODE_AP) {
            row(2, "WiFi AP");
            row(3, "IP %s", WiFi.softAPIP().toString().c_str());
        } else if (kiss == 2) {
            row(2, "WiFi %.15s", WiFi.SSID().c_str());
            row(3, "IP %s", KISS_NET::localIp().c_str());
            if (Config.kissNet.enabled) row(4, "KISS TCP %d  Cli %u", Config.kissNet.tcpPort, (unsigned)KISS_NET::clientCount());
            if (Config.cat.enabled)     row(Config.kissNet.enabled ? 5 : 4, "CAT TCP %d  Cli %u", Config.cat.port, (unsigned)CAT_NET::clientCount());
            if (!(Config.kissNet.enabled && Config.cat.enabled)) {
                if (Config.kissNet.enabled && Config.kissNet.udpPort > 0) row(5, "UDP %d  %d dBm", Config.kissNet.udpPort, (int)WiFi.RSSI());
                else                                                      row(5, "RSSI %d dBm", (int)WiFi.RSSI());
            }
        } else if (kiss == 1) {
            row(2, "WiFi %.15s", Config.fwUpdate.ssid.c_str());
            row(3, "Joining...");
        } else {
            row(2, "WiFi: off");
        }
    }


    // ---------- SYSTEM > BATTERY (Manuel - EA7JRS): carga, tension, estado, tiempo que queda y ahorro ----------
    bool batteryViewAvailable() {
        #if defined(BATTERY_PIN) || defined(HAS_AXP192) || defined(HAS_AXP2101)
            return true;
        #else
            return false;
        #endif
    }

    void drawBattery() {
        const uint8_t st = BATTERY_Utils::state();
        static const char* const SHORT[] = { "NONE", "...", "DIS", "CHG", "FULL" };
        header("BATTERY", SHORT[st < 5 ? st : 1]);
        if (st == BatteryModel::ST_NONE) {
            row(0, "No battery");
            row(1, "USB power");
            return;
        }
        const float pct = BATTERY_Utils::percent();
        const float volts = BATTERY_Utils::cellVoltage() * BATTERY_CELLS;
        #ifdef HAS_AXP2101
            String pmuPct = POWER_Utils::getBatteryInfoCurrent();     // la AXP2101 da su propio porcentaje (por ese nombre)
            pmuPct.trim();
            row(0, "%s%%  %.2f V", pmuPct.c_str(), volts);
        #else
            row(0, "%.0f%%  %.2f V", pct, volts);
        #endif
        // pila: contorno, borne y relleno por la carga (verde, ambar por debajo del 40 %, rojo por debajo del 15 %)
        const int bx = 2, by = 21, bw = PW - 10, bh = 13;
        pagesInk(INK_FRAME);
        pagesDrawRect(bx, by, bw, bh, true);
        pagesFillRect(bx + bw, by + 4, 3, bh - 8, true);
        const int fill = (int)lroundf((bw - 4) * (pct < 0 ? 0 : (pct > 100 ? 100 : pct)) / 100.0f);
        pagesInk(pct < 15 ? INK_BAD : (pct < 40 ? INK_WARN : INK_GOOD));
        if (fill > 0) pagesFillRect(bx + 2, by + 2, fill, bh - 4, true);
        pagesInk(INK_TEXT);

        row(3, "%s", BatteryModel::stateName((BatteryModel::State)st));
        float vph = 0, pph = 0;
        const bool have = BATTERY_Utils::trend(vph, pph);
        const float h = BATTERY_Utils::hoursLeft();
        char left[12];
        if (h >= 0) formatShort((uint32_t)(h * 3600.0f), left, sizeof(left));
        if (h >= 0 && st == BatteryModel::ST_DISCHARGING) row(4, "Left ~%s %+.1f%%/h", left, pph);
        else if (h >= 0 && st == BatteryModel::ST_CHARGING) row(4, "Full in ~%s", left);
        else if (have) row(4, "Trend %+.2f V/h", vph);
        else row(4, "Trend: measuring...");
        #if defined(HAS_AXP2101)
            row(5, "Charger: %s", POWER_Utils::chargerState());       // la AXP2101 no mide la corriente de la bateria
        #elif defined(HAS_AXP192)
            row(5, "I %.0f mA", POWER_Utils::getBatteryChargeDischargeCurrent());
        #else
            if (Config.battery.saveBelow == 0) row(5, "Saving: off");
            else row(5, "Saving <%u%%: %s", (unsigned)Config.battery.saveBelow, BATTERY_Utils::saving() ? "ON" : "ready");
        #endif
    }

    void drawBoard() {
        if (boardView == 1 && !linkAvailable()) boardView = 0;
        if (boardView == 2 && !batteryViewAvailable()) boardView = 0;
        if (boardView == 1) { drawBoardLink(); return; }
        if (boardView == 2) { drawBattery(); return; }
        header(PAGE_TITLES[PAGE_BOARD], batteryViewAvailable() ? "1P BATT" : (linkAvailable() ? "1P LINK" : nullptr));
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
        line[COLS] = '\0';
        text(2, 47, line);

        const char* bt = !bluetoothActive ? "off" : (bluetoothConnected ? "CON" : "on");
        const uint8_t kiss = KISS_NET::state();
        const char* wifi = (WiFi.getMode() & WIFI_MODE_AP) ? "AP" : (kiss == 2 ? "CON" : (kiss == 1 ? ".." : "off"));
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
        page = modeHomePage();                  // el modo elegido al arrancar se queda en su pagina (FM voice: la emisora, no MAIN)
        if (!pageVisible(page)) page = PAGE_MAIN;
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

    static void triggerAlarm(const char* call, AlarmKind kind, const char* detail, uint32_t nowMs, bool fromMsg = false) {
        bool same = alarmCall[0] && strcmp(alarmCall, call) == 0;
        bool known = same && (uint32_t)(nowMs - alarmMs) < ALARM_REPEAT_MS;
        snprintf(alarmCall, sizeof(alarmCall), "%s", call);
        snprintf(alarmText, sizeof(alarmText), "%s", detail);
        alarmKind = kind;
        alarmMs = nowMs;
        alarmFromMsg = fromMsg;
        if (known) return;                          // ya avisada: solo se actualizan los datos
        alarmSeq++;
        alarmSounding = true;
        alarmShown = true;
        alarmStartMs = nowMs;
        alarmBeepMs = 0;                            // suena en la proxima vuelta
        alarmWakeMs = 0;
        submenuOpen = false;
        submenuId = SM_PAGE;
    }

    static void goToStation(const Station* selected);
    // ---- AIS: barcos como destino de la brujula / rotor y alarmas (Manuel - EA7JRS) ----
    // Se anade (o actualiza) en la lista de estaciones con su MMSI como indicativo; open = abrir la brujula hacia el
    void aisTarget(const char* id, bool hasPos, double lat, double lon, float speedKmh, float course, const char* comment, bool open) {
        if (!open && !(compassTarget && strcmp(targetCall, id) == 0) && heard.indexOf(id) < 0) return;   // solo el que se sigue o ya esta en la lista
        heard.update(id, 's', 0, hasPos, (float)lat, (float)lon, 0, (int)lroundf(speedKmh), (int)lroundf(course), 0, 0.0f, millis(),
                     "AIS", nullptr, comment, nullptr, '/');
        if (open) goToStation(heard.byRecent((uint8_t)max(0, heard.indexOf(id))));
    }

    void aisAlarm(const char* id, const char* detail, bool distress, bool hasPos, double lat, double lon) {
        if (hasPos) heard.update(id, distress ? 'Y' : 's', 0, true, (float)lat, (float)lon, 0, 0, 0, 0, 0.0f, millis(), "AIS", nullptr, detail, nullptr, '/');
        triggerAlarm(id, distress ? ALARM_AIS_SOS : ALARM_AIS_CPA, detail, millis());
    }

    void teamOnPacket(const APRSPacket& packet, bool hasPos, uint32_t now);      // definidas mas abajo (vigilancia de grupo y EAI)
    void teamOnMessage(const APRSPacket& packet, uint32_t now);

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
        teamOnPacket(packet, hasPosition, nowMs);
        if (packet.type == 1) teamOnMessage(packet, nowMs);
        {   // alarmas: estado Mic-E Emergency o palabras SOS / EMERGENCY / MOB en el mensaje o comentario
            const char* body = (packet.type == 1 || packet.type == 0 || packet.type == 2 || packet.type == 4 || packet.type == 5) ? packet.payload.c_str() : "";
            AlarmKind kind = alarmFromText(body);
            const char* detail = body;
            if (kind == ALARM_NONE && packet.type == 4 && packet.miceType == "000") { kind = ALARM_EMERGENCY; detail = "Mic-E: Emergency!"; }
            if (kind != ALARM_NONE) triggerAlarm(packet.sender.c_str(), kind, detail, nowMs, packet.type == 1);
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

    // alarma de emergencia recibida sonando o baliza SOS propia en curso (estroboscopio del flash)
    bool emergencyFlash() {
        return alarmSounding || sosState != SOS_IDLE;
    }

    static void changedPage();

    // ---------- Vigilancia de grupo (estilo ARTS) y emergencia remota (estilo EAI) ----------
    // Copyright (C) 2026 Manuel - EA7JRS
    const uint8_t TEAM_MAX = 5;
    enum : uint8_t { TS_UNKNOWN, TS_IN, TS_OUT };
    struct TeamSlot {
        char        call[11];
        char        last[11];       // indicativo completo (con SSID) con el que se oyo por ultima vez
        bool        full;           // el indicativo lleva SSID: solo coincide ese; sin SSID coincide cualquiera
        bool        seen;
        bool        hasPos;
        float       lat, lon;
        uint32_t    seenMs;
        uint8_t     state;
    };
    TeamSlot    teamSlots[TEAM_MAX];
    uint8_t     teamCount       = 0;
    String      teamParsed;
    bool        teamParsedOnce  = false;
    uint32_t    teamStartMs     = 0;
    uint32_t    teamTickMs      = 0;
    uint32_t    teamPollMs      = 0;

    // "EA7AAA-7, ea7bbb" -> indicativos en mayusculas (3-9 caracteres A-Z 0-9 -); devuelve cuantos
    uint8_t teamParseCalls(const String& list, char (*out)[11], bool* full, uint8_t maxCount) {
        uint8_t n = 0;
        char token[11];
        uint8_t len = 0;
        bool bad = false;
        for (size_t i = 0; i <= list.length(); i++) {
            const char c = (i < list.length()) ? (char)toupper((unsigned char)list[i]) : ' ';
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-') {
                if (len < 10) token[len++] = c; else bad = true;
            } else {                                                // separador (coma, espacio, punto y coma...)
                if (len >= 3 && !bad && n < maxCount) {
                    token[len] = '\0';
                    memcpy(out[n], token, len + 1);
                    full[n] = strchr(token, '-') != nullptr;
                    n++;
                }
                len = 0;
                bad = false;
            }
        }
        return n;
    }

    bool teamCallMatches(const char* pattern, bool full, const String& sender) {
        String up = sender;
        up.toUpperCase();
        if (full) return up.equals(pattern);
        int dash = up.indexOf('-');
        if (dash > 0) up = up.substring(0, dash);
        return up.equals(pattern);
    }

    void teamEnsureParsed() {
        if (teamParsedOnce && teamParsed == Config.team.watchCalls) return;
        char calls[TEAM_MAX][11];
        bool full[TEAM_MAX];
        teamParsed      = Config.team.watchCalls;
        teamParsedOnce  = true;
        teamCount       = teamParseCalls(teamParsed, calls, full, TEAM_MAX);
        for (uint8_t i = 0; i < teamCount; i++) {
            memset(&teamSlots[i], 0, sizeof(TeamSlot));
            memcpy(teamSlots[i].call, calls[i], sizeof(teamSlots[i].call));
            teamSlots[i].full  = full[i];
            teamSlots[i].state = TS_UNKNOWN;
        }
        teamStartMs = 0;
    }

    void teamOnPacket(const APRSPacket& packet, bool hasPos, uint32_t now) {
        if (!Config.team.watchActive) return;
        teamEnsureParsed();
        for (uint8_t i = 0; i < teamCount; i++) {
            if (!teamCallMatches(teamSlots[i].call, teamSlots[i].full, packet.sender)) continue;
            teamSlots[i].seen   = true;
            teamSlots[i].seenMs = now ? now : 1;
            snprintf(teamSlots[i].last, sizeof(teamSlots[i].last), "%s", packet.sender.c_str());
            if (hasPos) { teamSlots[i].hasPos = true; teamSlots[i].lat = packet.latitude; teamSlots[i].lon = packet.longitude; }
        }
    }

    void teamAlert(const TeamSlot& slot) {
        char message[28];
        snprintf(message, sizeof(message), "%s %.9s", slot.state == TS_IN ? "IN RNG" : "OUT RNG", slot.call);
        showToast(message);
        displayToggle(true);
        displayTime = millis();
        if (Config.notification.buzzerActive) {
            if (slot.state == TS_IN) NOTIFICATION_Utils::messageBeep();
            else                     NOTIFICATION_Utils::countdownBeep();
        }
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Group", "%s", message);
    }

    // Una vez por segundo: pasa cada estacion a IN / OUT segun el tiempo sin oirla y la distancia; avisa en cada cambio; sondeo opcional
    void teamPoll(uint32_t now) {
        if (!Config.team.watchActive || !aprsMode()) return;
        if (now - teamTickMs < 1000UL) return;
        teamTickMs = now;
        teamEnsureParsed();
        if (teamCount == 0) return;
        if (teamStartMs == 0) { teamStartMs = now ? now : 1; teamPollMs = now; }
        const uint32_t timeoutMs = (uint32_t)constrain(Config.team.timeoutMin, 1, 120) * 60000UL;
        double myLat = 0, myLon = 0;
        const bool haveMe = ownPosition(myLat, myLon);
        for (uint8_t i = 0; i < teamCount; i++) {
            TeamSlot& slot = teamSlots[i];
            uint8_t next;
            if (!slot.seen) {
                next = (now - teamStartMs > timeoutMs) ? TS_OUT : TS_UNKNOWN;       // tras el arranque se espera un tiempo antes de dar a nadie por perdido
            } else {
                const bool fresh = (now - slot.seenMs) <= timeoutMs;
                const bool far   = Config.team.maxKm > 0 && slot.hasPos && haveMe && distanceKm(myLat, myLon, slot.lat, slot.lon) > Config.team.maxKm;
                next = (fresh && !far) ? TS_IN : TS_OUT;
            }
            if (next != slot.state) {
                slot.state = next;
                if (next != TS_UNKNOWN) teamAlert(slot);
            }
        }
        // sondeo: "ping" a quien lleva demasiado tiempo sin oirse (los trackers con este firmware responden "pong")
        if (Config.team.pollSec >= 60 && sosState == SOS_IDLE && (now - teamPollMs) >= (uint32_t)Config.team.pollSec * 1000UL) {
            teamPollMs = now;
            for (uint8_t i = 0; i < teamCount; i++) {
                const TeamSlot& slot = teamSlots[i];
                if (slot.seen && (now - slot.seenMs) < (uint32_t)Config.team.pollSec * 1000UL) continue;
                MSG_Utils::addToOutputBuffer(0, String(slot.call), "ping");
            }
        }
    }

    // Mensaje dirigido a este equipo: "EAI <codigo>" inicia la emergencia, "EAI STOP <codigo>" la termina (solo desde indicativos autorizados y con el codigo)
    void teamOnMessage(const APRSPacket& packet, uint32_t now) {
        if (!Config.team.eaiActive || Config.team.eaiCode.length() < 4 || !aprsMode()) return;
        if (!(packet.addressee == currentBeacon->callsign)) return;
        String body = packet.payload;
        body.trim();
        body.toUpperCase();
        if (!body.startsWith("EAI ")) return;
        char calls[TEAM_MAX][11];
        bool full[TEAM_MAX];
        const uint8_t n = teamParseCalls(Config.team.eaiCalls, calls, full, TEAM_MAX);
        bool authorised = false;
        for (uint8_t i = 0; i < n; i++) if (teamCallMatches(calls[i], full[i], packet.sender)) authorised = true;
        if (!authorised) return;                                    // sin respuesta: no se delata la funcion
        String rest = body.substring(4);
        rest.trim();
        bool stop = false;
        if (rest.startsWith("STOP ")) { stop = true; rest = rest.substring(5); rest.trim(); }
        String code = Config.team.eaiCode;
        code.trim();
        code.toUpperCase();
        if (rest != code) return;                                   // codigo incorrecto: se ignora
        displayToggle(true);
        displayTime = now;
        if (stop) {
            if (sosState != SOS_IDLE) sosCancel();
            MSG_Utils::addToOutputBuffer(0, packet.sender, "EAI OFF");
            return;
        }
        const int minutes = constrain(Config.team.eaiMinutes, 1, 50);
        if (sosState != SOS_ACTIVE) {
            sosState    = SOS_ACTIVE;
            sosNextMs   = now;
            sosSent     = 0;
            sosLastTick = 0;
            if (Config.notification.buzzerActive) NOTIFICATION_Utils::alarmBeep();
        }
        sosTimed = true;
        sosEndMs = now + (uint32_t)minutes * 60000UL;
        snprintf(sosBy, sizeof(sosBy), "%s", packet.sender.c_str());
        char message[28];
        snprintf(message, sizeof(message), "EAI by %.9s", packet.sender.c_str());
        showToast(message);
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "EAI", "Remote emergency started by %s (%d min)", packet.sender.c_str(), minutes);
        MSG_Utils::addToOutputBuffer(0, packet.sender, String("EAI ON ") + String(minutes) + "min");
    }

    // Radar tipo APL de Yaesu: posiciones de los miembros respecto a la propia, norte arriba, con escala automatica o fija
    const uint16_t TEAM_SCALES_KM[]   = { 1, 2, 5, 10, 20, 50, 100, 250, 500 };
    const uint8_t  TEAM_SCALE_COUNT   = sizeof(TEAM_SCALES_KM) / sizeof(TEAM_SCALES_KM[0]);

    void drawTeamRadar() {
        const int cx = 106 + XR, cy = 37, radius = 18;
        double myLat = 0, myLon = 0;
        const bool haveMe = ownPosition(myLat, myLon);
        // escala: la menor que contiene a todos los miembros con posicion (con un 10 % de margen)
        double farKm = 0;
        for (uint8_t i = 0; i < teamCount; i++) {
            const TeamSlot& slot = teamSlots[i];
            if (!(slot.seen && slot.hasPos && haveMe)) continue;
            const double km = distanceKm(myLat, myLon, slot.lat, slot.lon);
            if (km > farKm) farKm = km;
        }
        double scaleKm = TEAM_SCALES_KM[TEAM_SCALE_COUNT - 1];
        if (teamScale > 0 && teamScale <= TEAM_SCALE_COUNT) {
            scaleKm = TEAM_SCALES_KM[teamScale - 1];
        } else {
            for (uint8_t i = 0; i < TEAM_SCALE_COUNT; i++) if (TEAM_SCALES_KM[i] >= farKm * 1.1) { scaleKm = TEAM_SCALES_KM[i]; break; }
        }
        char scaleText[12], right[16];
        fmtDist(scaleKm, scaleText, sizeof(scaleText));
        snprintf(right, sizeof(right), "%s%s", teamScale == 0 ? "A " : "", scaleText);
        header("RADAR", right);

        pagesDrawCircle(cx, cy, radius, true);
        pagesDrawLine(cx - 2, cy, cx + 2, cy, true);                // la propia estacion, en el centro
        pagesDrawLine(cx, cy - 2, cx, cy + 2, true);
        text(cx - 2, cy - radius + 2, "N");

        const uint32_t now = millis();
        for (uint8_t i = 0; i < teamCount; i++) {
            const TeamSlot& slot = teamSlots[i];
            char distance[12] = "--";
            if (slot.seen && slot.hasPos && haveMe) {
                const double km  = distanceKm(myLat, myLon, slot.lat, slot.lon);
                const double brg = bearingDeg(myLat, myLon, slot.lat, slot.lon);
                const double frac = km / scaleKm;
                const bool outside = frac > 1.0;
                int x, y;
                polar(cx, cy, (float)((outside ? 1.0 : frac) * (radius - 2)), (float)brg, x, y);
                if (slot.state == TS_IN && !outside) pagesFillCircle(x, y, 2, true);          // en rango: relleno
                else                                  pagesDrawCircle(x, y, 2, true);         // fuera de rango o de escala: hueco
                char tag[2] = { (char)('1' + i), '\0' };
                text(x > cx + 8 ? x - 9 : x + 4, y - 3, tag);
                fmtDist(km, distance, sizeof(distance));
            } else if (slot.seen) {
                formatAge(now - slot.seenMs, distance, sizeof(distance));
            }
            char line[24];
            snprintf(line, sizeof(line), "%u%.6s %s", (unsigned)(i + 1), slot.call, distance);
            text(2, 11 + 9 * i, line);
        }
        if (!haveMe) text(2, 56, "No own position");
        else         text(2, 56, "1P zoom LP menu");
    }

    void drawTeamPage() {
        teamEnsureParsed();
        if (teamCount == 0) teamMode = TMODE_LIST;
        if (teamMode == TMODE_RADAR) { drawTeamRadar(); return; }
        uint8_t inRange = 0;
        for (uint8_t i = 0; i < teamCount; i++) if (teamSlots[i].state == TS_IN) inRange++;
        char right[10];
        snprintf(right, sizeof(right), "%u/%u", (unsigned)inRange, (unsigned)teamCount);
        header(PAGE_TITLES[PAGE_TEAM], right);
        if (teamCount == 0) {
            row(1, "No callsigns set");
            row(2, "WebUI > Station >");
            row(3, "Group watch");
            return;
        }
        if (teamCursor >= teamCount) teamCursor = 0;
        double myLat = 0, myLon = 0;
        const bool haveMe = ownPosition(myLat, myLon);
        const uint32_t now = millis();
        const bool browse = teamMode == TMODE_BROWSE;
        for (uint8_t i = 0; i < teamCount; i++) {
            const TeamSlot& slot = teamSlots[i];
            char extra[12];
            if (slot.seen && slot.hasPos && haveMe) fmtDist(distanceKm(myLat, myLon, slot.lat, slot.lon), extra, sizeof(extra));
            else if (slot.seen)                      formatAge(now - slot.seenMs, extra, sizeof(extra));
            else                                     snprintf(extra, sizeof(extra), "--");
            const char* state = slot.state == TS_IN ? "IN" : (slot.state == TS_OUT ? "OUT" : "..");
            if (browse) {                                           // el miembro marcado va en video inverso (como en STATIONS)
                char line[28];
                snprintf(line, sizeof(line), "%-9.9s %-3s%6s", slot.call, state, extra);
                const int y = ROW_Y0 + i * ROW_PITCH;
                const bool selected = (i == teamCursor);
                if (selected) { pagesInk(INK_SEL); pagesFillRect(0, y - 1, PW, 9, true); }
                pagesText(0, y, line, 1, selected);
                pagesInk(INK_TEXT);
            } else {
                row(i, "%-9.9s %-3s %s", slot.call, state, extra);
            }
        }
        if (browse) {                                               // rumbo y distancia del miembro marcado, como la pantalla GM de Yaesu
            const TeamSlot& slot = teamSlots[teamCursor];
            char line[28];
            if (slot.seen && slot.hasPos && haveMe) {
                char distance[12];
                const double brg = bearingDeg(myLat, myLon, slot.lat, slot.lon);
                fmtDist(distanceKm(myLat, myLon, slot.lat, slot.lon), distance, sizeof(distance));
                snprintf(line, sizeof(line), "B%03d %s %s", (int)lround(brg) % 360, cardinal16(brg), distance);
            } else {
                snprintf(line, sizeof(line), "%s", slot.seen ? "No position yet" : "Not heard yet");
            }
            text(2, 56, line);
            return;
        }
        if (Config.team.pollSec >= 60) row(5, "OUT %um  poll %us", (unsigned)Config.team.timeoutMin, (unsigned)Config.team.pollSec);
        else                           row(5, "OUT after %u min", (unsigned)Config.team.timeoutMin);
    }

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

    bool alarmInfo(const char*& call, const char*& text, uint8_t& kind, bool& msg, uint16_t& seq, bool& sounding) {
        call = alarmCall;
        text = alarmText;
        kind = alarmKind;
        msg = alarmFromMsg;
        seq = alarmSeq;
        sounding = alarmSounding;
        return alarmShown;
    }

    void alarmSilence() { alarmSounding = false; }

    uint8_t sosInfo(uint32_t& leftS, uint16_t& sent, const char*& by) {
        const uint32_t now = millis();
        leftS = (sosState == SOS_ACTIVE && sosTimed && (int32_t)(sosEndMs - now) > 0) ? (sosEndMs - now + 999) / 1000 : 0;
        sent = sosSent;
        by = sosBy;
        return sosState == SOS_ACTIVE ? 2 : sosState == SOS_COUNTDOWN ? 1 : 0;
    }

    void sosStop() { if (sosState != SOS_IDLE) sosCancel(); }

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
    void satPoll();                         // idem (modo SAT)
    void rotorPoll();                       // idem (rotor; cualquier modo)
    void cwBeaconPoll();                    // idem
    void cwKeypadPoll();                    // idem (teclado Yaesu FH-2)

    void update() {
        uint32_t nowMs = millis();
        #ifdef HAS_SA868
        {   // encoder de la T-TWR: sintoniza en la pagina FM o en la radio comercial; en las demas cuenta como actividad (hombre caido)
            const int clicks = FM_Utils::takeEncoder();
            if (clicks) {
                if (page == PAGE_FM && fmDialActive()) FM_Utils::dial(clicks);
                else if (page == PAGE_BCAST && viewing() && !submenuOpen) BCAST::step(clicks);
                else if (page == PAGE_AIS && viewing() && !submenuOpen) { aisSel += clicks; if (aisSel < 0) aisSel = 0; }
                #ifdef HAS_SAT
                else if (page == PAGE_SAT && viewing() && !submenuOpen) {
                    if (FM_Utils::satOn()) FM_Utils::satFine(clicks * 100);            // escuchando: ajuste fino de 100 Hz
                    else if (satView != 3 && satView != 4) {                          // si no, satelite siguiente / anterior de la lista
                        const int i = SAT_Utils::step(clicks > 0 ? 1 : -1);
                        if (i >= 0) { SAT_Utils::select(i); satTx = 0; }
                    }
                }
                #endif
                else FM_Utils::activity();
            }
        }
        #endif
        cwKeyPoll();
        cwKeypadPoll();
        teamPoll(nowMs);
        cwBeaconPoll();
        wsprPoll();
        ftPoll();
        lrfhssPoll();
        dapnetPoll();
        satPoll();
        rotorPoll();

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
        if (sosState == SOS_ACTIVE && sosTimed && (int32_t)(nowMs - sosEndMs) >= 0) {
            sosCancel();
            showToast("EAI ended");
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
    const size_t    CW_TEXT_MAX         = 4 * COLS;     // 4 filas de 21 columnas (26 en la Heltec)
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

    // Rotor, fuente 5 (Copyright (C) 2026 Manuel - EA7JRS): tras cada palabra recibida por RF se busca indicativo + locator de una
    // baliza ("VVV DE ED7YAA IM76HR"); si cambia, el rotor apunta al centro del cuadro desde el GPS del tracker
    void cwBeaconCheck() {
    #ifdef HAS_SAT
        char call[12], loc[10];
        if (!RotorFollow::parseCwBeacon(cwText.c_str(), call, sizeof(call), loc, sizeof(loc))) return;
        char oldCall[12], oldLoc[10];
        if (ROTOR_Utils::cwBeacon(oldCall, sizeof(oldCall), oldLoc, sizeof(oldLoc)) && strcmp(oldLoc, loc) == 0 && strcmp(oldCall, call) == 0) return;
        double lat, lon;
        if (!RotorFollow::locatorToLatLon(loc, lat, lon)) return;
        ROTOR_Utils::setCwBeacon(call, loc, lat, lon);
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Rotor", "CW beacon %s %s (%.4f, %.4f)", call[0] ? call : "?", loc, lat, lon);
    #endif
    }

    const char* cwModeLabel() {
        return Config.cw.keyMode == 0 ? "KEY" : (Config.cw.keyMode == 1 ? "IA" : "IB");
    }

    // S-meter para VHF / UHF (por encima de 30 MHz, IARU): S9 = -93 dBm, 6 dB por punto (S1 = -141 dBm); por encima de S9, +10, +20...
    void cwSMeter(float dbm, char* out, size_t n) {
        if (dbm < -141.0f) { snprintf(out, n, "S0"); return; }
        if (dbm >= -93.0f) {
            const int over = (int)((dbm + 93.0f + 5.0f) / 10.0f) * 10;
            if (over <= 0) snprintf(out, n, "S9"); else snprintf(out, n, "S9+%d", over);
            return;
        }
        snprintf(out, n, "S%d", constrain((int)((dbm + 147.0f) / 6.0f), 1, 9));
    }

    // Squelch del CW por RF: siguiente valor de la rueda OFF, -120, -115 ... -70 dBm (5 dB por paso)
    int cwNextSquelch(int current) {
        if (current == 0) return -120;
        return current + 5 > -70 ? 0 : current + 5;
    }

    void drawCwPage() {
        char right[12];
        if (cwRxActive) snprintf(right, sizeof(right), "RX %d", cwRxWpm);
        else            snprintf(right, sizeof(right), "%s %d", cwModeLabel(), Config.cw.wpm);
        header(PAGE_TITLES[PAGE_CW], right);
        if (cwRxActive) {
            // Recepcion: primera linea = S-meter, nivel (RSSI) y SNR; despues las 3 ultimas lineas de texto, la letra en curso y el squelch
            char sm[8], snr[6];
            cwSMeter(cwRxDbm, sm, sizeof(sm));
            if (cwRxSnr >= CWMorse::Envelope::MIN_SNR_DB) snprintf(snr, sizeof(snr), "%.0f", cwRxSnr); else snprintf(snr, sizeof(snr), "--");
            row(0, "%s %.0fdBm SNR %s", sm, cwRxDbm, snr);
            const size_t len = cwText.length();
            const size_t first = len > 3 * (size_t)COLS ? len - 3 * COLS : 0;      // las 3 lineas mas recientes
            for (uint8_t r = 0; r < 3; r++) {
                const size_t from = first + (size_t)r * COLS;
                if (from >= len) break;
                text(0, ROW_Y0 + (r + 1) * ROW_PITCH, cwText.substring(from, from + COLS).c_str());
            }
            if (cwPending[0]) row(4, "> %s", cwPending);
            if (Config.cw.squelchDbm == 0) row(5, "SQL OFF tap+ hold:end");
            else                           row(5, "SQL%4d tap+ hold:end", Config.cw.squelchDbm);
            return;
        }
        for (uint8_t r = 0; r < 4; r++) {
            size_t from = (size_t)r * COLS;
            if (from >= cwText.length()) break;
            String line = cwText.substring(from, from + COLS);
            text(0, ROW_Y0 + r * ROW_PITCH, line.c_str());
        }
        if (cwPending[0]) row(4, "> %s", cwPending);
        if (cwPin(Config.cw.keyPin) < 0) row(5, "No key pin (WebUI)");
        else row(5, "Key GPIO%d  Hold:macros", Config.cw.keyPin);
    }

    void cwDrawPage() {
        themeBegin();
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
        bool     tone = false;      // tono local en el zumbador (macros, FH-2, mensajes desde la app; no en la baliza CW desatendida)
    };

    bool cwButtonDown() {
        #ifdef BUTTON_PIN
            return digitalRead(BUTTON_PIN) == LOW;
        #else
            return false;
        #endif
    }

    void cwDraw(const CwRun& run, size_t done, const char* pattern) {
        themeBegin();
        char right[10];
        snprintf(right, sizeof(right), "%uWPM", run.wpm);
        header("CW TX", right);
        const int cols = COLS;
        size_t len = strlen(run.text);
        for (size_t i = 0; i < len && i < (size_t)cols * 4; i++) {
            char one[2] = { run.text[i], 0 };
            int col = (int)(i % cols), row = (int)(i / cols);
            pagesText(1 + col * 6, 12 + row * 10, one, 1, false);
            if (i < done) pagesDrawLine(1 + col * 6, 12 + row * 10 + 8, 1 + col * 6 + 4, 12 + row * 10 + 8, true);   // ya enviado
        }
        if (pattern && pattern[0] && pattern[0] != ' ') {                   // simbolo que acaba de salir, en puntos y rayas
            int x = 2;
            for (const char* e = pattern; *e && x < PW - 8; e++) {
                if (*e == '-') { pagesFillRect(x, 53, 9, 3, true); x += 12; }
                else           { pagesFillRect(x, 53, 3, 3, true); x += 6; }
            }
        }
        pagesZone(PZ_BOTTOM);
        pagesInk(INK_FRAME);
        pagesDrawRect(0, 58, PW, 6, true);
        pagesInk(INK_GOOD);
        if (len > 0) pagesFillRect(1, 59, (int)((PW - 2) * done / len), 4, true);
        pagesInk(INK_TEXT);
        pagesZone(PZ_BODY);
        pagesFlush();
    }

    void cwKeyHook(void* ctx, bool on) {
        CwRun* run = (CwRun*)ctx;
        run->keyDown = on;
        LoRa_Utils::cwKey(on);
        if (run->tone) NOTIFICATION_Utils::sidetone(on);
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
        run.tone    = !beacon && Config.cw.sidetone && Config.notification.buzzerActive;
        if (strlen(run.text) == 0) { showToast("CW: empty message"); return false; }
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "CW Tx", "---> %s (%u WPM)", run.text, run.wpm);
        cwDraw(run, 0, "");
        run.next = millis();
        CWMorse::Hooks hooks = { cwKeyHook, cwWaitHook, cwProgressHook, &run };
        bool finished = CWMorse::send(run.text, run.wpm, hooks);
        if (run.tone) NOTIFICATION_Utils::sidetone(false);         // por si se aborto con la tecla pulsada
        LoRa_Utils::cwEnd();
        cwAppendString(String(">") + run.text + " ");
        showToast(finished ? "CW sent" : "CW aborted");
        return finished;
    }

    void sendCw(uint8_t index) {
        if (index >= CW_MESSAGE_COUNT) return;
        sendCwText(cwExpand(Config.cw.messages[index]));
    }

    // Texto libre enviado desde la app por BLE (EA7JRS)
    bool cwSendText(const char* text) {
        String message = text;
        message.trim();
        message.toUpperCase();
        return sendCwText(message);
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
        if ((int)message.length() > COLS) message = message.substring(0, COLS - 1) + "~";
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
                const size_t from = (size_t)r * COLS;
                if (from >= len) break;
                char line[28];
                snprintf(line, sizeof(line), "%.*s", COLS, e->text + from);
                if (r == 2 && len > 3 * (size_t)COLS) line[COLS - 1] = '~';
                row(2 + r, "%s", line);
            }
        }
        row(5, "%ddBm B%lu M%lu%s", DAPNET_Utils::lastRssi(), (unsigned long)DAPNET_Utils::batches(), (unsigned long)DAPNET_Utils::messages(), DAPNET_Utils::lastTimeMs() ? " T" : "");
    }

    // ======================= SAT (satelites en 70 cm) =======================
    void satPoll() {
        if (Config.operatingMode != MODE_SAT) return;
        SAT_Utils::loop();
        SATRX_Utils::loop();
    }

    // El rotor sigue un objetivo terrestre tambien en los modos APRS (y el satelite solo en el modo SAT)
    void rotorPoll() {
    #ifdef HAS_SAT
        ROTOR_Utils::loop();
    #endif
    }

    // Duracion corta: "12m34s" o "1h05m"
    void satDuration(char* out, size_t n, double sec) {
        if (sec < 0) sec = 0;
        const long s = (long)sec;
        if (s >= 3600)      snprintf(out, n, "%ldh%02ldm", s / 3600, (s % 3600) / 60);
        else                snprintf(out, n, "%ldm%02lds", s / 60, s % 60);
    }

    void satHhmm(char* out, size_t n, double unixT) {
        const time_t t = (time_t)unixT;
        snprintf(out, n, "%02d:%02d:%02d", hour(t), minute(t), second(t));
    }

    // Pagina ROTOR (modos APRS): objetivo terrestre al que apunta el rotor
    void drawRotPage() {
    #ifdef HAS_SAT
        header(PAGE_TITLES[PAGE_ROT], ROTOR_Utils::stateText());
        const int rsrc = Config.rotor.source;
        if (rsrc == 1)      row(0, "APRS target (GO TO)");
        else if (rsrc == 4) row(0, "Last heard%s", Config.rotor.followFilter.isEmpty() ? "" : " (filter)");
        else if (rsrc == 5) row(0, "CW beacon (locator)");
        else                row(0, "Fixed %d/%d", Config.rotor.targetIndex + 1, RotorConfig::TARGETS);
        const RotorConfig::Target& t = Config.rotor.targets[Config.rotor.targetIndex];
        if (Config.rotor.source == 2 && t.name.isEmpty()) { row(1, "Slot empty"); row(2, "Set it in WebUI/app"); return; }
        float az, el; bool trk;
        if (!ROTOR_Utils::target(az, el, trk)) {
            row(1, "%s", rsrc == 2 ? t.name.c_str() : "No target yet");
            row(2, "%s", rsrc == 1 ? "STATIONS: GO TO" : rsrc == 4 ? "No station with pos" : rsrc == 5 ? "MORSE: Listen CW" : "Waiting for position");
            return;
        }
        row(1, "%.20s", ROTOR_Utils::targetName());
        row(2, "Az %03d El %02d %dkm", (int)lroundf(az) % 360, (int)lroundf(el), (int)lround(ROTOR_Utils::targetDistKm()));
        float ra, re;
        if (ROTOR_Utils::actual(ra, re)) row(3, "Rotor %03d/%02d", (int)lroundf(ra) % 360, (int)lroundf(re));
        else row(3, "Rotor: unknown");
        if (Config.rotor.source == 2 && t.freqHz > 0) row(4, "Freq %.4f MHz", t.freqHz / 1e6);
        row(5, "Hold: menu");
    #endif
    }

#ifdef HAS_SAT
    void drawSatPage() {
        const SatCore::Block* b = SAT_Utils::block();
        header(PAGE_TITLES[PAGE_SAT], SAT_Utils::haveQth() ? SAT_Utils::qthSource() : "NO POS");
        if (!SAT_Utils::hasData() || !b) {
            row(0, "No satellite data");
            row(1, "Send it from the");
            row(2, "Android app (SAT)");
            row(3, "or WebUI > SAT data");
            if (SAT_Utils::updateState() == SAT_Utils::UPD_ERROR) row(5, "%s", SAT_Utils::updateMessage());
            return;
        }
        row(0, "%u/%u %.14s", (unsigned)SAT_Utils::selected() + 1, (unsigned)SAT_Utils::count(), b->name);
        if (!SAT_Utils::haveTime()) { row(1, "Waiting for UTC time"); row(2, "(needs GPS fix)"); return; }
        if (!SAT_Utils::haveQth())  { row(1, "No position yet"); row(2, "GPS fix or WebUI QTH"); return; }

        SatCore::Look lk;
        const bool have = SAT_Utils::live(lk);
        if (satView == 0) {                                  // ahora
            if (have) {
                row(1, "Az %03d %-2s El %+3d", (int)lround(lk.azDeg), SatCore::cardinal(lk.azDeg), (int)lround(lk.elDeg));
                row(2, "Rng %dkm Alt %dkm", (int)lround(lk.rangeKm), (int)lround(lk.altKm));
                row(3, "Rate %+.2fkm/s", lk.rangeRateKmS);
                SatCore::Pass ps; bool searching = false;
                if (SAT_Utils::geo()) {
                    row(4, "%s", lk.elDeg >= 0 ? "GEO: fixed in sky" : "GEO: below horizon");
                } else if (SAT_Utils::nextPass(ps, searching)) {
                    char d[10];
                    const double t = (double)now();
                    if (t >= ps.aosUnix && t <= ps.losUnix) { satDuration(d, sizeof(d), ps.losUnix - t); row(4, "LOS in %s max %d", d, (int)lround(ps.maxElDeg)); }
                    else                                    { satDuration(d, sizeof(d), ps.aosUnix - t); row(4, "AOS in %s max %d", d, (int)lround(ps.maxElDeg)); }
                } else {
                    row(4, "%s", searching ? "Searching passes..." : "No pass in 36 h");
                }
            }
        } else if (satView == 1) {                           // proximo paso
            SatCore::Pass ps; bool searching = false;
            if (SAT_Utils::geo()) {
                row(1, "Geostationary");
                row(2, "No passes: fixed");
                row(3, "Az %03d El %+3d", have ? (int)lround(lk.azDeg) : 0, have ? (int)lround(lk.elDeg) : 0);
                row(4, "Freqs: v:freq view");
            } else if (SAT_Utils::nextPass(ps, searching)) {
                char a[10], l[10], d[10];
                satHhmm(a, sizeof(a), ps.aosUnix); satHhmm(l, sizeof(l), ps.losUnix);
                satDuration(d, sizeof(d), ps.losUnix - ps.aosUnix);
                row(1, "AOS %s %s", a, SatCore::cardinal(ps.aosAzDeg));
                row(2, "LOS %s %s", l, SatCore::cardinal(ps.losAzDeg));
                row(3, "Max El %d  %s", (int)lround(ps.maxElDeg), d);
                row(4, "UTC  min El %d", (int)Config.sat.minEl);
            } else {
                row(1, "%s", searching ? "Searching passes..." : "No pass in 36 h");
            }
        } else if (satView == 3) {                           // recepcion
            if (!SATRX_Utils::active()) {
                if (b->txCount == 0) { row(1, "No transmitter data"); }
                else {
                    if (satTx >= b->txCount) satTx = 0;
                    const SatCore::Transmitter& t = b->tx[satTx];
                    const char* why = "";
                    const SATRX_Utils::Kind rk = SATRX_Utils::classify(t, &why);
                    row(1, "RX idle  %u/%u", (unsigned)satTx + 1, (unsigned)b->txCount);
                    row(2, "%s %.9s", t.mode, t.desc);
                    if (rk == SATRX_Utils::K_NO) { row(3, "Not receivable:"); row(4, "%s", why); }
                    else                          { row(3, "%s %.4f MHz", SATRX_Utils::kindText(rk), SatCore::txDownCenter(t) / 1e6); row(4, "Menu > Listen"); }
                    if (SATRX_Utils::message()[0]) row(5, "%s", SATRX_Utils::message());
                }
            } else {
                row(1, "%s %.4f MHz", SATRX_Utils::kindText(SATRX_Utils::kind()), SATRX_Utils::rxHz() / 1e6);
                row(2, "Dop %+dHz Fn %+d%s", SATRX_Utils::dopplerHz(), SATRX_Utils::fineHz(), SATRX_Utils::dopplerOk() ? "" : " !");
                if (SATRX_Utils::kind() == SATRX_Utils::K_LORA) { char lt[24]; SATRX_Utils::loraText(lt, sizeof(lt)); row(3, "%ddBm %s", (int)lroundf(SATRX_Utils::rssi()), lt); }
            #ifdef HAS_SA868
                else if (FM_Utils::satUpHz()) {
                    char tn[14];
                    if (FM_Utils::satTone()) snprintf(tn, sizeof(tn), "T%.1f", FM_Utils::satTone() / 10.0);
                    else snprintf(tn, sizeof(tn), "n=%lu", (unsigned long)SATRX_Utils::frames());
                    row(3, "UP %.4f %s", FM_Utils::satUpHz() / 1e6, tn);
                }
                else                          row(3, "No TX here n=%lu", (unsigned long)SATRX_Utils::frames());
            #else
                else                                            row(3, "%ddBm n=%lu", (int)lroundf(SATRX_Utils::rssi()), (unsigned long)SATRX_Utils::frames());
            #endif
                char line[112];
                line[0] = 0;
                if (SATRX_Utils::kind() == SATRX_Utils::K_CW) SATRX_Utils::cwLine(line, 41);
                if (!line[0]) SATRX_Utils::logLine(0, line, sizeof(line));
                if (!line[0]) { row(4, "Listening..."); }
                else {
                    const size_t len = strlen(line);
                    const size_t pages = (len + 39) / 40;                       // 40 caracteres por pantalla: dos filas de 20
                    const size_t off = pages > 1 ? ((millis() / 3000) % pages) * 40 : 0;
                    char r[22];
                    snprintf(r, sizeof(r), "%.20s", line + off);
                    row(4, "%s", r);
                    if (len > off + 20) { snprintf(r, sizeof(r), "%.20s", line + off + 20); row(5, "%s", r); }
                }
            }
        } else if (satView == 4) {                           // emision
            if (b->txCount == 0) { row(1, "No transmitter data"); }
            else {
                if (satTx >= b->txCount) satTx = 0;
                const SatCore::Transmitter& t = b->tx[satTx];
                const char* why = "";
                const SATRX_Utils::Kind uk = SATTX_Utils::classifyUp(t, &why);
                double rx = 0, tx = 0;
                SAT_Utils::doppler(t, rx, tx);
                const int64_t up = SatCore::txUpCenter(t);
                row(1, "TX %u/%u %s %.8s", (unsigned)satTx + 1, (unsigned)b->txCount, t.mode, t.desc);
                if (up) row(2, "UP %.4f %s", up / 1e6, SATRX_Utils::kindText(uk)); else row(2, "UP -");
                if (up && tx > 0) row(3, "TX %.4f %+dHz", tx / 1e6, (int)lround(tx - up)); else row(3, "TX -");
                char st[28];
                if (SATTX_Utils::check(satTx, st, sizeof(st))) row(4, Config.sat.txText.isEmpty() ? "No message (WebUI)" : "Ready: menu > Send");
                else row(4, "%s", st);
                const uint32_t ago = SATTX_Utils::sinceLastS();
                if (ago != 0xFFFFFFFFUL) row(5, "Last %lus ago n=%lu %s", (unsigned long)ago, (unsigned long)SATTX_Utils::count(), SATTX_Utils::message());
                else                     row(5, "%.20s", Config.sat.txText.c_str());
            }
        } else {                                             // frecuencias
            if (b->txCount == 0) { row(1, "No transmitter data"); }
            else {
                if (satTx >= b->txCount) satTx = 0;
                const SatCore::Transmitter& t = b->tx[satTx];
                double rx = 0, tx = 0;
                SAT_Utils::doppler(t, rx, tx);
                row(1, "%u/%u %s %.10s", (unsigned)satTx + 1, (unsigned)b->txCount, t.mode, t.desc);
                const int64_t dn = SatCore::txDownCenter(t), up = SatCore::txUpCenter(t);
                if (dn) { const SATRX_Utils::Kind rk = SATRX_Utils::classify(t); row(2, "DN %.4f %s", dn / 1e6, SATRX_Utils::available() ? SATRX_Utils::kindText(rk) : ""); } else row(2, "DN -");
                if (dn && rx > 0) row(3, "RX %.4f %+dHz", rx / 1e6, (int)lround(rx - dn));
                if (up) row(4, "UP %.4f TX %.4f", up / 1e6, tx > 0 ? tx / 1e6 : 0.0);
                else    row(4, "UP -");
            }
        }
        if (satView >= 3) return;                            // las pantallas de recepcion y emision usan las seis filas
        if (Config.rotor.enabled && satView == 0) {
            float ra, re; bool trk;
            if (ROTOR_Utils::target(ra, re, trk)) row(5, "ROT %s %03d/%02d", ROTOR_Utils::stateText(), (int)lroundf(ra) % 360, (int)lroundf(re));
            else                                  row(5, "ROT %s", ROTOR_Utils::stateText());
            return;
        }
        const double age = SAT_Utils::tleAgeDays();
        row(5, "TLE %.1fd %s%s", age < 0 ? 0.0 : age, SAT_Utils::dataStamp()[0] ? "" : "", satView == 0 ? "v:now" : (satView == 1 ? "v:pass" : "v:freq"));
    }
#else
    void drawSatPage() {}
#endif

    // ======================= AIS =======================
    static void aisAgo(uint32_t ms, char* out, size_t n) {
        const uint32_t s = ms / 1000;
        if (s < 60) snprintf(out, n, "%lus", (unsigned long)s); else if (s < 3600) snprintf(out, n, "%lum", (unsigned long)(s / 60)); else snprintf(out, n, "%luh", (unsigned long)(s / 3600));
    }

    void drawAisPage() {
        int idx[AisCore::Table::CAP];
        const int m = AIS_Utils::sorted(idx, AisCore::Table::CAP);
        char right[12];
        const char ch = AIS_Utils::channel();
        if (ch) snprintf(right, sizeof(right), "%d %c", m, ch); else snprintf(right, sizeof(right), "%d", m);
        header(PAGE_TITLES[PAGE_AIS], right);
        if (m == 0) {
            row(0, "No vessels yet");
            if (Config.operatingMode == MODE_AIS) {
                row(1, "SA868 %s MHz", ch == 'B' ? "162.025" : "161.975");
                #ifdef HAS_SA868
                row(2, "Frames %lu bad %lu", (unsigned long)SA868::aisFrames(), (unsigned long)SA868::aisCrcErrors());
                #endif
                row(3, "Needs a VHF marine");
                row(4, "antenna (2 m works)");
            } else {
                row(1, "Source: %s", AIS_Utils::sourceName());
            }
            return;
        }
        if (aisSel >= m) aisSel = m - 1;
        AisCore::Own o; const bool haveOwn = AIS_Utils::own(o);
        if (aisView == 0) {                                          // lista por distancia (5 filas, la elegida marcada)
            const int first = aisSel >= 5 ? aisSel - 4 : 0;
            for (int r = 0; r < 5 && first + r < m; r++) {
                const AisCore::Vessel* v = AIS_Utils::table().at(idx[first + r]);
                if (!v) continue;
                char name[11];
                if (v->name[0]) snprintf(name, sizeof(name), "%.9s", v->name); else snprintf(name, sizeof(name), "%09lu", (unsigned long)v->mmsi);
                const bool dng = AIS_Utils::dangerous(*v);
                if (v->hasPos && haveOwn) {
                    double nm, brg; AisCore::rangeBearing(o, v->lat, v->lon, nm, brg);
                    row(r, "%c%-9.9s%c%4.1f %03d", first + r == aisSel ? '>' : ' ', name, dng ? '!' : ' ', nm > 99.9 ? 99.9 : nm, (int)lround(brg) % 360);
                } else {
                    row(r, "%c%-9.9s %s", first + r == aisSel ? '>' : ' ', name, v->hasPos ? "pos" : "no pos");
                }
            }
            row(5, "%lu msgs %s", (unsigned long)AIS_Utils::messages(), AIS_Utils::sourceName());
            return;
        }
        const AisCore::Vessel* v = AIS_Utils::table().at(idx[aisSel]);
        if (!v) return;
        const char* sart = AisCore::distressName(v->mmsi);
        row(0, "%.20s", v->name[0] ? v->name : (sart[0] ? sart : "(no name yet)"));
        row(1, "%09lu %c %.8s", (unsigned long)v->mmsi, v->cls, v->call[0] ? v->call : AisCore::shipTypeName(v->shipType));
        if (v->hasPos && haveOwn) {
            double nm, brg; AisCore::rangeBearing(o, v->lat, v->lon, nm, brg);
            row(2, "%.2fnm %03d %s", nm, (int)lround(brg) % 360, cardinal16(brg));
        } else if (v->hasPos) row(2, "%.4f %.4f", v->lat, v->lon);
        else row(2, "No position yet");
        char hd[8] = "";
        if (v->hdg != 511) snprintf(hd, sizeof(hd), " H%03d", v->hdg);
        if (v->hasSog || v->hasCog) row(3, "%.1fkn %03d%s %.6s", v->hasSog ? v->sogKn : 0.0f, v->hasCog ? (int)lround(v->cogDeg) % 360 : 0, hd, AisCore::statusName(v->status));
        else row(3, "%.20s", AisCore::shipTypeName(v->shipType));
        double c, t;
        if (haveOwn && v->hasPos && AisCore::cpa(o, *v, c, t) && t > 0 && t < 600) row(4, "CPA %.2fnm %.0fmin%s", c, t, AIS_Utils::dangerous(*v) ? " !" : "");
        else if (v->dest[0]) row(4, "To %.17s", v->dest);
        else if (v->length) row(4, "%um x %um %.8s", v->length, v->beam, AisCore::shipTypeName(v->shipType));
        char ago[8]; aisAgo(millis() - v->lastMs, ago, sizeof(ago));
        if (v->lastText[0]) row(5, "%.20s", v->lastText);
        else row(5, "%s ago%s%s", ago, v->length ? " " : "", v->length ? (String(v->length) + "m " + AisCore::shipTypeName(v->shipType)).c_str() : "");
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
    bool wsprMsOfDay(uint32_t& ms) { return GPS_Utils::utcMsOfDay(ms); }   // con PPS, alineado con su flanco

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
    bool lrfhssGrid(char out[9]);

    void drawLrfPage() {
        header(PAGE_TITLES[PAGE_LRF], lrfhssOn ? "ON" : "OFF");
        char call[11], grid[9] = "------";
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
            while (gpsSerial.available() > 0) { const char c = gpsSerial.read(); gps.encode(c); GNSS_Sky::feed(c); }
            bool down = cwButtonDown();
            if (!down) armed = true;
            else if (armed) aborted = true;
        };
        wsprProgress = -1;
        snprintf(wsprStatus, sizeof(wsprStatus), "Starting...");
        themeBegin(); drawWsprPage(); pagesFlush();
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
            if (!dark && i % 6 == 0) { themeBegin(); drawWsprPage(); pagesFlush(); }   // la pantalla se redibuja justo tras fijar el tono
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
            while (gpsSerial.available() > 0) { const char c = gpsSerial.read(); gps.encode(c); GNSS_Sky::feed(c); }
            bool down = cwButtonDown();
            if (!down) armed = true;
            else if (armed) aborted = true;
        };
        wsprProgress = -1;
        snprintf(wsprStatus, sizeof(wsprStatus), "Starting...");
        themeBegin(); drawFtPage(); pagesFlush();
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

    // Locator de 4, 6 u 8 caracteres (Config.lrfhss.locatorLen): el fijo de la WebUI (recortado a esa precision) o el del GPS
    bool lrfhssGrid(char out[9]) {
        const size_t want = (Config.lrfhss.locatorLen == 4 || Config.lrfhss.locatorLen == 8) ? (size_t)Config.lrfhss.locatorLen : 6;
        String fixed = Config.lrfhss.locator;
        fixed.trim();
        fixed.toUpperCase();
        if (LRFHSS::gridValid(fixed.c_str())) {                                    // un fijo mas corto que lo pedido se emite tal cual; uno mas largo se recorta
            const size_t len = fixed.length() < want ? fixed.length() : want;
            memcpy(out, fixed.c_str(), len);
            out[len] = '\0';
            return true;
        }
        double lat, lon;
        if (!ownPosition(lat, lon)) return false;
        const char* loc = Utils::getMaidenheadLocator(lat, lon, (uint8_t)want);
        char g[9];
        for (size_t i = 0; i < want; i++) g[i] = (char)toupper(loc[i]);
        g[want] = '\0';
        if (!LRFHSS::gridValid(g)) return false;
        memcpy(out, g, want + 1);
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
        char call[11], grid[9];
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
                themeBegin();
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
                themeBegin();
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
        if (cwPin(Config.cw.keyPin) >= 0) pinMode(Config.cw.keyPin, INPUT_PULLUP);
        if (cwPin(Config.cw.dahPin) >= 0) pinMode(Config.cw.dahPin, INPUT_PULLUP);
        cwPinsReady = true;
    }

    void cwReadPaddles(bool& dit, bool& dah) {
        dit = cwPin(Config.cw.keyPin) >= 0 && digitalRead(Config.cw.keyPin) == LOW;
        dah = cwPin(Config.cw.dahPin) >= 0 && digitalRead(Config.cw.dahPin) == LOW;
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
                NOTIFICATION_Utils::lightMorse(keyed);
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
        NOTIFICATION_Utils::lightMorse(false);
        if (rf) LoRa_Utils::cwEnd();
        cwPending[0] = '\0';
        displayTime = millis();
        cwDrawPage();
    }

    // Manipulador conectado al movil (app APRS LoRa+, EA7JRS): el movil manda pulsar / soltar con su marca de tiempo y aqui se
    // reproducen con un retardo fijo (BLE_Control::keyDelayMs) para absorber las variaciones del enlace. Misma salida que
    // cwKeySession (perfil de Telegrafia, tono local, decodificador en pantalla). Seguridades: portadora como maximo 10 s
    // seguidos, se suelta si cae el enlace o si el movil deja de mandar, y el boton corta la sesion.
    bool headingForRotor(float& hdg) {
        static float lastCourse = 0;
        static uint32_t lastCourseMs = 0;
        const uint32_t nowMs = millis();
        const bool gpsGood = gpsIsActive && gps.course.isValid() && gps.speed.isValid() && gps.location.age() < 3000 &&
                             gps.satellites.isValid() && gps.satellites.value() >= 4;
        if (gpsGood && GPS_Utils::getSpeedKmph() >= 5.0f) {                                   // en marcha el curso GPS es fiable
            lastCourse = GPS_Utils::getCourseDeg();
            lastCourseMs = nowMs ? nowMs : 1;
            hdg = lastCourse;
            return true;
        }
        if (MAG_Utils::isHeadingValid()) {                                                    // parado: magnetometro (norte verdadero)
            const float magRaw = wrap360(MAG_Utils::heading() - MAG_Utils::alignmentOffset());
            hdg = MAG_Utils::isAligned() ? MAG_Utils::heading() : wrap360(magRaw + currentDeclination(nullptr));
            return true;
        }
        if (lastCourseMs && nowMs - lastCourseMs < 30000UL) { hdg = lastCourse; return true; }
        return false;
    }

    // Posicion de una estacion para el rotor: la ultima recibida o, si se mueve y esta activada la estimacion, donde estara ahora
    // segun la velocidad y el rumbo de esa posicion (Copyright (C) 2026 Manuel - EA7JRS). true si es estimada
    static bool rotorStationPos(const Station* s, double& lat, double& lon) {
        lat = s->lat; lon = s->lon;
        if (!Config.rotor.predict || s->posMs == 0) return false;
        const double dt = (double)(millis() - s->posMs) / 1000.0;
        return RotorFollow::predict(s->lat, s->lon, s->speed, s->course, dt, Config.rotor.predictMaxS, 3.0, lat, lon);
    }

    bool aprsTarget(double& lat, double& lon, double& altM, char* name, size_t nameSize) {
        const Station* s = compassStation();
        if (!s || !s->hasPosition) return false;
        const bool est = rotorStationPos(s, lat, lon);
        altM = s->altitude;
        snprintf(name, nameSize, est ? "%s~" : "%s", s->call);      // "~" = posicion estimada
        return true;
    }

    // Rotor, fuente 4 (Copyright (C) 2026 Manuel - EA7JRS): la estacion mas reciente con posicion (APRS LoRa o FSK: las dos entran
    // en la misma lista) que pasa el filtro de indicativos y no es mas antigua que el limite de la WebUI
    bool lastHeardTarget(double& lat, double& lon, double& altM, char* name, size_t nameSize) {
        const uint32_t now = millis();
        const uint32_t maxAge = (uint32_t)Config.rotor.followMaxMin * 60000UL;
        for (uint8_t i = 0; i < heard.count(); i++) {
            const Station* s = heard.byRecent(i);
            if (!s) break;
            if (now - s->lastMs > maxAge) break;                                    // la lista va de la mas reciente a la mas antigua
            if (!s->hasPosition || !RotorFollow::callMatches(s->call, Config.rotor.followFilter.c_str())) continue;
            const bool est = rotorStationPos(s, lat, lon);
            altM = s->altitude;
            snprintf(name, nameSize, est ? "%s~" : "%s", s->call);
            return true;
        }
        return false;
    }

    void cwRemoteSession() {
        const bool rf       = Config.cw.keyTx && LoRa_Utils::cwAvailable();
        const bool tone     = Config.cw.sidetone && Config.notification.buzzerActive;
        const uint8_t wpm   = (uint8_t)constrain(Config.cw.wpm, (int)CWMorse::WPM_MIN, (int)CWMorse::WPM_MAX);
        const uint32_t MAX_KEY_DOWN_MS = 10000, IDLE_END_MS = 2500;
        if (rf) LoRa_Utils::cwSelect(false);
        CWMorse::Decoder decoder;
        decoder.begin(wpm, true);                   // se adapta a la velocidad real del operador
        bool keyed = false, armed = !cwButtonDown(), have = false, synced = false;
        BLE_Control::KeyEvent event = { 0, 0, false };
        int32_t offset = 0;                         // instante del tracker = t del movil + offset
        uint32_t lastActivity = millis(), keyedSince = 0;
        cwPending[0] = '\0';
        displayToggle(true);
        displayTime = millis();
        auto setKey = [&](bool on, uint32_t now) {
            if (on == keyed) return;
            keyed = on;
            if (rf) LoRa_Utils::cwKey(keyed);
            if (tone) NOTIFICATION_Utils::sidetone(keyed);
            if (keyed) { decoder.keyDown(now); keyedSince = now; } else decoder.keyUp(now);
            snprintf(cwPending, sizeof(cwPending), "%s", decoder.pending());
        };
        for (;;) {
            uint32_t now = millis();
            if (!have) have = BLE_Control::takeKeyEvent(event);
            if (have) {
                // primer evento, o vuelta tras una pausa larga: se fija el desfase con el reloj del movil
                if (!synced || (!keyed && (int32_t)(event.arrivalMs - lastActivity) > (int32_t)IDLE_END_MS)) {
                    offset = (int32_t)(event.arrivalMs + BLE_Control::keyDelayMs() - event.t);
                    synced = true;
                }
                const uint32_t due = event.t + (uint32_t)offset;
                if ((int32_t)(now - due) >= 0) {        // llego tarde (mas que el retardo): se ejecuta ya
                    setKey(event.down, now);
                    have = false;
                    lastActivity = now;
                }
            }
            if (keyed && (now - keyedSince > MAX_KEY_DOWN_MS || !BLE_Control::linkUp())) setKey(false, now);
            char c = decoder.tick(now);
            if (c) {
                cwAppend(c);
                snprintf(cwPending, sizeof(cwPending), "%s", decoder.pending());
                if (!keyed && !have) cwDrawPage();
            }
            if (keyed) lastActivity = now;
            if (cwButtonDown()) { if (armed) break; } else armed = true;
            if (!have && !keyed && now - lastActivity > IDLE_END_MS && decoder.pending()[0] == '\0' && !BLE_Control::keyEventWaiting()) break;
            if (!BLE_Control::linkUp() && !have && !keyed) break;
            delay(1);
        }
        if (keyed) setKey(false, millis());
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
        bool sqOpen = false, sqChanged = false;                    // squelch con histeresis de 3 dB
        uint32_t started = millis(), lastDraw = 0, pressAt = 0;   // pressAt: inicio de la pulsacion en curso (0 = suelto)
        cwRxActive = true;
        cwRxWpm = wpm;
        cwPending[0] = '\0';
        BLE_Control::cwRxEvent(true, 0, -150.0f, 0.0f, wpm);
        for (;;) {
            uint32_t now = millis();
            float level = LoRa_Utils::cwRxLevel();
            bool k = env.update(level);
            const int sq = Config.cw.squelchDbm;
            if (sq != 0) {                                          // squelch: por debajo del nivel elegido no hay senal
                if (sqOpen) { if (level < (float)(sq - 3)) sqOpen = false; }
                else if (level >= (float)sq) sqOpen = true;
                if (!sqOpen) k = false;
            }
            if (k != keyed) {
                keyed = k;
                if (keyed) decoder.keyDown(now); else decoder.keyUp(now);
                if (tone) NOTIFICATION_Utils::sidetone(keyed);
                NOTIFICATION_Utils::lightRx(keyed);                    // LED verde (Rx) = senal CW recibida
                snprintf(cwPending, sizeof(cwPending), "%s", decoder.pending());
            }
            char c = decoder.tick(now);
            if (c) {
                cwAppend(c);
                snprintf(cwPending, sizeof(cwPending), "%s", decoder.pending());
                dirty = true;
                BLE_Control::cwRxEvent(true, c, env.level(), env.valid() ? env.peak() - env.noise() : 0.0f, decoder.wpm());   // letra para la app
                if (c == ' ') cwBeaconCheck();                         // baliza con locator: objetivo del rotor (fuente 5)
            }
            if (!keyed) rotorPoll();                                   // el rotor sigue funcionando durante la escucha (se recalcula cada 1 s)
            // solo se redibuja con la portadora apagada (el dibujo tarda ~25 ms): tras un espacio entre palabras o cada segundo
            if (!keyed && ((dirty && c == ' ') || now - lastDraw > 1000)) {
                cwRxDbm = env.level();
                cwRxSnr = env.valid() ? env.peak() - env.noise() : 0.0f;
                cwRxWpm = decoder.wpm();
                cwDrawPage();
                BLE_Control::cwRxEvent(true, 0, cwRxDbm, cwRxSnr, cwRxWpm);       // nivel para la app, una vez por segundo
                lastDraw = millis();
                dirty = false;
                displayTime = lastDraw;
            }
            // Boton: pulsacion corta = siguiente squelch (OFF, -120 ... -70 dBm); pulsacion larga (0,7 s) = salir
            if (cwButtonDown()) {
                if (armed) {
                    if (pressAt == 0) pressAt = now;
                    else if (now - pressAt >= 700) break;
                }
            } else {
                if (pressAt != 0 && armed) {
                    Config.cw.squelchDbm = cwNextSquelch(Config.cw.squelchDbm);
                    sqChanged = true;
                    sqOpen = false;
                    lastDraw = 0;                                    // se redibuja en cuanto la portadora este apagada
                }
                pressAt = 0;
                armed = true;
            }
            if (now - started > 600000UL) break;                     // 10 minutos como maximo
            if (BLE_Control::cwRxStopRequested()) break;             // la app la para
            delay(1);
        }
        cwBeaconCheck();                                             // la ultima palabra puede no llevar espacio detras
        if (tone) NOTIFICATION_Utils::sidetone(false);
        NOTIFICATION_Utils::lightRx(false);
        LoRa_Utils::cwRxEnd();
        while (cwButtonDown()) delay(5);                             // espera a que se suelte el boton: el menu no debe verlo como una pulsacion
        if (sqChanged) Config.writeFile();                           // el squelch elegido se guarda al salir
        cwRxActive = false;
        cwPending[0] = '\0';
        BLE_Control::cwRxEvent(false, 0, -150.0f, 0.0f, 0);
        displayTime = millis();
        cwDrawPage();
        showToast("CW RX stopped");
    }

    // ---- Trainer (metodo Koch): solo zumbador / manipulador, sin emitir RF. Cualquier pulsacion del boton sale ----
    void cwTrainerScreen(const char* right, const char* big, uint8_t bigSize, const char* line1, const char* line2) {
        themeBegin();
        header("TRAINER", right);
        pagesInk(INK_ACCENT);
        if (big && big[0]) pagesText((PW - 6 * bigSize * (int)strlen(big)) / 2, 18, big, bigSize, false);
        pagesInk(INK_TEXT);
        if (line1) text((PW - 6 * (int)strlen(line1)) / 2, 44, line1);
        if (line2) text((PW - 6 * (int)strlen(line2)) / 2, 54, line2);
        pagesFlush();
    }

    char cwRandomKoch(uint8_t level) {
        return CWMorse::KOCH_ORDER[random(level)];
    }

    void cwTrainKeyHook(void*, bool on) {
        NOTIFICATION_Utils::sidetone(on);
        NOTIFICATION_Utils::lightMorse(on);
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
        NOTIFICATION_Utils::lightMorse(false);
        displayTime = millis();
        char message[24];
        snprintf(message, sizeof(message), "Trainer: %u groups", (unsigned)groups);
        showToast(message);
    }

    // Manipular: se muestra un caracter, se teclea con el manipulador y se corrige
    void cwTrainerKeying() {
        if (cwPin(Config.cw.keyPin) < 0) { showToast("Set key GPIO (WebUI)"); return; }
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
                    NOTIFICATION_Utils::lightMorse(keyed);
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
            NOTIFICATION_Utils::lightMorse(false);
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
        const int pin = cwPin(Config.cw.keypadPin);
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
        if (page != PAGE_CW || submenuOpen || cwPin(Config.cw.keyPin) < 0 || sosState != SOS_IDLE || alarmShown) return;
        cwInitPins();
        bool dit, dah;
        cwReadPaddles(dit, dah);
        if (dit || dah) cwKeySession();
    }

    void show() {
        if (!pageVisible(page)) { page = modeHomePage(); if (!pageVisible(page)) page = PAGE_MAIN; changedPage(); }
        themeBegin();
        memset(mirrorRows, 0, sizeof(mirrorRows));
        mirrorTitle[0] = '\0';
        mirrorRight[0] = '\0';
        mirrorPage = page;
        mirrorMs = millis();
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
            case PAGE_SAT:     drawSatPage();   break;
            case PAGE_ROT:     drawRotPage();   break;
            case PAGE_LAST:    drawLast();      break;
            case PAGE_LIST:    drawList();      break;
            case PAGE_GPS:     drawGps();       break;
            case PAGE_COMPASS: drawCompass();   break;
            case PAGE_ALT:     drawAltimeter(); break;
            case PAGE_WX:      drawWx();        break;
            case PAGE_AIS:     drawAisPage();   break;
            case PAGE_CLOCK:   drawClock();     break;
            case PAGE_LINK:    drawLink();      break;
            case PAGE_BOARD:   drawBoard();     break;
            case PAGE_TEAM:    drawTeamPage();  break;
            #ifdef HAS_SA868
            case PAGE_FM:      drawFmPage();    break;
            #endif
            case PAGE_BCAST:   drawBcastPage(); break;
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
        const int remoteMode = BLE_Control::takeBootMode();                                // modo pedido desde la app por BLE: sin menu (EA7JRS)
        if (remoteMode >= 0) {
            Config.operatingMode = remoteMode;
        } else {
        #ifdef BUTTON_PIN
            struct Option { uint8_t mode; const char* label; };
            Option options[16];
            uint8_t n = 0;
            options[n++] = Option{ MODE_APRS_LORA, APRS_MAIN_MODE_NAME };
            if (LoRa_Utils::fskAvailable()) options[n++] = Option{ MODE_APRS_FSK, "APRS FSK 9600" };
            if (LoRa_Utils::m17Available()) options[n++] = Option{ MODE_M17, "M17" };
            if (LoRa_Utils::fmAvailable())  options[n++] = Option{ MODE_FM, "FM voice" };
            if (LoRa_Utils::cwAvailable())  options[n++] = Option{ MODE_MORSE, "MORSE / CW" };
            if (LoRa_Utils::wsprAvailable()) options[n++] = Option{ MODE_WSPR, "WSPR beacon" };
            if (LoRa_Utils::wsprAvailable()) options[n++] = Option{ MODE_FT8, "FT8 beacon" };
            if (LoRa_Utils::wsprAvailable()) options[n++] = Option{ MODE_FT4, "FT4 beacon" };
            if (LoRa_Utils::wsprAvailable()) options[n++] = Option{ MODE_FST4W, "FST4W beacon" };
            if (Config.lrfhss.enabled && LoRa_Utils::lrfhssAvailable()) options[n++] = Option{ MODE_LRFHSS, "LR-FHSS beacon" };
            if (LoRa_Utils::dapnetAvailable()) options[n++] = Option{ MODE_DAPNET, "DAPNET pager" };
            if (LoRa_Utils::aisAvailable()) options[n++] = Option{ MODE_AIS, "AIS receiver" };
            #ifdef HAS_SAT
            options[n++] = Option{ MODE_SAT, "Satellites" };
            #endif
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
                    themeBegin();
                    pagesZone(PZ_TOP);
                    pagesInk(INK_HEAD);
                    pagesFillRect(0, 0, PW, 9, true);
                    pagesText((PW - 6 * 11) / 2, 1, "SELECT MODE", 1, true);
                    pagesInk(INK_TEXT);
                    pagesZone(PZ_BODY);
                    // Lista con ventana de 4 filas (11 px de paso) que sigue a la seleccion: no invade la linea inferior (y = 56) ni el temporizador
                    const uint8_t visibleRows = 4;
                    const uint8_t first = sel >= visibleRows ? (uint8_t)(sel - visibleRows + 1) : 0;
                    for (uint8_t k = 0; k < visibleRows && (uint8_t)(first + k) < n; k++) {
                        const uint8_t i = (uint8_t)(first + k);
                        const int top = 11 + k * 11;
                        if (i == sel) {
                            pagesInk(INK_SEL);
                            pagesFillRect(8, top, 108 + XR, 10, true);
                            pagesText(12, top + 1, options[i].label, 1, true);
                            pagesInk(INK_TEXT);
                        } else {
                            pagesText(12, top + 1, options[i].label, 1, false);
                        }
                    }
                    pagesInk(INK_FRAME);
                    if (first > 0)                      pagesFillTriangle(119 + XR, 16, 125 + XR, 16, 122 + XR, 12, true);       // hay opciones por encima
                    if (first + visibleRows < n)        pagesFillTriangle(119 + XR, 46, 125 + XR, 46, 122 + XR, 50, true);       // y por debajo
                    pagesInk(INK_MUTED);
                    pagesText(2, 56, "Click:next Hold:OK", 1, false);
                    char left[16];
                    snprintf(left, sizeof(left), "%ds", (int)(seconds > 99 ? 99 : seconds));
                    pagesInk(INK_ACCENT);
                    pagesText(PW - 2 - 6 * (int)strlen(left), 56, left, 1, false);
                    pagesInk(INK_TEXT);
                    pagesFlush();
                }
                delay(5);
            }
            if (chosen) while (digitalRead(BUTTON_PIN) == LOW) delay(10);                  // espera a soltar para no generar otra pulsacion
            // MORSE solo se activa si se acepta a mano (pulsacion larga) y no se guarda; sin pulsar nada se arranca el ultimo modo APRS
            uint8_t mode = chosen ? options[sel].mode : (uint8_t)Config.savedMode;
            Config.operatingMode = mode;
            if (mode != MODE_MORSE && mode != MODE_WSPR && mode != MODE_LRFHSS && mode != MODE_DAPNET && mode != MODE_SAT && mode != MODE_AIS && mode != MODE_FT8 && mode != MODE_FT4 && mode != MODE_FST4W && mode != Config.savedMode) {
                Config.savedMode = mode;
                Config.writeFile();                                                        // el ultimo modo APRS elegido pasa a ser el predeterminado
            }
        #endif
        }
        if (Config.operatingMode != MODE_MORSE && Config.operatingMode != MODE_WSPR && Config.operatingMode != MODE_LRFHSS && Config.operatingMode != MODE_DAPNET && Config.operatingMode != MODE_SAT && Config.operatingMode != MODE_FT8 && Config.operatingMode != MODE_FT4 && Config.operatingMode != MODE_FST4W && Config.operatingMode != MODE_APRS_FSK && !(Config.operatingMode == MODE_M17 && LoRa_Utils::m17Available()) && !(Config.operatingMode == MODE_FM && LoRa_Utils::fmAvailable()) && !(Config.operatingMode == MODE_AIS && LoRa_Utils::aisAvailable())) Config.operatingMode = MODE_APRS_LORA;
        if (Config.operatingMode == MODE_WSPR || ftMode()) { beaconManualMode = true; page = PAGE_WSPR; wsprOn = true; }
        if (Config.operatingMode == MODE_LRFHSS) { beaconManualMode = true; page = PAGE_LRF; lrfhssOn = true; }
        if (Config.operatingMode == MODE_DAPNET) { beaconManualMode = true; page = PAGE_DAP; DAPNET_Utils::reset(); }       // solo recepcion: sin balizas APRS
        if (Config.operatingMode == MODE_AIS) { beaconManualMode = true; page = PAGE_AIS; }                    // receptor AIS: no emite
        #ifndef HAS_SAT
        if (Config.operatingMode == MODE_SAT) Config.operatingMode = MODE_APRS_LORA;                          // modo no disponible en esta placa
        #endif
        if (Config.operatingMode == MODE_SAT) { beaconManualMode = true; page = PAGE_SAT; SAT_Utils::begin(); SATTX_Utils::begin(); }               // solo prediccion: sin balizas APRS
        #ifdef HAS_SAT
        ROTOR_Utils::begin();
        #endif
        if (Config.operatingMode == MODE_MORSE) { beaconManualMode = true; page = PAGE_CW; }                   // en MORSE no hay balizas APRS automaticas
        #ifdef HAS_SA868
        if (Config.operatingMode == MODE_FM) { page = PAGE_FM; beaconManualMode = !Config.fm.aprsBeacon; }     // FM: emisora de voz; balizas APRS si se piden
        #endif
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Main", "Mode: %s",
                   Config.operatingMode == MODE_WSPR ? "WSPR" : (Config.operatingMode == MODE_FT8 ? "FT8" : Config.operatingMode == MODE_FT4 ? "FT4" : Config.operatingMode == MODE_FST4W ? "FST4W" : Config.operatingMode == MODE_DAPNET ? "DAPNET" : Config.operatingMode == MODE_AIS ? "AIS" : Config.operatingMode == MODE_SAT ? "SAT" : Config.operatingMode == MODE_LRFHSS ? "LR-FHSS" : Config.operatingMode == MODE_MORSE ? "MORSE" : (Config.operatingMode == MODE_APRS_FSK ? "APRS FSK" : Config.operatingMode == MODE_M17 ? "M17" : Config.operatingMode == MODE_FM ? "FM voice" : APRS_MAIN_MODE_NAME)));
        return (uint8_t)Config.operatingMode;
    }

    void showSplash(const String& version, const String& callsign) {
        pagesBegin();
        // arriba a la izquierda el indicativo del autor de esta version (APRS LoRa+); a la derecha la version (solo numeros)
        // y justo debajo el indicativo configurado, sin SSID
        String bare = callsign;
        int dash = bare.indexOf('-');
        if (dash > 0) bare = bare.substring(0, dash);
        if (pagesHasColor()) pagesSetLight(false);     // la bienvenida siempre sobre el fondo oscuro del logo (como la de la app)
        #ifdef HAS_TFT
        if (Config.display.splash == 1) {               // logo de la app a todo color: pin a la izquierda, nombre a la derecha (Manuel - EA7JRS)
            pagesBegin();
            pagesZone(PZ_TOP);
            pagesImage565(4, 7, LOGO_APP_ICON_W, LOGO_APP_ICON_H, LOGO_APP_ICON);
            pagesImage565(68, 5, LOGO_APP_TEXT_W, LOGO_APP_TEXT_H, LOGO_APP_TEXT);
            pagesInk(INK_MUTED);
            pagesText(70, 60, "EA7JRS", 1, false);
            pagesText(PW - 2 - 6 * (int)version.length(), 60, version.c_str(), 1, false);
            pagesInk(INK_ACCENT);
            pagesText(70, 70, bare.c_str(), 1, false);
            pagesInk(INK_TEXT);
            pagesFlush();
            return;
        }
        #endif
        pagesBegin();
        pagesZone(PZ_TOP);
        pagesInk(INK_FRAME);
        pagesText(2, 1, "EA7JRS", 1, false);
        pagesInk(INK_MUTED);
        pagesText(PW - 2 - 6 * (int)version.length(), 1, version.c_str(), 1, false);
        pagesInk(INK_ACCENT);
        pagesText(PW - 2 - 6 * (int)bare.length(), 10, bare.c_str(), 1, false);
        // logo APRS LoRa+ (pin con L+ y ondas) centrado, y el nombre debajo (en color: "APRS" en blanco y "LoRa+" en verde)
        const int extra = PAGES_H - 64;                 // pantalla mas alta: el logo y el nombre bajan
        pagesBitmap2((PW - LOGO_SPLASH_W) / 2, 12 + extra * 5 / 8, LOGO_SPLASH_W, LOGO_SPLASH_H, LOGO_SPLASH);
        pagesInk(INK_TEXT);
        pagesText((PW - 6 * 10) / 2, 52 + extra * 5 / 8, "APRS ", 1, false);
        pagesInk(INK_GOOD);
        pagesText((PW - 6 * 10) / 2 + 30, 52 + extra * 5 / 8, "LoRa+", 1, false);
        pagesInk(INK_TEXT);
        pagesFlush();
    }

    // Pantalla del punto de acceso WiFi de configuracion (WEB-CONF), con el formato de las paginas APRS (EA7JRS)
    void showWifiAp(const char* ssid, const char* password, const char* ip, int clients, int idleSecsLeft) {
        themeBegin();
        rowLimit = COLS;
        header("WIFI AP", "WEB-CONF");
        row(0, "Web configuration");
        row(1, "SSID %.15s", ssid);
        row(2, "Pass %.15s", password);
        row(3, "IP   %s", ip);
        row(4, "Clients %d  Hold: exit", clients);      // mantener el boton 2 s: sale del punto de acceso
        if (clients > 0 || idleSecsLeft < 0) row(5, "In use");
        else                                 row(5, "Closes in %d:%02d", idleSecsLeft / 60, idleSecsLeft % 60);
        pagesFlush();
    }

    static void changedPage() {
        lastIndex = 0;
        lastView = 0;
        lastCall[0] = '\0';
        listBrowse = false;
        teamMode = TMODE_LIST;
        boardView = 0;
        gpsView = 0;
        clockView = 0;
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

    // GROUP: en la lista con cursor las pulsaciones mueven el cursor; en el radar cambian la escala (0 = automatica)
    static bool moveTeamCursor(int direction) {
        if (page != PAGE_TEAM || teamMode == TMODE_LIST) return false;
        if (teamMode == TMODE_BROWSE) {
            if (teamCount > 0) teamCursor = (uint8_t)((teamCursor + teamCount + direction) % teamCount);
        } else {
            const uint8_t steps = TEAM_SCALE_COUNT + 1;
            teamScale = (uint8_t)((teamScale + steps + direction) % steps);
        }
        return true;
    }

    // SYSTEM: 1P pasa a BATTERY (placas con bateria) y a LINK (con conexiones activas), y de ahi a la pagina siguiente; 2P vuelve
    static bool moveBoardView(int direction) {
        if (page != PAGE_BOARD) return false;
        uint8_t seq[3], n = 0;
        seq[n++] = 0;
        if (batteryViewAvailable()) seq[n++] = 2;
        if (linkAvailable()) seq[n++] = 1;
        int at = 0;
        for (uint8_t i = 0; i < n; i++) if (seq[i] == boardView) at = i;
        const int to = at + direction;
        if (to < 0 || to >= n) return false;
        boardView = seq[to];
        return true;
    }

    // GPS: 1P pasa a la vista GNSS y de ahi a la pagina siguiente; 2P vuelve (EA7JRS)
    static bool moveGpsView(int direction) {
        if (page != PAGE_GPS) return false;
        if (direction > 0 && gpsView == 0) { gpsView = 1; return true; }
        if (direction < 0 && gpsView == 1) { gpsView = 0; return true; }
        return false;
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
        if (moveMessageCursor(+1) || moveListCursor(+1) || moveTeamCursor(+1) || moveBoardView(+1) || moveGpsView(+1) || moveClockView(+1)) return;
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
        if (moveMessageCursor(-1) || moveListCursor(-1) || moveTeamCursor(-1) || moveBoardView(-1) || moveGpsView(-1) || moveClockView(-1)) return;
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
        if (page == PAGE_TEAM && teamMode != TMODE_LIST) {     // cursor o radar -> lista
            teamMode = TMODE_LIST;
            return true;
        }
        if (page == PAGE_BOARD && boardView != 0) {            // vista LINK o BATTERY -> SYSTEM
            boardView = 0;
            return true;
        }
        if (page == PAGE_GPS && gpsView != 0) {                // vista GNSS -> GPS
            gpsView = 0;
            return true;
        }
        if (page == PAGE_CLOCK && clockView != 0) {            // vista SUN o MOON -> CLOCK
            clockView = 0;
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

    // ---- GROUP: acciones sobre el miembro marcado o sobre todos los que estan en rango (Copyright (C) 2026 Manuel - EA7JRS) ----
    static const char* teamAddress(const TeamSlot& slot) {
        return slot.last[0] ? slot.last : slot.call;                // con el SSID con el que se oyo; si no, el indicativo configurado
    }

    static void teamGoTo() {
        if (teamCursor >= teamCount) return;
        const TeamSlot& slot = teamSlots[teamCursor];
        if (!slot.seen || !slot.hasPos) { showToast("No position yet"); return; }
        const int index = heard.indexOf(slot.last);
        goToStation(index >= 0 ? heard.byRecent((uint8_t)index) : nullptr);
    }

    // Una vez enviado a los miembros indicados, avisa de cuantos mensajes se han puesto en cola
    static void teamSendTo(const char* body, bool one) {
        uint8_t queued = 0;
        for (uint8_t i = 0; i < teamCount; i++) {
            const TeamSlot& slot = teamSlots[i];
            if (one ? (i != teamCursor) : (slot.state != TS_IN)) continue;         // a todos: solo a los que estan en rango (como "SEND to ALL" de Yaesu)
            const int ackId = MSG_Utils::addToOutputBuffer(1, String(teamAddress(slot)), String(body));
            if (ackId > 0) { onMessageSent(teamAddress(slot), body, (uint16_t)ackId); queued++; }
        }
        char toast[26];
        if (queued == 0) snprintf(toast, sizeof(toast), "%s", one ? "Already queued" : "None in range");
        else             snprintf(toast, sizeof(toast), "Queued x%u: %.10s", (unsigned)queued, body);
        showToast(toast);
    }

    static void teamPing() {
        uint8_t queued = 0;
        const bool one = teamMode == TMODE_BROWSE;
        for (uint8_t i = 0; i < teamCount; i++) {
            if (one && i != teamCursor) continue;
            MSG_Utils::addToOutputBuffer(0, String(teamAddress(teamSlots[i])), "ping");
            queued++;
        }
        char toast[26];
        snprintf(toast, sizeof(toast), "Ping x%u", (unsigned)queued);
        showToast(toast);
    }

    static int execute(Command command, bool remote);

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
        return execute(items[submenuSel < count ? submenuSel : 0].cmd, false);
    }

    // Ejecuta un comando de menu. remote = true: viene de BLE (act), sin navegar submenus
    static int execute(Command command, bool remote) {
        (void)remote;
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
        if (command == C_MSG_QUICK || command == C_TEAM_QUICK) { // segundo nivel: respuestas rapidas
            submenuId = SM_QUICK;
            submenuSel = 0;
            submenuMs = millis();
            return PAGES_CMD_NONE;
        }
        submenuOpen = false;
        submenuId = SM_PAGE;
        if (command >= C_QUICK0 && command <= C_QUICK7) {
            if (page == PAGE_TEAM) teamSendTo(QUICK_TEXT[command - C_QUICK0], teamMode == TMODE_BROWSE);
            else                   sendQuickReply((uint8_t)(command - C_QUICK0));
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
            case C_BC_SEEKUP:   BCAST::seek(1); break;
            case C_BC_SEEKDN:   BCAST::seek(-1); break;
            case C_BC_VOLUP:    BCAST::setVolume(BCAST::volume() + 1); break;
            case C_BC_VOLDN:    BCAST::setVolume(BCAST::volume() - 1); break;
            case C_BC_MUTE:     BCAST::setMute(!BCAST::muted()); break;
            case C_BC_BAND: {
                uint8_t b = BCAST::band();
                for (int k = 1; k <= BCAST::BAND_COUNT; k++) { const uint8_t nb = (uint8_t)((b + k) % BCAST::BAND_COUNT); if (BCAST::bandAvailable(nb)) { b = nb; break; } }
                if (b == BCAST::band()) showToast("FM only (Si4735: AM)"); else BCAST::setBand(b);
                break;
            }
            case C_BC_MONO:     BCAST::setMono(!Config.bcast.mono); break;
            case C_BC_STORE: {
                int slot = BCAST::presetIndexNow();
                for (int k = 0; slot < 0 && k < BcastConfig::PRESETS; k++) if (Config.bcast.presets[k] <= 0) slot = k;
                if (slot < 0) { showToast("Presets full"); break; }
                BCAST::presetStore(slot);
                char t[20]; snprintf(t, sizeof(t), "Preset %d stored", slot + 1); showToast(t);
                break;
            }
            case C_BC_PRESET:   if (BCAST::presetNext(1) < 0) showToast("No presets"); break;
            case C_BC_POWER:    BCAST::power(!BCAST::isOn()); break;
            #ifdef HAS_SA868
            case C_FM_VM:       FM_Utils::toggleVfoMem(); break;
            case C_FM_EDIT: {
                FM_Utils::setEdit((uint8_t)((FM_Utils::edit() + 1) % FM_Utils::ED_COUNT));
                static const char* const E[FM_Utils::ED_COUNT] = { "Dial: frequency", "Dial: tone", "Dial: offset", "Dial: squelch" };
                showToast(E[FM_Utils::edit()]);
                break;
            }
            case C_FM_SHIFT:    FM_Utils::cycleShift(); break;
            case C_FM_TONEMODE: FM_Utils::cycleToneMode(); break;
            case C_FM_STEP:     FM_Utils::cycleStep(); break;
            case C_FM_POWER:    FM_Utils::togglePower(); break;
            case C_FM_WIDE:     FM_Utils::toggleWide(); break;
            case C_FM_SQLAUTO:  FM_Utils::toggleAutoSquelch(); break;
            case C_FM_NR:
                if (!SA868::fmSpeakerFromEsp() && SA868::revision() != 21) { showToast("NR: Rev2.1 only"); break; }
                FM_Utils::cycleNoiseReduction();
                break;
            case C_FM_SCAN:     if (FM_Utils::scanning()) FM_Utils::scanStop(); else FM_Utils::scanStart(); break;
            case C_FM_STORE: {
                int slot = FM_Utils::memMode() ? FM_Utils::memIndex() : -1;
                for (int k = 0; slot < 0 && k < FmConfig::MEMORIES; k++) if (!FM_Utils::memUsed(k)) slot = k;
                if (slot < 0) { showToast("Memories full"); break; }
                FM_Utils::store(slot, nullptr);
                break;
            }
            case C_FM_ARTS:     FM_Utils::artsToggle(); break;
            case C_FM_EAI:      if (FM_Utils::eaiOn()) FM_Utils::eaiStop(); else FM_Utils::eaiStart(); break;
            case C_FM_APRS:
                FM_Utils::toggleAprsBeacon();
                beaconManualMode = !Config.fm.aprsBeacon;
                break;
            case C_RADIO_TUNE:  radioTuneCarrier(); break;
            #ifdef HAS_SAT
            case C_FM_SAT: {                                            // el VFO sigue al satelite elegido (o deja de hacerlo)
                if (FM_Utils::satOn()) { FM_Utils::satStop(); break; }
                if (!SAT_Utils::hasData()) SAT_Utils::begin();
                const SatCore::Block* sb = SAT_Utils::block();
                if (!sb) { showToast("No satellite data"); break; }
                int t = Config.fm.satTx < sb->txCount && FM_Utils::satUsable(Config.fm.satTx) ? Config.fm.satTx : -1;
                for (int k = 0; t < 0 && k < sb->txCount; k++) if (FM_Utils::satUsable(k)) t = k;
                if (t < 0) { showToast(SA868::uhf() ? "Sat not on 70cm" : "Sat not on 2m"); break; }
                if (!FM_Utils::satStart(t)) showToast(FM_Utils::notice());
                break;
            }
            case C_FM_SATTX:   FM_Utils::satNextTx(1); break;
            case C_FM_SATNEXT: {                                        // siguiente satelite de la lista (categoria del modo SAT: favoritos, esta radio...)
                if (!SAT_Utils::hasData()) SAT_Utils::begin();
                const bool was = FM_Utils::satOn();
                const int n = SAT_Utils::count();
                int i = SAT_Utils::selected();
                for (int k = 0; k < n; k++) {
                    i = SAT_Utils::step(1);
                    if (i < 0) break;
                    SAT_Utils::select(i);
                    const SatCore::Block* sb = SAT_Utils::block();
                    bool usable = false;
                    for (int t = 0; sb && t < sb->txCount && !usable; t++) usable = FM_Utils::satUsable(t);
                    if (usable) break;
                }
                const SatCore::Block* sb = SAT_Utils::block();
                if (!sb) { showToast("No satellite data"); break; }
                int t = -1;
                for (int k = 0; t < 0 && k < sb->txCount; k++) if (FM_Utils::satUsable(k)) t = k;
                if (t >= 0) FM_Utils::satStart(t);                  // y el VFO pasa a seguirlo
                else { if (was) FM_Utils::satStop(); showToast("No usable TX"); }
                break;
            }
            #endif
            case C_QSY:         qsyToStation(heard.byRecent(lastIndex)); break;
            #endif
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
            case C_SYS_BT:
            case C_SYS_BTPROTO:
                if (command == C_SYS_BT) Config.bluetooth.active = !bluetoothActive;
                else                     Config.bluetooth.useKISS = !Config.bluetooth.useKISS;
                Config.writeFile();                              // el cambio de Bluetooth necesita reiniciar la pila: se guarda y se reinicia
                showToast("Restarting...");
                show();
                delay(600);
                ESP.restart();
                break;
            case C_SYS_KISSWIFI:
                #if KISS_NET_SUPPORTED
                    if (!aprsMode()) { showToast("APRS modes only"); break; }
                    Config.kissNet.enabled = !Config.kissNet.enabled;
                    KISS_NET::refresh();                         // arranca, reabre o apaga el enlace segun lo que quede activo (KISS y CAT lo comparten)
                    if (Config.kissNet.enabled && KISS_NET::state() == 0) {      // sin red WiFi configurada: no queda activado
                        Config.kissNet.enabled = false;
                        showToast("Set WiFi in WebUI");
                    } else {
                        showToast(Config.kissNet.enabled ? "WiFi KISS ON" : "WiFi KISS OFF");
                    }
                    Config.writeFile();
                #endif
                break;
            case C_SYS_CATWIFI:
                #if KISS_NET_SUPPORTED
                    if (!aprsMode()) { showToast("APRS modes only"); break; }
                    Config.cat.enabled = !Config.cat.enabled;
                    KISS_NET::refresh();
                    if (Config.cat.enabled && KISS_NET::state() == 0) {
                        Config.cat.enabled = false;
                        showToast("Set WiFi in WebUI");
                    } else {
                        showToast(Config.cat.enabled ? "WiFi CAT ON" : "WiFi CAT OFF");
                    }
                    Config.writeFile();
                #endif
                break;
            case C_SYS_IARU:
                showToast(IARU_Auto::request() ? "IARU: checking..." : "No GPS fix");
                break;
            case C_SYS_THEME: {
                static const char* const NAMES[3] = { "Theme: dark", "Theme: light", "Theme: auto" };
                Config.display.theme = (uint8_t)((Config.display.theme + 1) % 3);
                Config.writeFile();
                showToast(NAMES[Config.display.theme]);
                break;
            }
            case C_TEAM_BROWSE:
                if (teamCount == 0) { showToast("No callsigns set"); break; }
                teamMode = TMODE_BROWSE;
                teamCursor = 0;
                break;
            case C_TEAM_RADAR:
                if (teamCount == 0) { showToast("No callsigns set"); break; }
                teamMode = TMODE_RADAR;
                teamScale = 0;
                break;
            case C_TEAM_LIST:
                teamMode = TMODE_LIST;
                break;
            case C_TEAM_AUTO:
                teamScale = 0;
                break;
            case C_TEAM_GOTO:
                teamGoTo();
                break;
            case C_TEAM_PING:
                teamPing();
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
            case C_CW_SQUELCH:
                Config.cw.squelchDbm = cwNextSquelch(Config.cw.squelchDbm);
                Config.writeFile();
                if (Config.cw.squelchDbm == 0) showToast("CW squelch OFF");
                else { char t[24]; snprintf(t, sizeof(t), "CW squelch %d dBm", Config.cw.squelchDbm); showToast(t); }
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
            case C_SATRX_TOGGLE:
            #ifdef HAS_SAT
                if (SATRX_Utils::active()) { SATRX_Utils::stop(); showToast("Listen OFF"); }
                else if (SATRX_Utils::start(satTx)) { satView = 3; showToast("Listening"); }
                else showToast(SATRX_Utils::message());
            #endif
                break;
            case C_SATRX_TX:
            #ifdef HAS_SAT
            {
                const SatCore::Block* sb = SAT_Utils::block();
                if (!sb || sb->txCount == 0) { showToast("No transmitter data"); break; }
                const bool was = SATRX_Utils::active();
                if (was) SATRX_Utils::stop();
                uint8_t i = satTx;
                for (uint8_t k = 0; k < sb->txCount; k++) {                  // siguiente transmisor que se pueda recibir
                    i = (uint8_t)((i + 1) % sb->txCount);
                    if (SATRX_Utils::classify(sb->tx[i]) != SATRX_Utils::K_NO) break;
                }
                satTx = i;
                if (was && SATRX_Utils::start(satTx)) showToast("Listening");
                else if (was) showToast(SATRX_Utils::message());
                else { char tt[28]; snprintf(tt, sizeof(tt), "Tx %u: %.8s", (unsigned)satTx + 1, sb->tx[satTx].mode); showToast(tt); }
            }
            #endif
                break;
            case C_SATRX_FINE_UP:
            case C_SATRX_FINE_DN:
            #ifdef HAS_SAT
                SATRX_Utils::fine(command == C_SATRX_FINE_UP ? 100 : -100);
                { char tt[24]; snprintf(tt, sizeof(tt), "Fine %+d Hz", SATRX_Utils::fineHz()); showToast(tt); }
            #endif
                break;
            case C_SATRX_LORA:
            #ifdef HAS_SAT
                SATRX_Utils::nextLoraPreset();
                { char tt[24]; SATRX_Utils::loraText(tt, sizeof(tt)); showToast(tt); }
            #endif
                break;
            case C_SATRX_CLEAR:
            #ifdef HAS_SAT
                SATRX_Utils::clearLog();
                showToast("RX log cleared");
            #endif
                break;
            case C_AIS_VIEW: aisView ^= 1; break;
            case C_AIS_NEXT: aisSel++; break;
            case C_AIS_PREV: if (aisSel > 0) aisSel--; break;
            case C_AIS_GOTO: {
                int idx[AisCore::Table::CAP];
                const int m = AIS_Utils::sorted(idx, AisCore::Table::CAP);
                if (m == 0) { showToast("No vessels"); break; }
                if (aisSel >= m) aisSel = m - 1;
                const AisCore::Vessel* v = AIS_Utils::table().at(idx[aisSel]);
                if (!v || !v->hasPos) { showToast("Vessel has no position"); break; }
                AIS_Utils::goTo(v->mmsi);
                break;
            }
            case C_AIS_CH:
                Config.ais.channel = (Config.ais.channel + 1) % 3;
                AIS_Utils::setChannelMode(Config.ais.channel);
                Config.writeFile();
                break;
            case C_AIS_ALARM:
                Config.ais.cpaAlarm = !Config.ais.cpaAlarm;
                Config.writeFile();
                showToast(Config.ais.cpaAlarm ? "Collision alarm ON" : "Collision alarm OFF");
                break;
            case C_AIS_CLEAR: AIS_Utils::clear(); aisSel = 0; showToast("List cleared"); break;
            case C_WX_NET:
                showToast(BLE_Control::netRequest("wx") ? "Asked the phone" : "No app connected");
                break;
            case C_SAT_PHONE:
                showToast(BLE_Control::netRequest("sat") ? "Asked the phone" : "No app connected");
                break;
            case C_SATTX_POS:
            #ifdef HAS_SAT
            {
                #ifdef HAS_SA868
                const uint8_t pi = (uint8_t)(FM_Utils::satOn() ? FM_Utils::satTxIndex() : satTx);
                #else
                const uint8_t pi = satTx;
                #endif
                const bool okTx = SATTX_Utils::sendPosition(pi);
                showToast(okTx ? "Position sent" : SATTX_Utils::message());
            }
            #endif
                break;
            case C_SAT_FAV:
            #ifdef HAS_SAT
            {
                const SatCore::Block* fb = SAT_Utils::block();
                if (!fb) break;
                const bool on = !SAT_Utils::isFavorite(fb->noradId);
                if (!SAT_Utils::setFavorite(fb->noradId, on)) showToast("Favorites full (32)");
                else showToast(on ? "Added to favorites" : "Removed from favorites");
            }
            #endif
                break;
            case C_SATTX_SEND:
            #ifdef HAS_SAT
            {
                const bool okTx = SATTX_Utils::send(satTx, Config.sat.txText.c_str());
                showToast(okTx ? "Sent" : SATTX_Utils::message());
            }
            #endif
                break;
            case C_SAT_NEXT:
            case C_SAT_PREV: {
                SATRX_Utils::stop();
                const int i = SAT_Utils::step(command == C_SAT_NEXT ? 1 : -1);
                if (i >= 0) { SAT_Utils::select(i); satTx = 0; }
                else showToast("No satellite in this list");
                break;
            }
            case C_SAT_VIEW:
                if (satView == 2 && SAT_Utils::block() && satTx + 1 < SAT_Utils::block()->txCount) { satTx++; break; }          // en la vista de frecuencias recorre los transmisores
                satView = (satView + 1) % (SATRX_Utils::available() ? (Config.sat.txEnabled ? 5 : 4) : 3);
                satTx = 0;
                break;
            case C_ROT_SOURCE: {
                const int s = RotorFollow::nextSource(Config.rotor.source, Config.operatingMode == MODE_SAT);   // la fuente "satelite" solo existe en el modo SAT
                Config.rotor.source = s;
                Config.writeFile();
                char toast[32];
                snprintf(toast, sizeof(toast), "Rotor: %s", RotorFollow::sourceName(s));
                showToast(toast);
                break;
            }
            case C_ROT_NEXT:
            case C_ROT_PREV: {
                const int n = RotorConfig::TARGETS;
                int i = Config.rotor.targetIndex;
                for (int k = 0; k < n; k++) {
                    i = (i + (command == C_ROT_NEXT ? 1 : n - 1)) % n;
                    if (!Config.rotor.targets[i].name.isEmpty()) break;
                }
                Config.rotor.targetIndex = i;
                Config.rotor.source = 2;
                Config.writeFile();
                showToast(Config.rotor.targets[i].name.isEmpty() ? "No fixed targets" : Config.rotor.targets[i].name.c_str());
                break;
            }
            case C_SAT_CAT: {
                SAT_Utils::nextCategory();
                satTx = 0;
                char t[28]; snprintf(t, sizeof(t), "List: %s", SAT_Utils::categoryName());
                showToast(t);
                break;
            }
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

    // ---- Control remoto por BLE (app Android): cualquier comando de menu por nombre (Manuel - EA7JRS) ----

    // Comandos que no se ejecutan en remoto: abren submenus (navegacion), son sesiones interactivas con el pulsador
    // o reinician el equipo; tienen su propio comando BLE (mode, cw, wspr...) o no tienen sentido sin pantalla
    static bool remoteBlocked(Command c) {
        switch (c) {
            case C_BACK: case C_MENU: case C_MODE_CHANGE: case C_MSG_REPLY:
            case C_GPS_GNSS: case C_CLK_ZONE: case C_LORA_POWER: case C_ALT_QNH: case C_COMPASS_DECL:
            case C_MSG_QUICK: case C_TEAM_QUICK:
            case C_CW_LISTEN: case C_CW_TRAIN_LISTEN: case C_CW_TRAIN_KEY: case C_WSPR_TUNE: case C_CWB_TUNE: case C_RADIO_TUNE:   // la app: orden "carrier"
                return true;
            default:
                return false;
        }
    }

    static bool pageHasCommand(uint8_t p, Command c) {
        uint8_t count = 0;
        const Item* items = itemsFor(p, count);
        for (uint8_t i = 0; i < count; i++) if (items[i].cmd == c) return true;
        return false;
    }

    // Ejecuta el comando "name" (como en COMMAND_NAMES, sin mayusculas obligatorias). Devuelve un PAGES_CMD_* (>= 0)
    // o -1 con *error rellenado
    int remoteAct(const char* name, const char** error) {
        static const char* const NONE = "";
        *error = NONE;
        int found = -1;
        for (uint8_t i = 0; i < sizeof(COMMAND_NAMES) / sizeof(COMMAND_NAMES[0]); i++) {
            if (strcasecmp(COMMAND_NAMES[i], name) == 0) { found = i; break; }
        }
        if (found < 0) { *error = "accion desconocida"; return -1; }
        Command command = (Command)found;
        if (remoteBlocked(command)) { *error = "no disponible por BLE"; return -1; }
        if (sosState == SOS_COUNTDOWN && command != C_SOS_CANCEL) { *error = "cuenta atras SOS en curso"; return -1; }
        const bool leaf = (command >= C_G_GPS && command <= C_G_QZSS) || (command >= C_ZN_MODE && command <= C_ZN_DN) ||
                          (command >= C_PW_SHOW && command <= C_PW_DN) || (command >= C_Q_AUTO && command <= C_Q_DN2) ||
                          (command >= C_DECL_MODE && command <= C_DECL_DN2);
        const uint8_t savedId = submenuId;
        submenuId = SM_PAGE;                                      // las listas de pagina, no un segundo nivel
        bool ok = leaf || pageHasCommand(page, command);
        if (!ok) {
            for (uint8_t p = 0; p < PAGE_COUNT && !ok; p++) {
                if (!pageVisible(p) || p == page) continue;
                if (pageHasCommand(p, command)) { page = p; ok = true; }
            }
        }
        submenuId = savedId;
        if (!ok) { *error = "no disponible en este modo"; return -1; }
        submenuOpen = false;
        submenuId = SM_PAGE;
        return execute(command, true);
    }

    void forEachAction(void (*callback)(uint8_t page, const char* pageTitle, const char* name, const char* label, bool allowed, void* ctx), void* ctx) {
        const uint8_t savedId = submenuId;
        submenuId = SM_PAGE;
        for (uint8_t p = 0; p < PAGE_COUNT; p++) {
            if (!pageVisible(p)) continue;
            uint8_t count = 0;
            const Item* items = itemsFor(p, count);
            for (uint8_t i = 0; i < count; i++) {
                const Command c = items[i].cmd;
                if (c == C_BACK) continue;
                callback(p, PAGE_TITLES[p], COMMAND_NAMES[c], items[i].label, !remoteBlocked(c), ctx);
            }
        }
        submenuId = savedId;
    }

    // Datos para la app: mensajes APRS y estaciones escuchadas (Manuel - EA7JRS)
    uint32_t    msgSeq()      { return msgChangeSeq; }
    uint8_t     msgCount()    { return msgLog.count(); }
    uint8_t     msgUnread()   { return msgLog.unread(); }
    const PAGES_Core::MsgEntry* msgAt(uint8_t position) { return msgLog.byRecent(position); }
    uint8_t     msgStateAt(uint8_t position) {
        const MsgEntry* e = msgLog.byRecent(position);
        return e ? (uint8_t)MessageLog::stateOf(*e) : 0;
    }
    uint8_t     heardCount()  { return heard.count(); }
    uint32_t    heardPackets() { return heard.totalPackets(); }
    const PAGES_Core::Station* heardAt(uint8_t position) { return heard.byRecent(position); }
    int         sendMessage(const char* to, const char* text, bool wantAck) {
        int ackId = MSG_Utils::addToOutputBuffer(wantAck ? 1 : 0, String(to), String(text));
        if (wantAck) {
            if (ackId <= 0) return -1;                  // ya estaba en la cola
            onMessageSent(to, text, (uint16_t)ackId);
        } else {
            onMessageSent(to, text, 0);
        }
        return ackId;
    }

    bool        iaqValue(float& index, const char** label) {
        if (!iaq.ready()) return false;
        index = iaq.index();
        *label = iaq.label();
        return true;
    }

    uint8_t     screenPage()  { return mirrorPage; }
    const char* screenTitle() { return mirrorTitle; }
    const char* screenRight() { return mirrorRight; }
    const char* screenRow(uint8_t r) { return r < MIRROR_ROWS ? mirrorRows[r] : ""; }
    uint8_t     screenRows()  { return MIRROR_ROWS; }
    uint32_t    screenAgeMs() { return mirrorMs ? millis() - mirrorMs : 0xFFFFFFFFUL; }
    uint8_t     currentPage() { return page; }
    uint8_t     pageCount()   { return PAGE_COUNT; }
    const char* pageTitleOf(uint8_t p) { return p < PAGE_COUNT ? PAGE_TITLES[p] : ""; }
    bool        selectPage(uint8_t p) {
        if (p >= PAGE_COUNT || !pageVisible(p)) return false;
        if (page != p) { page = p; submenuOpen = false; submenuId = SM_PAGE; changedPage(); }
        return true;
    }

    bool wantsFastRefresh() {
        return viewing() && (page == PAGE_COMPASS || page == PAGE_FM || sosState == SOS_COUNTDOWN || alarmShown);
    }

    // El encoder sintoniza solo con la pagina FM a la vista y sin submenu (T-TWR)
    bool fmDialActive() { return viewing() && page == PAGE_FM && !submenuOpen && !alarmShown; }

}

#endif