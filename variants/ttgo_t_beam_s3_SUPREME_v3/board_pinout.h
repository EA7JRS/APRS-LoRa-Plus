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

#ifndef BOARD_PINOUT_H_
#define BOARD_PINOUT_H_

    //  LoRa Radio
    #define HAS_SX1262
    #define HAS_TCXO
    #define RADIO_SCLK_PIN      12
    #define RADIO_MISO_PIN      13
    #define RADIO_MOSI_PIN      11
    #define RADIO_CS_PIN        10
    #define RADIO_DIO0_PIN      -1
    #define RADIO_RST_PIN       5
    #define RADIO_DIO1_PIN      1
    #define RADIO_BUSY_PIN      4

    //  Display
    #undef  OLED_SDA
    #undef  OLED_SCL
    #undef  OLED_RST

    #define OLED_SDA            17
    #define OLED_SCL            18
    #define OLED_RST            -1      // la Supreme no tiene pin RST de OLED

    //  GPS
    #define HAS_GPS_CTRL
    #define GPS_RX              8
    #define GPS_TX              9
    #define GPS_BAUDRATE        9600    // el M10 de esta placa se detecta a 9600 baudios
    #define GPS_CONFIG_CONSTELLATIONS       // configura constelaciones del u-blox M10 por UBX-CFG-VALSET
    #define GPS_USE_GPS         1
    #define GPS_USE_GALILEO     1
    #define GPS_USE_BEIDOU      0
    #define GPS_USE_GLONASS     0
    #define GPS_USE_QZSS        0       // QZSS desactivado
    #define GPS_USE_SBAS        1       // SBAS: WAAS (América) + EGNOS (Europa), según el satélite visible
    #define GPS_USE_ASSISTNOW_AUTONOMOUS    1   // A-GPS offline (AssistNow Autonomous) del propio M10
    #define GPS_SPEED_FILTER                    // velocidad/rumbo estables con la unidad parada
    #define GPS_LAST_POSITION                   // guarda y muestra la ultima posicion valida tras reiniciar
    #define WX_IMPROVED                         // BME280: presion a nivel del mar exacta, humedad coherente con la correccion de temperatura
    #define GPS_BARO_ALTITUDE                   // altitud fusionada barometro + GPS
    #define GPS_COORD_DECIMALS 5                // decimales de lat/lon en pantalla (5 = ~1,1 m; 4 = ~11 m)

    // Salida NMEA 0183 / NMEA 2000 (GNSS, meteorologia y estaciones APRS oidas como objetivos AIS)
    #define NMEA_OUTPUT
    #define NMEA_USB                1           // por el puerto USB (frases NMEA mezcladas con el log)
    #define NMEA_TCP                0           // servidor TCP (activa el WiFi de forma permanente: consume bateria)
    #define NMEA_TCP_PORT           10110
    #define NMEA_WIFI_SSID          ""          // vacio: crea el punto de acceso "LoRaTracker-NMEA"; con valor: se une a esa red
    #define NMEA_WIFI_PASSWORD      ""
    #define NMEA_UART_TX_PIN        -1          // pin TX por defecto para un conversor TTL->RS422 (plotter); -1 = desactivado. Se cambia en la WebUI (Hardware > NMEA output); sugerido: 43 (TXD de la cabecera)
    #define NMEA_UART_BAUD          38400       // 38400 es la velocidad de AIS
    #define NMEA_OUT_GNSS           1           // RMC/GGA/VTG/GLL/ZDA propios; ponlo a 0 para un plotter Garmin por NMEA 0183 (no los acepta)
    #define NMEA_OUT_ENV            1           // MDA y XDR (el plotter acepta MDA)
    #define NMEA_OUT_AIS            1           // !AIVDM de las estaciones APRS (el plotter acepta VDM)
    #define NMEA_N2K_SEASMART       0           // NMEA 2000 como $PCDIN (pasarelas SeaSmart)
    #define NMEA_N2K_ACTISENSE      0           // NMEA 2000 como Actisense N2K ASCII
    #define NMEA_AIS_MID            224         // MID del MMSI sintetico (224 = Espana)
    #define GPS_STATIC_HOLD_CMS 50              // static hold del M10 (cm/s); 0 = desactivado
    #define GPS_STATIC_HOLD_DIST_M 15           // distancia para salir de static hold (m)

    //  Pantallas APRS (menu 7.Screens): activas en todas las placas con pantalla (ver display.h)
    #define PAGES_GYRO_SIGN     -1              // sentido inicial del giroscopio para la brujula; se corrige solo al girar

    //  Sensores (según documentación LilyGO)
    #define SD_CS_PIN           47      // la SD comparte el bus SPI con el IMU
    #define IMU_CS_PIN          34      // QMI8658 (SPI)
    #define IMU_INT_PIN         33
    #define IMU_SCK_PIN         36
    #define IMU_MISO_PIN        37
    #define IMU_MOSI_PIN        35
    #define RTC_INT_PIN         14      // RTC en I2C1 (0x51) junto al PMU
    #define MAG_HEADING_OFFSET_DEG  0   // ajuste de la orientacion de la brujula respecto al norte
    #define MAG_HEADING_INVERT      0   // 1 si el sentido de giro sale invertido

    //  OTHER
    #define BUTTON_PIN          0

    #define HAS_AXP2101
    #ifndef BOARD_HAS_PSRAM
        #define BOARD_HAS_PSRAM
    #endif

#endif