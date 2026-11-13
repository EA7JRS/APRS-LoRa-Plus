/* Copyright (C) 2026 Manuel - EA7JRS
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

#ifndef PRODUCT_H_
#define PRODUCT_H_

// Nombre del equipo segun el firmware. APRS_MAPRS_TRANSPONDER: «APRS LoRa+ MAPRS transponder» (Manuel, 2026-11-13), transpondedor
// AIS por APRS LoRa (perfil nautico AIS) en la LilyGO T3 v1.6.1 (LoRa32 V2.1, 433 MHz), entorno ttgo-lora32-v21_MAPRS.
#ifdef APRS_MAPRS_TRANSPONDER
    #define PRODUCT_NAME        "APRS LoRa+ MAPRS transponder"
    #define PRODUCT_SHORT       "MAPRS"                 // debajo de «APRS LoRa+» en la bienvenida
    #define PRODUCT_AP_SSID     "APRSLoRa+MAPRS"        // punto de acceso de la WebUI
    #define PRODUCT_BT_NAME     "APRSLoRa+MAPRS"        // nombre Bluetooth de serie
#else
    #define PRODUCT_NAME        "APRS LoRa+ tracker"
    #define PRODUCT_SHORT       ""
    #define PRODUCT_AP_SSID     "APRSLoRa+tracker"
    #define PRODUCT_BT_NAME     "APRSLoRa+tracker"
#endif

#endif
