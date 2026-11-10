/* Copyright (C) 2026 Ricardo Guzman - CA2RXU
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

/*___________________________________________________________________

██╗      ██████╗ ██████╗  █████╗      █████╗ ██████╗ ██████╗ ███████╗
██║     ██╔═══██╗██╔══██╗██╔══██╗    ██╔══██╗██╔══██╗██╔══██╗██╔════╝
██║     ██║   ██║██████╔╝███████║    ███████║██████╔╝██████╔╝███████╗
██║     ██║   ██║██╔══██╗██╔══██║    ██╔══██║██╔═══╝ ██╔══██╗╚════██║
███████╗╚██████╔╝██║  ██║██║  ██║    ██║  ██║██║     ██║  ██║███████║
╚══════╝ ╚═════╝ ╚═╝  ╚═╝╚═╝  ╚═╝    ╚═╝  ╚═╝╚═╝     ╚═╝  ╚═╝╚══════╝

      ████████╗██████╗  █████╗  ██████╗██╗  ██╗███████╗██████╗
      ╚══██╔══╝██╔══██╗██╔══██╗██╔════╝██║ ██╔╝██╔════╝██╔══██╗
         ██║   ██████╔╝███████║██║     █████╔╝ █████╗  ██████╔╝
         ██║   ██╔══██╗██╔══██║██║     ██╔═██╗ ██╔══╝  ██╔══██╗
         ██║   ██║  ██║██║  ██║╚██████╗██║  ██╗███████╗██║  ██║
         ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═╝ ╚═════╝╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝

                       Ricardo Guzman - CA2RXU
                        Manuel Canca - EA7JRS
          https://github.com/richonguzman/LoRa_APRS_Tracker
             (donations : http://paypal.me/richonguzman)
____________________________________________________________________*/

#include <BluetoothSerial.h>
#include <APRSPacketLib.h>
#include <TinyGPS++.h>
#include <Arduino.h>
#include <logger.h>
#include <WiFi.h>
#include "smartbeacon_utils.h"
#include "bluetooth_utils.h"
#include "keyboard_utils.h"
#include "joystick_utils.h"
#include "configuration.h"
#include "battery_utils.h"
#include "station_utils.h"
#include "sensors_utils.h"
#include "imu_utils.h"
#include "mag_utils.h"
#include "rtc_utils.h"
#include "nmea_link.h"
#include "cat_net.h"
#include "thermal_utils.h"
#include "board_pinout.h"
#include "button_utils.h"
#include "power_utils.h"
#include "sleep_utils.h"
#include "menu_utils.h"
#include "pages_utils.h"
#include "lora_utils.h"
#include "wifi_utils.h"
#include "msg_utils.h"
#include "gps_utils.h"
#include "web_utils.h"
#include "ble_utils.h"
#include "ble_control.h"
#include "wx_utils.h"
#include "display.h"
#include "utils.h"
#include "notification_utils.h"
#include "fw_update.h"
#include "iaru_auto.h"
#include "kiss_net.h"
#include "bcast_radio.h"
#include "sat_config.h"
#include "sat_utils.h"
#include "ais_utils.h"
#ifdef HAS_SA868
    #include "fm_utils.h"
    #include "sa868_radio.h"
#endif
#ifdef HAS_TOUCHSCREEN
#include "touch_utils.h"
#endif


String      versionDate             = "2026-11-01";
String      versionNumber           = "5.4.1";
const char* lastResetReason         = "";       // motivo del ultimo reinicio si no fue normal (fallo, vigilante, tension baja): log, pantalla y evento info

// Motivo del arranque: los reinicios por fallo (panic), vigilante o caida de tension se muestran al arrancar para poder
// diagnosticar un bucle de reinicios sin el puerto serie (EA7JRS)
static const char* abnormalResetReason() {
    switch (esp_reset_reason()) {
        case ESP_RST_PANIC:    return "CRASH";
        case ESP_RST_INT_WDT:  return "INT WDT";
        case ESP_RST_TASK_WDT: return "TASK WDT";
        case ESP_RST_WDT:      return "WDT";
        case ESP_RST_BROWNOUT: return "LOW VOLTAGE";
        default:               return "";
    }
}
Configuration                       Config;
HardwareSerial                      gpsSerial(1);
TinyGPSPlus                         gps;
#ifdef HAS_BT_CLASSIC
    BluetoothSerial                 SerialBT;
#endif

uint8_t     myBeaconsIndex          = 0;
int         myBeaconsSize           = 0;                 // se asigna en setup() tras Config.init()
Beacon      *currentBeacon          = nullptr;           // idem
uint8_t     loraIndex               = 0;
int         loraIndexSize           = 0;                 // idem (el perfil LoRa AIS no rota con el menu)
LoraType    *currentLoRaType        = nullptr;           // idem

int         menuDisplay             = 100;
uint32_t    menuTime                = millis();

bool        statusUpdate            = true;
bool        displayEcoMode          = false;             // idem
bool        displayState            = true;
uint32_t    displayTime             = millis();
uint32_t    refreshDisplayTime      = millis();

bool        sendUpdate              = true;

bool        bluetoothActive         = false;             // idem
bool        bluetoothConnected      = false;

uint32_t    lastTx                  = 0;
uint32_t    txInterval              = 60000L;
uint32_t    lastTxTime              = 0;
double      lastTxLat               = 0.0;
double      lastTxLng               = 0.0;
double      lastTxDistance          = 0.0;

bool        flashlight              = false;
bool        digipeaterActive        = false;
bool        sosActive               = false;

bool        miceActive              = false;

bool        smartBeaconActive       = true;

uint32_t    lastGPSTime             = 0;

APRSPacket                          lastReceivedPacket;

logging::Logger                     logger;
//#define DEBUG

extern bool gpsIsActive;

// Pila de loopTask: 8 KB (por defecto) se agota al guardar la configuracion (serializacion JSON anidada dentro de setup()) -> "Stack canary watchpoint triggered".
#ifdef SET_LOOP_TASK_STACK_SIZE
    SET_LOOP_TASK_STACK_SIZE(16 * 1024);
#endif

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== LoRa APRS Tracker 5.4.1 | build: main (2026-11-10) ===");   // si no ves esta linea, no se ha grabado este firmware
    FW_Update::printBanner();                                      // placa + version para el instalador web USB
    Config.init();                                                 // antes estaba en el constructor global (provocaba el bucle de arranque)
    myBeaconsSize     = Config.beacons.size();
    currentBeacon     = &Config.beacons[myBeaconsIndex];
    loraIndexSize     = Config.loraTypes.size() - (Config.aisLoraIndex() >= 0 ? 1 : 0);
    currentLoRaType   = &Config.loraTypes[loraIndex];
    displayEcoMode    = Config.display.ecoMode;
    bluetoothActive   = Config.bluetooth.active;

    #ifndef DEBUG
        logger.setDebugLevel(Config.cat.usb ? logging::LoggerLevel::LOGGER_LEVEL_ERROR : logging::LoggerLevel::LOGGER_LEVEL_INFO);   // CAT por USB: el log no se mezcla con las respuestas
    #endif

    POWER_Utils::setup();
    displaySetup();
    lastResetReason = abnormalResetReason();
    if (lastResetReason[0]) {
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "Main", "Last reset: %s", lastResetReason);
        displayShow("LAST RESET", "", lastResetReason, "", "Check power supply", "or send the USB log", 2500);
    }
    if (POWER_Utils::pmuOffReason()[0]) {         // la PMU se apago sola (no fue la tecla ni el software)
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "Main", "PMU power off: %s", POWER_Utils::pmuOffReason());
        displayShow("PMU POWER OFF", "", POWER_Utils::pmuOffReason(), "", "Charge the battery", "or check the USB supply", 2500);
    }
    POWER_Utils::externalPinSetup();
    #ifdef FAN_CTRL_PIN
        THERMAL_Utils::setup();
    #endif

    STATION_Utils::loadIndex(0);    // callsign Index
    STATION_Utils::loadIndex(1);    // lora freq settings Index
    currentBeacon   = &Config.beacons[myBeaconsIndex];
    currentLoRaType = &Config.loraTypes[loraIndex];
    STATION_Utils::nearStationInit();
    startupScreen(loraIndex, versionNumber, currentBeacon->callsign);
    #ifdef HAS_APRS_PAGES
        PAGES_Utils::selectMode();      // APRS LoRa / APRS FSK / MORSE: debe decidirse antes de arrancar la radio
    #endif

    WIFI_Utils::checkIfWiFiAP();

    MSG_Utils::loadNumMessages();
    GPS_Utils::setup();
    LoRa_Utils::setup();
    LoRa_Utils::syncAisProfile();
    Utils::i2cScannerForPeripherals();
    WX_Utils::setup();
    BCAST::begin();                                                 // radio comercial por I2C (si esta activada en la WebUI)
    SENSORS_Utils::logBoardInventory();
    IMU_Utils::setup();
    MAG_Utils::setup();
    #ifdef HAS_APRS_PAGES
        PAGES_Utils::setup();
    #endif
    RTC_Utils::setup();
    GPS_Utils::loadLastPosition();

    WiFi.mode(WIFI_OFF);
    logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "Main", "WiFi controller stopped");

    // NMEA_Link despues de WiFi.mode(WIFI_OFF) por si NMEA_TCP esta activo y debe levantar red
    NMEA_Link::setup();
    AIS_Utils::begin();                                             // receptor AIS: modulo externo por UART (EA7JRS)
    KISS_NET::setup();                                             // KISS por WiFi (si esta activado): se une a la WiFi sin bloquear el arranque

    if (bluetoothActive) {
        if (Config.bluetooth.useBLE) {
            BLE_Utils::setup();
        } else {
            #ifdef HAS_BT_CLASSIC
                BLUETOOTH_Utils::setup();
            #endif
        }
    }

    #ifdef BUTTON_PIN
        BUTTON_Utils::setup();
    #endif
    #ifdef HAS_JOYSTICK
        JOYSTICK_Utils::setup();
    #endif
    KEYBOARD_Utils::setup();
    #ifdef HAS_TOUCHSCREEN
        TOUCH_Utils::setup();
    #endif

    esp_random();
    randomSeed(esp_random());

    POWER_Utils::lowerCpuFrequency();
    logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "Main", "Smart Beacon is: %s", Utils::getSmartBeaconState().c_str());
    logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Main", "Setup Done!");
    menuDisplay = 0;
    #ifdef RPC_MicroTracker
        pinMode(BUTTON_AP_PIN, INPUT);
        delay(50);
        if (digitalRead(BUTTON_AP_PIN) == LOW) {
            Config.wifiAP.active = true;
            Config.writeFile();
            ESP.restart();
        }
    #endif
}

void loop() {
    FW_Update::serialPoll();                                       // "APRS-INFO?" por USB -> placa y version
    NOTIFICATION_Utils::update();                                  // campanas APRS del zumbador (sin bloqueos)
    currentBeacon = &Config.beacons[myBeaconsIndex];
    if (statusUpdate) {
        if (APRSPacketLib::checkNocall(currentBeacon->callsign)) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "Config", "Change your callsigns in WebConfig");
            displayShow("ERROR", "Callsigns = NOCALL!", "---> change it !!!", 2000);
            KEYBOARD_Utils::rightArrow();
            currentBeacon = &Config.beacons[myBeaconsIndex];
        }
        miceActive = APRSPacketLib::validateMicE(currentBeacon->micE);
    }

    LoRa_Utils::syncAisProfile();
    SMARTBEACON_Utils::checkSettings(currentBeacon->smartBeaconSetting);
    SMARTBEACON_Utils::checkState();

    BATTERY_Utils::monitor();
    #ifdef FAN_CTRL_PIN
        THERMAL_Utils::monitor();
    #endif
    Utils::checkDisplayEcoMode();

    #ifdef BUTTON_PIN
        BUTTON_Utils::loop();
    #endif
    KEYBOARD_Utils::read();
    #ifdef HAS_JOYSTICK
        JOYSTICK_Utils::loop();
    #endif
    #ifdef HAS_TOUCHSCREEN
        TOUCH_Utils::loop();
    #endif

    ReceivedLoRaPacket packet = LoRa_Utils::receivePacket();
    if (!packet.text.isEmpty()) BLE_Control::rfEvent('r', packet.rssi, packet.snr);     // luz RX de la app (EA7JRS)
    #ifdef HAS_SA868
        if (SA868::modeFm()) FM_Utils::loop();     // T-TWR: PTT, escaneo, ARTS, EAI; modos FM y SAT (VFO que sigue al satelite) (EA7JRS)
    #endif
    BCAST::loop();
    SAT_Utils::service();                                           // subida del archivo de satelites por la WebUI (EA7JRS)
    AIS_Utils::loop();                                              // receptor AIS: modulo, canales, alarmas (EA7JRS)

    MSG_Utils::checkReceivedMessage(packet);
    MSG_Utils::processOutputBuffer();
    MSG_Utils::clean15SegBuffer();

    if (packet.text.length() > 3) { KISS_NET::onRx(packet.text.substring(3)); CAT_NET::onRx(packet.rssi); }
    KISS_NET::poll();

    if (bluetoothActive && bluetoothConnected) {
        if (Config.bluetooth.useBLE) {
            if (packet.text.length() > 3) BLE_Utils::sendToPhone(packet.text.substring(3));
            BLE_Utils::sendToLoRa();
        } else {
            #ifdef HAS_BT_CLASSIC
                if (packet.text.length() > 3) BLUETOOTH_Utils::sendToPhone(packet.text.substring(3));
                BLUETOOTH_Utils::sendToLoRa();
            #endif
        }
    }

    MSG_Utils::ledNotification();
    Utils::checkFlashlight();
    IARU_Auto::loop();                                              // region IARU automatica segun el GPS (si esta activada)
    STATION_Utils::checkListenedStationsByTimeAndDelete();

    lastTx = millis() - lastTxTime;
    IMU_Utils::update();
    MAG_Utils::update();
    RTC_Utils::update();
    WX_Utils::update();
    if (gpsIsActive) {
        GPS_Utils::getData();
        bool gps_time_update = gps.time.isUpdated();
        bool gps_loc_update  = gps.location.isUpdated();
        GPS_Utils::setDateFromData();

        int currentSpeed = (int) GPS_Utils::getSpeedKmph();

        if (gps_loc_update) {
            Utils::checkStatus();
            GPS_Utils::updateLastPosition();
            MAG_Utils::feedGpsCourse(GPS_Utils::getCourseDeg(), GPS_Utils::getSpeedKmph(),
                                     gps.satellites.value() >= 6 && gps.hdop.isValid() && gps.hdop.hdop() <= 2.5 && gps.course.isValid());
        }

        // Actualizar el intervalo de SmartBeacon/AIS antes de evaluar si toca transmitir
        if (gps_time_update || gps_loc_update) SMARTBEACON_Utils::checkInterval(currentSpeed);

        if (!sendUpdate && gps_loc_update && smartBeaconActive) {
            GPS_Utils::calculateDistanceTraveled();
            if (!sendUpdate) GPS_Utils::calculateHeadingDelta(currentSpeed);
            STATION_Utils::checkStandingUpdateTime();
        }
        SMARTBEACON_Utils::checkFixedBeaconTime();
        #ifdef HAS_APRS_PAGES
            if (sendUpdate && !PAGES_Utils::beaconAllowed()) sendUpdate = false;     // modo manual: sin balizas automaticas
        #endif
        if (sendUpdate && gps_loc_update) {
            STATION_Utils::sendBeacon();
            #ifdef HAS_APRS_PAGES
                PAGES_Utils::beaconSent();
            #endif
        }

        uint32_t refreshPeriod = 1000;
        #ifdef HAS_APRS_PAGES
            if (PAGES_Utils::wantsFastRefresh()) refreshPeriod = 200;      // brujula: ~5 Hz
        #endif
        if (millis() - refreshDisplayTime >= refreshPeriod || gps_time_update) {
            GPS_Utils::checkStartUpFrames();
            MENU_Utils::showOnScreen();
            refreshDisplayTime = millis();
        }
        SLEEP_Utils::checkIfGPSShouldSleep();

        NMEA_Link::update();    // despues del bloque GPS: lat()/lng() borran el indicador "updated" de TinyGPS
        #ifdef HAS_APRS_PAGES
            PAGES_Utils::update();
        #endif

        if (gpsSerial.available() == 0) {
            bool stationary = IMU_Utils::isReady() && !IMU_Utils::movingNow() && !GPS_Utils::isMoving();
            delay(stationary ? 5 : 2);
        }
    } else {
        if (SLEEP_Utils::gpsShouldWakeUp()) {
            SLEEP_Utils::gpsWakeUp();
        }
        STATION_Utils::checkStandingUpdateTime();
        uint32_t refreshPeriodNoGps = 1000;
        #ifdef HAS_APRS_PAGES
            if (PAGES_Utils::wantsFastRefresh()) refreshPeriodNoGps = 200;
        #endif
        if (millis() - refreshDisplayTime >= refreshPeriodNoGps) {
            MENU_Utils::showOnScreen();
            refreshDisplayTime = millis();
        }
        NMEA_Link::update();
        #ifdef HAS_APRS_PAGES
            PAGES_Utils::update();
        #endif
        delay(10);
    }

    if (bluetoothActive && Config.bluetooth.useBLE) BLE_Control::loop();     // app APRS LoRa+: ordenes y eventos de control (EA7JRS)
}