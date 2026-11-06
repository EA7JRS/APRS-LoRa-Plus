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

#ifndef CONFIGURATION_H_
#define CONFIGURATION_H_

#include <Arduino.h>
#include <vector>
#include <FS.h>


class WiFiAP {
public:
    bool    active;
    String  password;
};

// Actualizacion del firmware (5.3.1): WiFi de casa para buscar versiones nuevas y opciones automaticas
// KISS por WiFi (5.3.2): servidor TCP (y UDP opcional) en la red WiFi a la que se une el tracker (la misma de la actualizacion de firmware)
class KissNetConfig {
public:
    bool    enabled;            // apagado por defecto (la WiFi consume y impide el sueno profundo)
    int     tcpPort;            // puerto TCP KISS (8001 por defecto, como Dire Wolf)
    int     udpPort;            // puerto UDP; 0 = sin UDP
    String  allowed;            // direcciones IP permitidas (o prefijos acabados en punto); vacio = cualquiera de la red
};

// CAT por WiFi (Hamlib/rigctld): servidor TCP compatible con el modelo "NET rigctl" de Hamlib, en la misma red WiFi que KISS
class CatConfig {
public:
    bool    enabled;            // WiFi: apagado por defecto
    bool    usb;                // CAT por el puerto USB (serie), apagado por defecto; aplica al reiniciar (silencia el log)
    int     port;               // puerto TCP (4532 por defecto, como rigctld)
    String  allowed;            // direcciones IP permitidas (o prefijos acabados en punto); vacio = cualquiera de la red
    bool    pttBeacon;          // PTT 1 emite una baliza APRS (apagado por defecto)
    int     tuneKHz;            // ventana de sintonia en kHz alrededor de la frecuencia del perfil (0 = el cliente no puede cambiar la frecuencia)
};

// Salida NMEA por UART hacia un conversor TTL->RS422 (plotter Garmin): pin TX y velocidad. Solo las placas con NMEA_OUTPUT (T-Beam S3 Supreme); aplica al reiniciar
class NmeaUartConfig {
public:
    int     txPin;              // GPIO TX; -1 = sin salida por UART
    int     baud;               // 4800, 9600 o 38400 (velocidad de AIS)
};

// Vigilancia de grupo (estilo ARTS) y emergencia remota (estilo EAI)
class TeamConfig {
public:
    bool    watchActive;        // vigila a las estaciones de la lista y avisa al entrar/salir de alcance
    String  watchCalls;         // indicativos separados por comas o espacios (maximo 5); sin SSID = cualquier SSID
    int     timeoutMin;         // minutos sin oirla para darla por fuera de alcance
    int     maxKm;              // distancia maxima en km (0 = sin limite)
    int     pollSec;            // 0 = solo escucha; >= 60: envia "ping" a quien no se oye desde hace ese tiempo
    bool    eaiActive;          // permite la emergencia remota por mensaje APRS
    String  eaiCalls;           // quien puede activarla (maximo 5 indicativos)
    String  eaiCode;            // codigo (4-12 caracteres) que debe llevar el mensaje "EAI <codigo>"
    int     eaiMinutes;         // duracion de la emergencia remota (1-50 min)
};

class FwUpdate {
public:
    String  ssid;                       // red WiFi (cliente) con internet; vacio = sin comprobacion en linea
    String  password;
    bool    autoCheck;                  // al arrancar, buscar una version nueva (retrasa el arranque unos segundos solo si hay red)
    bool    autoInstall;                // y, si hay una nueva, instalarla sin preguntar (solo placas con dos ranuras de firmware)
};

class Beacon {
public:
    String  callsign;
    String  symbol;
    String  overlay;
    String  micE;
    String  comment;
    bool    smartBeaconActive;
    byte    smartBeaconSetting;
    bool    gpsEcoMode;
    String  profileLabel;
    String  status;

    // Perfil nautico AIS (solo el perfil con aisProfile = true; ver ais_vessel.h)
    bool        aisProfile      = false;
    uint8_t     aisVesselType   = 0;        // indice de la tabla de embarcaciones (clase AIS, SSID y simbolo salen de ahi)
    String      aisName;                    // nombre de la embarcacion (max. 20 caracteres)
    uint32_t    aisMmsi         = 0;        // 9 digitos; 0 = no definido (no se envian datos estaticos)
    uint16_t    aisLength       = 0;        // eslora (m)
    uint16_t    aisBeam         = 0;        // manga (m)
    float       aisDraft        = 0.0f;     // calado (m)
};

class Display {
public:
    bool    showSymbol;
    bool    ecoMode;
    int     timeout;
    bool    turn180;
};

class Battery {
public:
    bool    sendVoltage;
    bool    voltageAsTelemetry;
    bool    sendVoltageAlways;
    bool    monitorVoltage;
    float   sleepVoltage;
};

class Winlink {
public:
    String  password;
};

class Telemetry {
public:
    bool    active;
    bool    sendTelemetry;
    float   temperatureCorrection;
};

class Notification {
public:
    bool    ledTx;
    int     ledTxPin;
    bool    ledMessage;                         // LED verde (Rx): parpadea mientras haya mensajes sin leer (y alarma de emergencia)
    int     ledMessagePin;
    bool    ledRxBeacon;                        // LED verde (Rx): destello corto por cada paquete recibido de otra estacion (balizas...)
    bool    ledTxCw;                            // modo CW: LED rojo (Tx) = portadora emitida (tecla pulsada, macros, baliza CW)
    bool    ledRxCw;                            // modo CW: LED verde (Rx) = senal CW recibida
    bool    flashCw;                            // modo CW: flash blanco = lampara Morse (manipulador y entrenador)
    bool    flashEmergency;                     // flash blanco: estroboscopio con una alarma de emergencia o la baliza SOS propia (necesita el flash activo)
    bool    ledFlashlight;
    int     ledFlashlightPin;
    bool    buzzerActive;
    int     buzzerPinTone;
    int     buzzerPinVcc;
    bool    bootUpBeep;
    bool    txBeep;
    bool    messageRxBeep;
    bool    stationBeep;
    bool    lowBatteryBeep;
    bool    shutDownBeep;
    bool    bellRings;                          // melodias de campana APRS (estilo Yaesu FTM-400DR) en lugar de pitidos simples
};

class LoraType {
public:
    long    frequency;
    int     spreadingFactor;
    long    signalBandwidth;
    int     codingRate4;
    int     power;
    bool    ais             = false;        // perfil LoRa fijo del perfil nautico AIS (siempre SF9 / CR 4/5)
    bool    fsk             = false;        // true = packet AX.25 FSK G3RUH (9600 bd) en lugar de LoRa (no disponible en LLCC68)
    float   fskBitRate      = 9.6f;         // kbps (9.6 = G3RUH estandar)
    float   fskFreqDev      = 3.0f;         // kHz (desviacion de frecuencia)
    float   fskRxBandwidth  = 39.0f;        // kHz (ancho de banda del receptor; valor valido en SX126x y SX127x)
    int     iaruRegion      = 1;            // region IARU (1, 2, 3) usada por la WebUI para el plan de bandas
    long    fskFrequency    = 0;            // APRS FSK: frecuencia propia del perfil (Hz); 0 = la del perfil LoRa (configuraciones anteriores)
    int     fskPower        = 0;            // APRS FSK: potencia propia del perfil (dBm); 0 = la del perfil LoRa
    int     fskIaruRegion   = 1;            // region IARU del perfil FSK (plan de bandas de la WebUI)
    int     fskShaping      = 1;            // filtro gaussiano del transmisor FSK: 0 sin filtro (FSK), 1 BT 0,5 (GFSK, G3RUH), 2 BT 0,3 (GMSK), 3 BT 0,7, 4 BT 1,0
    int     fskFec          = 0;            // protocolo de emision FSK: 0 AX.25, 1 FX.25 (16 bytes de control), 2 FX.25 (32), 3 FX.25 (64), 4 IL2P. La recepcion entiende AX.25 y FX.25 siempre
};

// Region IARU automatica: la region (1, 2, 3) de todos los perfiles se ajusta sola segun la posicion GPS (plan de bandas incluido)
class IaruAutoConfig {
public:
    int     mode;               // 0 = apagada (por defecto, region a mano en la WebUI), 1 = al arrancar y bajo peticion, 2 = ademas en marcha (al cruzar un limite)
    bool    confirm;            // true = pide confirmacion en pantalla antes de cambiar una frecuencia; false = la aplica directamente
};

class PTT {
public:
    bool    active;
    int     io_pin;
    int     preDelay;
    int     postDelay;
    bool    reverse;
};

// Modo de funcionamiento, elegido al arrancar tras el mensaje de bienvenida
enum { MODE_APRS_LORA = 0, MODE_APRS_FSK = 1, MODE_MORSE = 2, MODE_WSPR = 3, MODE_LRFHSS = 4, MODE_DAPNET = 5, MODE_FT8 = 6, MODE_FT4 = 7, MODE_FST4W = 8, MODE_SAT = 9 };

// Baliza WSPR (modo WSPR): mensaje tipo 1 con el indicativo del perfil de baliza (sin SSID), locator de 4 caracteres y potencia
class WSPRConfig {
public:
    int     freqHz;                             // frecuencia central (dial + 1500 Hz); 70 cm: dial 432,300 MHz -> 432301500
    int     corrHz;                             // correccion medida del oscilador (Hz, se suma a la frecuencia)
    bool    useGlobalPpm;                       // true: la correccion es la global en ppm (radioPpm1000) aplicada a la frecuencia WSPR; false: corrHz
    int     powerDbm;                           // potencia del mensaje y de la radio: 0, 3, 7, 10, 13, 17 o 20
    int     intervalMin;                        // una emision cada N minutos (par: 2, 4, 6, 10, 12, 20, 30, 60)
    String  locator;                            // locator fijo de 4 caracteres; vacio = el del GPS
    bool    dither;                             // afinado de tonos: cada simbolo en dos tramos (dos pasos del PLL) para acercar la separacion a 1,4648 Hz
    bool    screenOff;                          // apaga la pantalla durante la emision (menos consumo y calor junto al TCXO)
    int     driftTenths;                        // compensacion de deriva en decimas de Hz/min (-200..200); positivo = la frecuencia sube durante la emision
    bool    perPower;                           // usa la tabla por potencia en lugar de corrHz y driftTenths
    int     ppCorrHz[7];                        // offset (Hz) para 0, 3, 7, 10, 13, 17 y 20 dBm
    int     ppDriftTenths[7];                   // compensacion de deriva (0,1 Hz/min) para 0, 3, 7, 10, 13, 17 y 20 dBm
};

// GPS: ajustes del receptor GNSS. u-blox M10 (MAX-M10S): todos; L76K (CASIC): solo constelaciones GPS/BeiDou/GLONASS y tasa. Se aplican al arrancar.
class GPSConfig {
public:
    int     gnssMask;                           // bits: 1 GPS, 2 Galileo, 4 BeiDou, 8 GLONASS, 16 QZSS, 32 SBAS
    bool    assistNow;                          // AssistNow Autonomous (A-GPS offline del M10)
    int     dynModel;                           // modelo dinamico: 0 portatil, 2 estacionario, 3 peaton, 4 automovil, 5 mar, 6/7/8 aereo <1/2/4 g, 9 muneca
    int     rateMs;                             // periodo entre medidas (ms): M10 500, 1000, 2000, 5000 o 10000; L76K 200, 250, 500 o 1000 (maximo 5 Hz)
    int     minElev;                            // elevacion minima de satelite (grados, 0-30)
    int     staticHoldCms;                      // static hold: velocidad (cm/s); 0 = valor del firmware del receptor
    int     staticHoldDistM;                    // static hold: distancia de salida (m)
    bool    sbasDiffCorr;                       // SBAS: usar correcciones diferenciales
    bool    sbasRanging;                        // SBAS: usar los satelites GEO como fuente de medida
    bool    sbasIntegrity;                      // SBAS: usar informacion de integridad
    bool    ppsEnabled;                         // salida de pulso (timepulse TP1)
    int     ppsFreqHz;                          // frecuencia del pulso (Hz): 1, 2, 5 o 10
    int     ppsLenMs;                           // anchura del pulso (ms)
    bool    ppsRising;                          // true: flanco de subida al inicio del segundo; false: de bajada
    int     ppsCableDelayNs;                    // retardo del cable de antena (ns)
    int     ppsUserDelayNs;                     // retardo de usuario del pulso (ns)
    int     cnMask;                             // receptor CASIC (Quectel L76K): constelaciones, bits 1 GPS, 4 BeiDou, 8 GLONASS (QZSS siempre activo; sin Galileo ni SBAS)
    bool    nmeaLean;                           // L76K: salida NMEA reducida (GGA, RMC, GSV, antena): menos trafico serie, imprescindible por encima de 1 Hz
};

// Baliza LR-FHSS de propagacion, modo propio, independiente del WSPR; DESACTIVADO por defecto y reservado en la WebUI hasta su autorizacion (solo SX1262/SX1268; solo transmision): "PB1 indicativo locator-de-6 potencia"
// Receptor de buscapersonas DAPNET (modo DAPNET): POCSAG, solo recepcion
class DAPNETConfig {
public:
    int     freqHz;                             // frecuencia de recepcion (Hz); DAPNET: 439,9875 MHz en la mayor parte de Europa
    int     baud;                               // velocidad POCSAG: 512, 1200 o 2400 baudios
    int     devTenthsKHz;                       // desviacion FSK en decimas de kHz (POCSAG: +-4,5 kHz = 45)
    int     rxBwTenthsKHz;                      // ancho de banda de recepcion en decimas de kHz (valores del SX126x: 117, 146, 195, 234, 293)
    String  rics;                               // RIC propios separados por coma (0 a 2097151); el 8 (identificacion de transmisores) se muestra siempre
    bool    monitorAll;                         // mostrar todos los mensajes que se reciban, sean para ti o no
    bool    timeSync;                           // poner el reloj con la hora UTC que difunde DAPNET cuando no hay hora GPS
    int     charset;                            // 0 = ASCII, 1 = aleman ISO 646 (DIN 66003)
};

// Modo SAT (satelites en 70 cm): prediccion de pasos y Doppler. Los TLE y las frecuencias se descargan por WiFi (la red de Firmware update)
class SatConfig {
public:
    int     noradId;                            // satelite elegido (numero NORAD; 0 = el primero de la lista)
    int     category;                           // filtro de la lista: 0 todos, 1 70 cm, 2 LoRa / LR-FHSS / TinyGS, 3 geoestacionarios (Inmarsat: solo seguimiento)
    int     minEl;                              // elevacion minima de un paso util (grados)
    double  lat, lon;                           // posicion fija de la estacion cuando no hay GPS (0,0 = sin definir)
    String  dataUrl;                            // direccion del archivo de datos (vacio = el oficial del proyecto)
};

// Control de rotor de antenas (modo SAT): cliente de rotctld de Hamlib por TCP / WiFi, GS-232 por Bluetooth SPP, o evento BLE para la app
class RotorConfig {
public:
    bool    enabled;
    int     transport;                          // 0 = TCP (rotctld), 1 = Bluetooth SPP (GS-232)
    String  host;                               // rotctld: nombre o IP
    int     port;                               // rotctld: 4533 por defecto
    String  btName;                             // nombre Bluetooth del controlador (transporte 1)
    int     stepTenths;                         // paso minimo entre ordenes (decimas de grado)
    int     preSec;                             // segundos antes del AOS en que se coloca el rotor
    int     azOffset, elOffset;                 // correcciones (grados)
    bool    park;                               // ir a la posicion de reposo al terminar el paso
    int     parkAz, parkEl;
    bool    bleEvents;                          // enviar el evento "rot" a la app por BLE
};

// Balizas FT8 y FT4 (modos FT8 y FT4; solo emision, SX1262/SX1268): mensaje estandar con el indicativo del perfil de baliza, ranuras de 15 s (FT8) o 7,5 s (FT4) sincronizadas con el GPS
class FTConfig {
public:
    int     ft8FreqHz;                          // frecuencia de DIAL de FT8 (Hz); el tono 0 sale a dial + 1500 Hz. 70 cm: 432,174 MHz (WSJT-X)
    int     ft4FreqHz;                          // frecuencia de DIAL de FT4 (Hz); 0 = sin definir (el plan IARU no fija canal): no emite hasta que se rellene
    int     ft8Region;                          // region IARU (1, 2, 3) del perfil FT8 (aviso de segmento en la WebUI)
    int     ft4Region;                          // region IARU del perfil FT4
    int     fstFreqHz;                          // frecuencia de DIAL de FST4W (Hz); 0 = sin definir (no hay canal de FST4W en 70 cm en WSJT-X ni en el plan IARU): no emite hasta que se rellene
    int     fstRegion;                          // region IARU del perfil FST4W
    int     fstSeconds;                         // periodo T/R de FST4W: 120 o 300 s
    int     fstEvery;                           // una emision de FST4W cada N periodos (2..240)
    int     corrHz;                             // desplazamiento adicional (Hz) sobre la correccion global en ppm
    int     powerDbm;                           // potencia de salida (dBm), 0..22
    int     everyCycles;                        // una emision cada N ciclos (15 s en FT8, 7,5 s en FT4), 2..240
    String  locator;                            // locator fijo de 4 caracteres; vacio = el del GPS
    String  text;                               // mensaje libre (hasta 35 caracteres); vacio = "CQ indicativo locator"
    bool    screenOff;                          // pantalla apagada durante la emision (la actualizacion de la pantalla interrumpiria el PLL)
};

class LRFHSSConfig {
public:
    bool    enabled;                            // DESACTIVADA por defecto
    int     freqHz;                             // frecuencia central (Hz): debe estar en 430-440 MHz y fuera de 432,0-432,5 y 435-438 MHz (ver lrfhss.h)
    int     bwIdx;                              // anchura de rejilla LR-FHSS: indice 0..9 (39,06 ... 1574,2 kHz); 0 = 39,06 kHz (dentro de los 125 kHz del plan IARU R1)
    int     crIdx;                              // velocidad de codigo: 0 = 5/6, 1 = 2/3, 2 = 1/2, 3 = 1/3
    bool    narrowGrid;                         // true: rejilla de 3,9 kHz; false: 25,4 kHz
    int     powerDbm;                           // potencia de salida (dBm); es la que se anuncia en el mensaje
    int     intervalMin;                        // una emision cada N minutos (par: 2, 4, 6, 8, 10, 12, 20, 30, 60)
    String  locator;                            // locator fijo de 4, 6 u 8 caracteres; vacio = el del GPS
    int     locatorLen;                         // precision del locator que se emite: 4, 6 (por defecto) u 8 caracteres (8 = unos 700 x 460 m: revela la posicion casi exacta)
};

#define CW_MESSAGE_COUNT    5
class CWConfig {
public:
    int     wpm;                                // 5-40 palabras por minuto
    int     keyMode;                            // 0 = manipulador vertical, 1 = iambico A, 2 = iambico B
    int     keyPin;                             // GPIO del jack (punta): manipulador vertical / punto; -1 = sin manipulador
    int     dahPin;                             // GPIO del jack (anillo): raya; -1 = sin
    bool    swapPaddles;                        // intercambia punto y raya (zurdos)
    int     keypadPin;                          // GPIO con ADC del teclado Yaesu FH-2 (escalera de resistencias + pull-up a 3V3); -1 = sin teclado
    int     keypadMv[CW_MESSAGE_COUNT];         // milivoltios medidos al pulsar las teclas 1-5 (calibracion); 0 = sin calibrar
    bool    keyTx;                              // el manipulador tambien emite la portadora RF
    bool    sidetone;                           // tono local en el zumbador al manipular
    int     trainerLevel;                       // nivel Koch del trainer (cuantos caracteres, 2-41)
    int     squelchDbm;                         // CW por RF en recepcion: la portadora solo cuenta si supera este nivel (dBm, -130..-40); 0 = sin squelch (solo deteccion automatica)
    int     trainerGapWpm;                      // Farnsworth: velocidad efectiva de los espacios en el trainer (0 = igual que wpm)
    int     beaconMin;                          // baliza CW autonoma: minutos entre emisiones (0 = desactivada)
    // Dos perfiles de radio: baliza CW (perfil 1) y telegrafia con manipulador / mensajes / trainer (perfil 2)
    long    beaconFreq;                         // perfil CW Beacon: frecuencia (Hz)
    int     beaconPower;                        // perfil CW Beacon: potencia (dBm)
    int     beaconRegion;                       // perfil CW Beacon: region IARU (1, 2, 3)
    long    keyFreq;                            // perfil Telegrafia: frecuencia (Hz)
    int     keyPower;                           // perfil Telegrafia: potencia (dBm)
    int     keyRegion;                          // perfil Telegrafia: region IARU (1, 2, 3)
    String  beaconText;                         // texto de la baliza; {CALL}, {LOC} (locator 6) y {VBAT} (voltios de bateria)
    String  messages[CW_MESSAGE_COUNT];         // texto de los mensajes; {CALL} = indicativo sin SSID
};

class BLUETOOTH {
public:
    bool    active;
    String  deviceName;
    bool    useBLE;
    bool    useKISS;
    int     controlPin;                         // PIN (0-999999) de emparejamiento del servicio de control BLE de la app (EA7JRS)
};


class Configuration {
public:

    WiFiAP                  wifiAP;
    FwUpdate                fwUpdate;
    TeamConfig              team;
    KissNetConfig           kissNet;
    CatConfig               cat;
    IaruAutoConfig          iaruAuto;
    NmeaUartConfig          nmeaUart;
    std::vector<Beacon>     beacons;
    Display                 display;
    Battery                 battery;
    Winlink                 winlink;
    Telemetry               telemetry;
    Notification            notification;
    std::vector<LoraType>   loraTypes;
    PTT                     ptt;
    BLUETOOTH               bluetooth;
    CWConfig                cw;
    WSPRConfig              wspr;
    FTConfig                ft;
    LRFHSSConfig            lrfhss;
    DAPNETConfig            dapnet;
    SatConfig               sat;
    RotorConfig             rotor;
    GPSConfig               gps;
    int                     radioPpm1000;       // correccion del oscilador en milesimas de ppm (-50000..50000), global: TX y RX de todos los modos
    int                     operatingMode;      // modo en curso: MODE_APRS_LORA / MODE_APRS_FSK / MODE_MORSE / MODE_WSPR / MODE_LRFHSS / MODE_DAPNET / MODE_FT8 / MODE_FT4 / MODE_FST4W / MODE_SAT (MORSE, WSPR, LRFHSS, FT8, FT4 y FST4W solo si se elige a mano en el arranque)
    int                     savedMode;          // ultimo modo APRS elegido (LoRa o FSK): es el que arranca solo si no se pulsa nada; MORSE nunca se guarda

    bool    simplifiedTrackerMode;
    int     sendCommentAfterXBeacons;
    String  path;
    String  email;
    int     nonSmartBeaconRate;
    int     rememberStationTime;
    int     standingUpdateTime;
    bool    sendAltitude;
    bool    disableGPS;
    bool    cadActive;                  // escuchar antes de transmitir: CAD en LoRa, deteccion de portadora por RSSI en FSK
    int     cadWaitSec;                 // espera maxima por un canal libre (s); pasado el tiempo se transmite igualmente
    int     cadFskRssi;                 // FSK: nivel (dBm) por encima del cual el canal se considera ocupado

    void setDefaultValues();
    void setCwDefaults();
    void setWsprDefaults();
    void setGpsDefaults();
    void setLrfhssDefaults();
    void setFtDefaults();
    void setDapnetDefaults();
    void enforceAisBeacon(Beacon& beacon);              // SSID, simbolo, SmartBeacon y datos AIS coherentes con el tipo de embarcacion
    bool ensureAisProfiles();                           // anade el perfil nautico y el perfil LoRa AIS si faltan (true si cambio algo)
    int  aisLoraIndex() const;                          // -1 si no hay perfil LoRa AIS
    int  aisBeaconIndex() const;                        // -1 si no hay perfil nautico AIS
    bool writeFile();
    Configuration();
    void init();                // montar SPIFFS y leer/crear la configuracion; llamar desde setup()

private:
    bool readFile();
};

#endif