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
    // Lienzo logico de las pantallas APRS, PAGES_W x PAGES_H. Cada placa lo lleva a su pantalla:
    // OLED 128x64 tal cual; pantallas a color (HAS_TFT) 160x80 (26 columnas): la Heltec Wireless Tracker entera y la
    // T-Deck (320x240) ampliado x2 y centrado en vertical.
    #if defined(HAS_TFT)
        #define PAGES_W     160
        #define PAGES_H     80
    #else
        #define PAGES_W     128
        #define PAGES_H     64
    #endif
    // Franjas verticales (Manuel - EA7JRS): las paginas se dibujan en 64 filas logicas; si la pantalla es mas alta,
    // la cabecera queda arriba, la barra de iconos abajo y el cuerpo centrado entre las dos.
    enum PagesZone : uint8_t { PZ_TOP, PZ_BODY, PZ_BOTTOM };
    void    pagesZone(uint8_t zone);                                        // vale hasta el siguiente pagesZone / pagesBegin (PZ_BODY)
    // Tintas (pantallas a color): el "on" de los dibujos sale con el color de la tinta en uso. En monocromo no hacen nada.
    enum PagesInk : uint8_t { INK_TEXT, INK_HEAD, INK_ACCENT, INK_SEL, INK_FRAME, INK_MUTED, INK_GOOD, INK_WARN, INK_BAD,
                             INK_SUN, INK_MOON, INK_SHADE, INK_NIGHT, INK_COUNT };   // sol, luna iluminada, cara oscura, arco de la noche
    void    pagesInk(uint8_t ink);                                          // vale hasta el siguiente pagesInk / pagesBegin (INK_TEXT)
    bool    pagesHasColor();
    void    pagesSetLight(bool light);                                      // tema claro (true) u oscuro
    void    pagesBegin();                                                   // borra el lienzo
    void    pagesFlush();                                                   // lo envia a la pantalla
    void    pagesFillRect(int x, int y, int w, int h, bool on);
    void    pagesDrawRect(int x, int y, int w, int h, bool on);
    void    pagesDrawLine(int x0, int y0, int x1, int y1, bool on);
    void    pagesDrawCircle(int x, int y, int r, bool on);
    void    pagesFillCircle(int x, int y, int r, bool on);
    void    pagesFillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, bool on);
    void    pagesBitmap2(int x, int y, int w, int h, const uint8_t* data);        // 2 bits por pixel (0 fondo, 1..3 capas); en color cada capa con su color
    void    pagesText(int x, int y, const char* text, uint8_t size, bool inverted);   // fuente 6x8 por tamano; inverted = fondo sobre la tinta
    void    pagesImage565(int x, int y, int w, int h, const uint16_t* data);     // imagen RGB565 (solo pantallas a color)
    // Icono de paleta (hasta 15 colores RGB565 con opacidad; pixel 0 = transparente; 4 bits por pixel, el izquierdo en los bits
    // altos), fundido con el fondo o, con onInk, con el color de la tinta en uso (filas seleccionadas). Solo pantallas a color.
    void    pagesIcon4(int x, int y, int size, const uint16_t* colors, const uint8_t* alphas, const uint8_t* pixels, bool onInk);
#endif

void startupScreen(uint8_t index, const String& version, const String& callsign = "");

#endif