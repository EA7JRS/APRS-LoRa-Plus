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

#ifndef DISPLAY_H_
#define DISPLAY_H_

#include <Arduino.h>
#include "board_pinout.h"

// Pantallas APRS (menu 7.Screens): disponibles en todas las placas con pantalla (OLED o TFT).
// Para quitarlas en una placa concreta: #define NO_APRS_PAGES en su board_pinout.h
#if !defined(HELTEC_WSL_V3_TNC) && !defined(NO_APRS_PAGES)
    #define HAS_APRS_PAGES
#endif


void displaySetup();
void displayToggle(bool toggle);

void displayShow(const String& header, const String& line1, const String& line2, int wait = 0);
void displayShow(const String& header, const String& line1, const String& line2, const String& line3, const String& line4, const String& line5, int wait = 0);

// Aviso en ventana emergente pequena: titulo, 2 lineas y barra de progreso (percent < 0 = sin barra)
void displayPopup(const String& title, const String& line1, const String& line2, int percent = -1, int wait = 0);

#ifdef HAS_APRS_PAGES
    // Lienzo logico monocromo de 128x64 para las pantallas APRS. Cada placa lo lleva a su pantalla:
    // OLED 128x64 tal cual; Heltec Wireless Tracker (160x80) centrado; T-Deck (320x240) ampliado x2.
    void    pagesBegin();                                                   // borra el lienzo
    void    pagesFlush();                                                   // lo envia a la pantalla
    void    pagesFillRect(int x, int y, int w, int h, bool on);
    void    pagesDrawRect(int x, int y, int w, int h, bool on);
    void    pagesDrawLine(int x0, int y0, int x1, int y1, bool on);
    void    pagesDrawCircle(int x, int y, int r, bool on);
    void    pagesFillCircle(int x, int y, int r, bool on);
    void    pagesFillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, bool on);
    void    pagesText(int x, int y, const char* text, uint8_t size, bool inverted);   // fuente 6x8 por tamano; inverted = negro sobre blanco
#endif

void startupScreen(uint8_t index, const String& version, const String& callsign = "");

#endif