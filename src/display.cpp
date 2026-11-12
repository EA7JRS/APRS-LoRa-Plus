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

#include <logger.h>
#include <Wire.h>
#include "custom_characters.h"
#include "custom_colors.h"
#include "configuration.h"
#include "station_utils.h"
#include "board_pinout.h"
#include "display.h"
#include "pages_utils.h"
#include "TimeLib.h"


#ifdef HAS_TFT
    #include <TFT_eSPI.h>

    TFT_eSPI    tft     = TFT_eSPI();
    TFT_eSprite sprite  = TFT_eSprite(&tft);

    #ifdef HELTEC_WIRELESS_TRACKER
        #define bigSizeFont     2
        #define smallSizeFont   1
        #define lineSpacing     12
        #define maxLineLength   26
    #endif
    #if defined(TTGO_T_DECK_GPS) || defined(TTGO_T_DECK_PLUS)
        #define color1  TFT_BLACK
        #define color2  0x0249
        #define green   0x1B08

        #define bigSizeFont     4
        #define normalSizeFont  2
        #define smallSizeFont   1
        #define lineSpacing     20
        #define maxLineLength   22

        extern String topHeader1;
        extern String topHeader1_1;
        extern String topHeader1_2;
        extern String topHeader1_3;
        extern String topHeader2;
    #endif
#else
    #include <Adafruit_GFX.h>

    #define ssd1306 //comment this line with "//" when using SH1106 screen instead of SSD1306

    #if defined(TTGO_T_Beam_S3_SUPREME_V3) || defined(TTGO_T_BEAM_1W) || defined(LILYGO_T_TWR_PLUS)
        #undef ssd1306
    #endif
    #if defined(HELTEC_V3_GPS) || defined(HELTEC_V3_TNC) || defined(HELTEC_V3_2_GPS) || defined(HELTEC_V3_2_TNC)
        #define OLED_DISPLAY_HAS_RST_PIN
    #endif

    #ifndef HELTEC_WSL_V3_TNC
        #ifdef ssd1306
            #include <Adafruit_SSD1306.h>
            Adafruit_SSD1306 display(128, 64, &Wire, OLED_RST);
        #else
            #include <Adafruit_SH110X.h>
            Adafruit_SH1106G display(128, 64, &Wire, OLED_RST);
        #endif
    #endif
#endif

#define SYMBOL_HEIGHT 14
#define SYMBOL_WIDTH  16

extern Configuration    Config;
extern Beacon           *currentBeacon;
extern int              menuDisplay;
extern bool             bluetoothConnected;

const char* symbolArray[]     = { "[", ">", "j", "b", "<", "s", "u", "R", "v", "(", ";", "-", "k",
                                "C", "a", "Y", "O", "'", "=", "y", "U", "p", "_", ")"};
int   symbolArraySize         = sizeof(symbolArray)/sizeof(symbolArray[0]);
const uint8_t *symbolsAPRS[]  = {runnerSymbol, carSymbol, jeepSymbol, bikeSymbol, motorcycleSymbol, shipSymbol,
                                truck18Symbol, recreationalVehicleSymbol, vanSymbol, carsateliteSymbol, tentSymbol,
                                houseSymbol, truckSymbol, canoeSymbol, ambulanceSymbol, yatchSymbol, baloonSymbol,
                                aircraftSymbol, trainSymbol, yagiSymbol, busSymbol, dogSymbol, wxSymbol, wheelchairSymbol};
// T-Beams bought with soldered OLED Screen comes with only 4 pins (VCC, GND, SDA, SCL)
// If your board didn't come with 4 pins OLED Screen and comes with 5 and one of them is RST...
// Uncomment Next Line (Remember ONLY if your OLED Screen has a RST pin). This is to avoid memory issues.
//#define OLED_DISPLAY_HAS_RST_PIN

int         lastMenuDisplay         = 0;
uint8_t     screenBrightness        = 1;    //from 1 to 255 to regulate brightness of screens
bool        symbolAvailable         = true;

extern logging::Logger logger;


#if defined(HAS_TFT) && (defined(TTGO_T_DECK_GPS) || defined(TTGO_T_DECK_PLUS))
    void drawButton(int xPos, int yPos, int wide, int height, String buttonText, int color) {
        uint16_t baseColor, lightColor, darkColor;
        switch (color) {
            case 0:     // Grey Theme
                baseColor   = greyColor;
                lightColor  = greyColorLight;
                darkColor   = greyColorDark;
                break;
            case 1:     // Green Theme
                baseColor   = greenColor;
                lightColor  = greenColorLight;
                darkColor   = greenColorDark;
                break;
            case 2:     // Red Theme
                baseColor   = redColor;
                lightColor  = redColorLight;
                darkColor   = redColorDark;
                break;
            default:    // Fallback color
                baseColor   = 0x0000;   // Black
                lightColor  = 0xFFFF;   // White
                darkColor   = 0x0000;   // Black
                break;
        }

        sprite.fillRect(xPos, yPos, wide, height, baseColor);           // Dibuja el fondo del botón
        sprite.fillRect(xPos, yPos + height - 2, wide, 2, darkColor);   // Línea inferior
        sprite.fillRect(xPos, yPos, wide, 2, lightColor);               // Línea superior
        sprite.fillRect(xPos, yPos, 2, height, lightColor);             // Línea izquierda
        sprite.fillRect(xPos + wide - 2, yPos, 2, height, darkColor);   // Línea derecha

        sprite.setTextSize(2);
        sprite.setTextColor(TFT_WHITE, baseColor);

        // Calcula la posición del texto para que esté centrado
        int textWidth = sprite.textWidth(buttonText);           // Ancho del texto
        int textHeight = 16;                                    // Altura aproximada (depende de `setTextSize`)
        int textX = xPos + (wide - textWidth) / 2;              // Centrado horizontal
        int textY = yPos + (height - textHeight) / 2;           // Centrado vertical

        sprite.drawString(buttonText, textX, textY);
    }

    void draw_T_DECK_Top() {
        sprite.fillSprite(TFT_BLACK);
        sprite.fillRect(0, 0, 320, 38, redColor);
        sprite.setTextFont(0);
        sprite.setTextSize(bigSizeFont);
        sprite.setTextColor(TFT_WHITE, redColor);
        sprite.drawString(topHeader1, 3, 5);

        sprite.setTextSize(smallSizeFont);
        sprite.setTextColor(TFT_WHITE, redColor);
        sprite.drawString(topHeader1_1, 258, 5);
        sprite.drawString("UTC:" + topHeader1_2, 246, 15);

        sprite.fillRect(0, 38, 320, 2, redColorDark);

        sprite.fillRect(0, 40, 320, 2, greyColorLight);
        sprite.fillRect(0, 42, 320, 20, greyColor);
        sprite.setTextSize(2);
        sprite.setTextColor(TFT_WHITE, greyColor);
        sprite.drawString(topHeader2, 8, 44);
        sprite.fillRect(0, 60, 320, 2, greyColorDark);
    }

    void draw_T_DECK_MenuButtons(int menu) {
        int ladoCuadrado            = 45;
        int curvaCuadrado           = 8;
        int espacioEntreCuadrados   = 18;
        int margenLineaCuadrados    = 10;
        int alturaPrimeraLinea      = 75;
        int alturaSegundaLinea      = 145;
        int16_t colorCuadrados      = 0x2925;
        int16_t colorDestacado      = greyColor;

        for (int i = 0; i < 5; i++) {
            if (i == menu - 1) {
                sprite.fillRoundRect(
                    margenLineaCuadrados + (i * (ladoCuadrado + espacioEntreCuadrados)) - 1,
                    alturaPrimeraLinea - 1,
                    ladoCuadrado + 2,
                    ladoCuadrado + 2,
                    curvaCuadrado,
                    TFT_WHITE
                );
                sprite.fillRoundRect(
                    margenLineaCuadrados + (i * (ladoCuadrado + espacioEntreCuadrados)),
                    alturaPrimeraLinea,
                    ladoCuadrado,
                    ladoCuadrado,
                    curvaCuadrado,
                    TFT_BLACK
                );
                sprite.fillRoundRect(
                    margenLineaCuadrados + (i * (ladoCuadrado + espacioEntreCuadrados)),    // x-coordinate
                    alturaPrimeraLinea,                                                     // y-coordinate
                    ladoCuadrado,                                                           // width
                    ladoCuadrado,                                                           // height
                    curvaCuadrado,                                                          // corner radius
                    colorDestacado                                                          // color
                );
            } else {
                sprite.fillRoundRect(
                    margenLineaCuadrados + (i * (ladoCuadrado + espacioEntreCuadrados)),    // x-coordinate
                    alturaPrimeraLinea,                                                     // y-coordinate
                    ladoCuadrado,                                                           // width
                    ladoCuadrado,                                                           // height
                    curvaCuadrado,                                                          // corner radius
                    colorCuadrados                                                          // color
                );
            }
            sprite.fillRoundRect(
                margenLineaCuadrados + (i * (ladoCuadrado + espacioEntreCuadrados)),    // x-coordinate
                alturaSegundaLinea,                                                     // y-coordinate
                ladoCuadrado,                                                           // width
                ladoCuadrado,                                                           // height
                curvaCuadrado,                                                          // corner radius
                colorCuadrados                                                          // color
            );
        }
    }

#endif

void displaySetup() {
    delay(500);
    STATION_Utils::loadIndex(2);    // Screen Brightness value
    #ifdef HAS_TFT
        tft.init();
        tft.begin();
        if (Config.display.turn180) {
                tft.setRotation(3);
        } else {
            tft.setRotation(1);
        }
        pinMode(TFT_BL, OUTPUT);
        analogWrite(TFT_BL, screenBrightness);
        tft.setTextFont(0);
        tft.fillScreen(TFT_BLACK);
        #if defined(TTGO_T_DECK_GPS) || defined(TTGO_T_DECK_PLUS)
            sprite.createSprite(320,240);
        #else
            sprite.createSprite(160,80);
        #endif
    #else
        #ifdef OLED_DISPLAY_HAS_RST_PIN
            pinMode(OLED_RST, OUTPUT);
            digitalWrite(OLED_RST, LOW);
            delay(20);
            digitalWrite(OLED_RST, HIGH);
        #endif

        Wire.begin(OLED_SDA, OLED_SCL);     // also used by I2C peripherals scanner
        #ifndef HELTEC_WSL_V3_TNC
            #ifdef ssd1306
                if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3c, false, false)) {
                    logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "SSD1306", "allocation failed!");
                    while (true) {}
                }
            #else
                uint8_t oledAddr = 0x3c;
                #ifdef LILYGO_T_TWR_PLUS                // la pantalla de las T-TWR de UHF esta en 0x3D
                    Wire.beginTransmission(0x3d);
                    if (Wire.endTransmission() == 0) oledAddr = 0x3d;
                #endif
                if (!display.begin(oledAddr, false)) {
                    logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "SH1106", "allocation failed!");
                    while (true) {}
                }
            #endif
            if (Config.display.turn180) display.setRotation(2);
            display.clearDisplay();
            #ifdef ssd1306
                display.setTextColor(WHITE);
            #else
                display.setTextColor(SH110X_WHITE);
            #endif
            display.setTextSize(1);
            display.setCursor(0, 0);
            #ifdef ssd1306
                display.ssd1306_command(SSD1306_SETCONTRAST);
                display.ssd1306_command(screenBrightness);
            #else
                display.setContrast(screenBrightness);
            #endif
            display.display();
        #endif
    #endif
}

void displayToggle(bool toggle) {
    if (toggle) {
        #ifdef HAS_TFT
            analogWrite(TFT_BL, screenBrightness);
        #elif !defined(HELTEC_WSL_V3_TNC)
            #ifdef ssd1306
                display.ssd1306_command(SSD1306_DISPLAYON);
            #else
                display.oled_command(SH110X_DISPLAYON);
            #endif
        #endif
    } else {
        #ifdef HAS_TFT
            analogWrite(TFT_BL, 0);
        #elif !defined(HELTEC_WSL_V3_TNC)
            #ifdef ssd1306
                display.ssd1306_command(SSD1306_DISPLAYOFF);
            #else
                display.oled_command(SH110X_DISPLAYOFF);
            #endif
        #endif
    }
}

void displayShow(const String& header, const String& line1, const String& line2, int wait) {
    #ifdef HAS_TFT
        #if defined(TTGO_T_DECK_GPS) || defined(TTGO_T_DECK_PLUS)
            draw_T_DECK_Top();

            sprite.setTextSize(normalSizeFont);
            sprite.setTextColor(TFT_WHITE, TFT_BLACK);

            const String* const lines[] = {&header, &line1, &line2};
            int yLineOffset = 70;

            for (int i = 0; i < 3; i++) {
                String text = *lines[i];
                if (text.length() > 0) {
                    while (text.length() > 0) {
                        String chunk = text.substring(0, maxLineLength);
                        sprite.drawString(chunk, 35, yLineOffset);
                        text = text.substring(maxLineLength);
                        yLineOffset += lineSpacing;
                    }
                } else {
                    sprite.drawString(text, 3, yLineOffset);
                    yLineOffset += lineSpacing;
                }
            }
        #endif
        #if defined(HELTEC_WIRELESS_TRACKER)
            sprite.fillSprite(TFT_BLACK);
            sprite.fillRect(0, 0, 160, 19, TFT_YELLOW);
            sprite.setTextFont(0);
            sprite.setTextSize(bigSizeFont);
            sprite.setTextColor(TFT_BLACK, TFT_YELLOW);
            sprite.drawString(header, 3, 3);

            const String* const lines[] = {&line1, &line2};

            sprite.setTextSize(smallSizeFont);
            sprite.setTextColor(TFT_WHITE, TFT_BLACK);

            int yLineOffset = (lineSpacing * 2) - 2;

            for (int i = 0; i < 2; i++) {
                String text = *lines[i];
                if (text.length() > 0) {
                    while (text.length() > 0) {
                        String chunk = text.substring(0, maxLineLength);
                        sprite.drawString(chunk, 3, yLineOffset);
                        text = text.substring(maxLineLength);
                        yLineOffset += lineSpacing;
                    }
                } else {
                    sprite.drawString(text, 3, yLineOffset);
                    yLineOffset += lineSpacing;
                }
            }
        #endif
        sprite.pushSprite(0,0);
    #elif !defined(HELTEC_WSL_V3_TNC)
        const String* const lines[] = {&line1, &line2};

        display.clearDisplay();
        #ifdef ssd1306
            display.setTextColor(WHITE);
        #else
            display.setTextColor(SH110X_WHITE);
        #endif
        display.setTextSize(2);
        display.setCursor(0, 0);
        display.println(header);
        display.setTextSize(1);
        for (int i = 0; i < 2; i++) {
            display.setCursor(0, 16 + (10 * i));
            display.println(*lines[i]);
        }
        #ifdef ssd1306
            display.ssd1306_command(SSD1306_SETCONTRAST);
            display.ssd1306_command(screenBrightness);
        #else
            display.setContrast(screenBrightness);
        #endif
        display.display();
    #endif
    #ifndef HELTEC_WSL_V3_TNC
        delay(wait);
    #endif
}

// Ventana emergente pequena (titulo + 2 lineas + barra de progreso opcional) centrada en la pantalla. Cada texto se recorta a lo que cabe.
// Copyright (C) 2026 Manuel - EA7JRS
void displayPopup(const String& title, const String& line1, const String& line2, int percent, int wait) {
    #ifndef HELTEC_WSL_V3_TNC
        #if defined(HAS_TFT) && (defined(TTGO_T_DECK_GPS) || defined(TTGO_T_DECK_PLUS))
            const int sz = 2;
        #else
            const int sz = 1;
        #endif
        const int cw = 6 * sz;                                    // ancho de un caracter
        const int lineH = 8 * sz + 3;
        const bool bar = percent >= 0;
        #ifdef HAS_TFT
            const int W = sprite.width(), H = sprite.height();
        #else
            const int W = display.width(), H = display.height();
        #endif
        const int boxW = W - 16;
        const int boxH = (lineH + 2) + 2 * lineH + (bar ? 12 : 4) + 2;
        const int x0 = (W - boxW) / 2, y0 = (H - boxH) / 2;
        const unsigned maxChars = (unsigned)((boxW - 8) / cw);
        const String texts[3] = { title.substring(0, maxChars), line1.substring(0, maxChars), line2.substring(0, maxChars) };
        const int pct = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
        const int barY = y0 + (lineH + 2) + 2 * lineH + 2;
        #ifdef HAS_TFT
            sprite.fillSprite(TFT_BLACK);
            sprite.setTextFont(0);
            sprite.setTextSize(sz);
            sprite.drawRoundRect(x0, y0, boxW, boxH, 3, TFT_YELLOW);
            sprite.fillRect(x0, y0, boxW, lineH + 2, TFT_YELLOW);
            sprite.setTextColor(TFT_BLACK, TFT_YELLOW);
            sprite.drawString(texts[0], x0 + (boxW - (int)texts[0].length() * cw) / 2, y0 + 3);
            sprite.setTextColor(TFT_WHITE, TFT_BLACK);
            for (int i = 1; i < 3; i++) sprite.drawString(texts[i], x0 + (boxW - (int)texts[i].length() * cw) / 2, y0 + (lineH + 2) + (i - 1) * lineH + 3);
            if (bar) {
                sprite.drawRect(x0 + 6, barY, boxW - 12, 8, TFT_WHITE);
                sprite.fillRect(x0 + 8, barY + 2, ((boxW - 16) * pct) / 100, 4, TFT_WHITE);
            }
            sprite.pushSprite(0, 0);
        #else
            #ifdef ssd1306
                const uint16_t cWhite = WHITE, cBlack = BLACK;
            #else
                const uint16_t cWhite = SH110X_WHITE, cBlack = SH110X_BLACK;
            #endif
            display.clearDisplay();
            display.setTextSize(1);
            display.drawRoundRect(x0, y0, boxW, boxH, 3, cWhite);
            display.fillRect(x0, y0, boxW, lineH + 2, cWhite);
            display.setTextColor(cBlack);
            display.setCursor(x0 + (boxW - (int)texts[0].length() * cw) / 2, y0 + 3);
            display.print(texts[0]);
            display.setTextColor(cWhite);
            for (int i = 1; i < 3; i++) {
                display.setCursor(x0 + (boxW - (int)texts[i].length() * cw) / 2, y0 + (lineH + 2) + (i - 1) * lineH + 3);
                display.print(texts[i]);
            }
            if (bar) {
                display.drawRect(x0 + 6, barY, boxW - 12, 8, cWhite);
                display.fillRect(x0 + 8, barY + 2, ((boxW - 16) * pct) / 100, 4, cWhite);
            }
            #ifdef ssd1306
                display.ssd1306_command(SSD1306_SETCONTRAST);
                display.ssd1306_command(screenBrightness);
            #else
                display.setContrast(screenBrightness);
            #endif
            display.display();
        #endif
        delay(wait);
    #else
        (void)title; (void)line1; (void)line2; (void)percent; (void)wait;
    #endif
}

#ifndef HELTEC_WSL_V3_TNC
void drawSymbol(int symbolIndex, bool bluetoothActive) {
    const uint8_t *bitMap = symbolsAPRS[symbolIndex];
    #ifdef HAS_TFT
        if (bluetoothActive) bitMap = bluetoothSymbol;
        #if defined(HELTEC_WIRELESS_TRACKER)
            sprite.drawBitmap(128 - SYMBOL_WIDTH, 3, bitMap, SYMBOL_WIDTH, SYMBOL_HEIGHT, TFT_WHITE);
        #endif
        #if defined(TTGO_T_DECK_GPS) || defined(TTGO_T_DECK_PLUS)
            sprite.drawBitmap(280, 70, bitMap, SYMBOL_WIDTH, SYMBOL_HEIGHT, TFT_WHITE);
        #endif
    #else
        display.drawBitmap((display.width() - SYMBOL_WIDTH), 0, bitMap, SYMBOL_WIDTH, SYMBOL_HEIGHT, 1);
    #endif
}
#endif

void displayShow(const String& header, const String& line1, const String& line2, const String& line3, const String& line4, const String& line5, int wait) {
    #ifdef HAS_TFT
        #if defined(TTGO_T_DECK_GPS) || defined(TTGO_T_DECK_PLUS)
            draw_T_DECK_Top();
            sprite.setTextSize(normalSizeFont);
            sprite.setTextColor(TFT_WHITE, TFT_BLACK);

            const String* const lines[] = {&header, &line1, &line2, &line3, &line4, &line5};
            int yLineOffset = 70;

            for (int i = 0; i < 6; i++) {
                String text = *lines[i];
                if (text.length() > 0) {
                    while (text.length() > 0) {
                        String chunk = text.substring(0, maxLineLength);
                        sprite.drawString(chunk, 35, yLineOffset);
                        text = text.substring(maxLineLength);
                        yLineOffset += lineSpacing;
                    }
                } else {
                    sprite.drawString(text, 3, yLineOffset);
                    yLineOffset += lineSpacing;
                }
            }

            drawButton(30,  210, 80, 28, "Send", 1);
            drawButton(125, 210, 80, 28, "Menu", 0);
            drawButton(220, 210, 80, 28, "Exit", 2);
        #endif
        #if defined(HELTEC_WIRELESS_TRACKER)
            sprite.fillSprite(TFT_BLACK);
            sprite.fillRect(0, 0, 160, 19, redColor);
            sprite.setTextFont(0);
            sprite.setTextSize(bigSizeFont);
            sprite.setTextColor(TFT_WHITE, redColor);
            sprite.drawString(header, 3, 3);

            const String* const lines[] = {&line1, &line2, &line3, &line4, &line5};

            sprite.setTextSize(smallSizeFont);
            sprite.setTextColor(TFT_WHITE, TFT_BLACK);

            int yLineOffset = (lineSpacing * 2) - 2;

            for (int i = 0; i < 5; i++) {
                String text = *lines[i];
                if (text.length() > 0) {
                    while (text.length() > 0) {
                        String chunk = text.substring(0, maxLineLength);
                        sprite.drawString(chunk, 3, yLineOffset);
                        text = text.substring(maxLineLength);
                        yLineOffset += lineSpacing;
                    }
                } else {
                    sprite.drawString(text, 3, yLineOffset);
                    yLineOffset += lineSpacing;
                }
            }
        #endif
            if (menuDisplay == 0 && Config.display.showSymbol) {
                int symbol = 100;
                for (int i = 0; i < symbolArraySize; i++) {
                    if (currentBeacon->symbol == symbolArray[i]) {
                        symbol = i;
                        break;
                    }
                }

                symbolAvailable = symbol != 100;

                /*  Symbol alternate every 5s
                *   If bluetooth is disconnected or if we are in the first part of the clock, then we show the APRS symbol
                *   Otherwise, we are in the second part of the clock, then we show BT connected */

                const auto time_now = now();
                if (!bluetoothConnected || time_now % 10 < 5) {
                    if (symbolAvailable) drawSymbol(symbol, false);
                } else if (bluetoothConnected) {    // TODO In this case, the text symbol stay displayed due to symbolAvailable false in menu_utils
                    drawSymbol(symbol, true);
                }
            }
        sprite.pushSprite(0,0);
    #elif !defined(HELTEC_WSL_V3_TNC)
        const String* const lines[] = {&line1, &line2, &line3, &line4, &line5};

        display.clearDisplay();
        #ifdef ssd1306
            display.setTextColor(WHITE);
            display.drawLine(0, 16, 128, 16, WHITE);
            display.drawLine(0, 17, 128, 17, WHITE);
        #else
            display.setTextColor(SH110X_WHITE);
            display.drawLine(0, 16, 128, 16, SH110X_WHITE);
            display.drawLine(0, 17, 128, 17, SH110X_WHITE);
        #endif
        display.setTextSize(2);
        display.setCursor(0, 0);
        display.println(header);
        display.setTextSize(1);
        for (int i = 0; i < 5; i++) {
            display.setCursor(0, 20 + (9 * i));
            display.println(*lines[i]);
        }
        #ifdef ssd1306
            display.ssd1306_command(SSD1306_SETCONTRAST);
            display.ssd1306_command(screenBrightness);
        #else
            display.setContrast(screenBrightness);
        #endif

        if (menuDisplay == 0 && Config.display.showSymbol) {
            int symbol = 100;
            for (int i = 0; i < symbolArraySize; i++) {
                if (currentBeacon->symbol == symbolArray[i]) {
                    symbol = i;
                    break;
                }
            }

            symbolAvailable = symbol != 100;

            /*
            * Symbol alternate every 5s
            * If bluetooth is disconnected or if we are in the first part of the clock, then we show the APRS symbol
            * Otherwise, we are in the second part of the clock, then we show BT connected
            */
            const auto time_now = now();
            if (!bluetoothConnected || time_now % 10 < 5) {
                if (symbolAvailable) drawSymbol(symbol, false);
            } else if (bluetoothConnected) {    // TODO In this case, the text symbol stay displayed due to symbolAvailable false in menu_utils
                drawSymbol(symbol, true);
            }
        }
        display.display();
    #endif
    #ifndef HELTEC_WSL_V3_TNC
        delay(wait);
    #endif
}

#ifdef HAS_APRS_PAGES
// ---------- Lienzo de las pantallas APRS ----------
#ifdef HAS_TFT
    #if defined(TTGO_T_DECK_GPS) || defined(TTGO_T_DECK_PLUS)    // 160x80 x2 = 320x160, centrado en los 240 px de alto (EA7JRS)
        #define PAGES_SCALE     2
        #define PAGES_OFFSET_X  0
        #define PAGES_OFFSET_Y  ((240 - PAGES_H * 2) / 2)
        #define PAGES_BODY_DY   ((PAGES_H - 64) / 2)
    #else   // Heltec Wireless Tracker: lienzo de 160x80 entero; el cuerpo de 64 filas, 8 px mas abajo (Manuel - EA7JRS)
        #define PAGES_SCALE     1
        #define PAGES_OFFSET_X  0
        #define PAGES_OFFSET_Y  0
        #define PAGES_BODY_DY   ((PAGES_H - 64) / 2)
    #endif
    // Paletas de la app Android (Theme.kt): oscura y clara. RGB565.
    #define RGB565(c) ((uint16_t)((((c) >> 8) & 0xF800) | (((c) >> 5) & 0x07E0) | (((c) >> 3) & 0x001F)))
    struct PagesPalette { uint16_t bg, onInk, ink[INK_COUNT], logo[3]; };
    static const PagesPalette PAL_DARK = {
        RGB565(0x131A22), RGB565(0x131A22),
        { RGB565(0xE8EEF3), RGB565(0x28B5E1), RGB565(0xFFB347), RGB565(0x3CE68A), RGB565(0x28B5E1),
          RGB565(0x9FB0BF), RGB565(0x3CE68A), RGB565(0xFFB347), RGB565(0xFF6B6B),
          RGB565(0xFFB347), RGB565(0xF2EBD0), RGB565(0x2B3A4A), RGB565(0x3A4A5C), RGB565(0xFFD84D), RGB565(0xFF9A3C) },
        { RGB565(0x3CE68A), RGB565(0x28B5E1), RGB565(0xE8EEF3) } };
    static const PagesPalette PAL_LIGHT = {
        RGB565(0xF4F7FA), RGB565(0xFFFFFF),
        { RGB565(0x131A22), RGB565(0x1B8DB8), RGB565(0xB05E00), RGB565(0x1E9E5C), RGB565(0x1B8DB8),
          RGB565(0x5C6B7A), RGB565(0x1E9E5C), RGB565(0xB05E00), RGB565(0xC83A3A),
          RGB565(0xE08A00), RGB565(0xE8C45A), RGB565(0x3A4654), RGB565(0x9AA8B6), RGB565(0xB89400), RGB565(0xC86400) },
        { RGB565(0x1E9E5C), RGB565(0x1B8DB8), RGB565(0x131A22) } };
    static const PagesPalette* pagesPal = &PAL_DARK;
    static uint16_t pagesInkColor = PAL_DARK.ink[INK_TEXT];
    static int      pagesDy = PAGES_BODY_DY;

    #define PAGES_X(v)  (PAGES_OFFSET_X + (v) * PAGES_SCALE)
    #define PAGES_Y(v)  (PAGES_OFFSET_Y + ((v) + pagesDy) * PAGES_SCALE)
    #define PAGES_COLOR(on)  ((on) ? pagesInkColor : pagesPal->bg)

    bool pagesHasColor() { return true; }

    void pagesSetLight(bool light) {
        pagesPal = light ? &PAL_LIGHT : &PAL_DARK;
        pagesInkColor = pagesPal->ink[INK_TEXT];
    }

    void pagesInk(uint8_t ink) {
        pagesInkColor = pagesPal->ink[ink < INK_COUNT ? ink : INK_TEXT];
    }

    void pagesZone(uint8_t zone) {
        pagesDy = zone == PZ_TOP ? 0 : (zone == PZ_BOTTOM ? 2 * PAGES_BODY_DY : PAGES_BODY_DY);
    }

    void pagesBegin() {
        pagesInk(INK_TEXT);
        pagesZone(PZ_BODY);
        sprite.fillSprite(pagesPal->bg);
    }

    void pagesFlush() {
        sprite.pushSprite(0, 0);
    }

    void pagesFillRect(int x, int y, int w, int h, bool on) {
        sprite.fillRect(PAGES_X(x), PAGES_Y(y), w * PAGES_SCALE, h * PAGES_SCALE, PAGES_COLOR(on));
    }

    void pagesDrawRect(int x, int y, int w, int h, bool on) {
        for (int i = 0; i < PAGES_SCALE; i++) {      // el trazo crece hacia dentro para conservar el tamano
            sprite.drawRect(PAGES_X(x) + i, PAGES_Y(y) + i, w * PAGES_SCALE - 2 * i, h * PAGES_SCALE - 2 * i, PAGES_COLOR(on));
        }
    }

    void pagesDrawLine(int x0, int y0, int x1, int y1, bool on) {
        sprite.drawLine(PAGES_X(x0), PAGES_Y(y0), PAGES_X(x1), PAGES_Y(y1), PAGES_COLOR(on));
        #if PAGES_SCALE > 1
            sprite.drawLine(PAGES_X(x0) + 1, PAGES_Y(y0), PAGES_X(x1) + 1, PAGES_Y(y1), PAGES_COLOR(on));
            sprite.drawLine(PAGES_X(x0), PAGES_Y(y0) + 1, PAGES_X(x1), PAGES_Y(y1) + 1, PAGES_COLOR(on));
        #endif
    }

    void pagesDrawCircle(int x, int y, int r, bool on) {
        sprite.drawCircle(PAGES_X(x), PAGES_Y(y), r * PAGES_SCALE, PAGES_COLOR(on));
        #if PAGES_SCALE > 1
            sprite.drawCircle(PAGES_X(x), PAGES_Y(y), r * PAGES_SCALE - 1, PAGES_COLOR(on));
        #endif
    }

    void pagesFillCircle(int x, int y, int r, bool on) {
        sprite.fillCircle(PAGES_X(x), PAGES_Y(y), r * PAGES_SCALE, PAGES_COLOR(on));
    }

    void pagesFillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, bool on) {
        sprite.fillTriangle(PAGES_X(x0), PAGES_Y(y0), PAGES_X(x1), PAGES_Y(y1), PAGES_X(x2), PAGES_Y(y2), PAGES_COLOR(on));
    }

    // Mapa de bits de 2 bits por pixel (0 fondo, 1..3 capas). En color: ondas, pin y L+ con los colores del logo
    void pagesBitmap2(int x, int y, int w, int h, const uint8_t* data) {
        for (int j = 0; j < h; j++) {
            for (int i = 0; i < w; i++) {
                uint16_t n = (uint16_t)(j * w + i);
                uint8_t v = (pgm_read_byte(data + (n >> 2)) >> ((n & 3) * 2)) & 3;
                if (v) sprite.fillRect(PAGES_X(x + i), PAGES_Y(y + j), PAGES_SCALE, PAGES_SCALE, pagesPal->logo[v - 1]);
            }
        }
    }

    // Imagen RGB565 (un uint16_t por pixel, fila a fila) en coordenadas del lienzo
    void pagesImage565(int x, int y, int w, int h, const uint16_t* data) {
        for (int j = 0; j < h; j++) {
            for (int i = 0; i < w; i++) {
                sprite.fillRect(PAGES_X(x + i), PAGES_Y(y + j), PAGES_SCALE, PAGES_SCALE, pgm_read_word(data + j * w + i));
            }
        }
    }

    void pagesIcon4(int x, int y, int size, const uint16_t* colors, const uint8_t* alphas, const uint8_t* pixels, bool onInk) {
        const uint16_t under = onInk ? pagesInkColor : pagesPal->bg;
        for (int j = 0; j < size; j++) {
            for (int i = 0; i < size; i++) {
                const int k = j * size + i;
                const uint8_t b = pixels[k >> 1];
                const uint8_t v = (k & 1) ? (b & 0x0F) : (b >> 4);
                if (!v) continue;
                const uint8_t a = alphas[v - 1];
                const uint16_t c = a >= 250 ? colors[v - 1] : sprite.alphaBlend(a, colors[v - 1], under);
                sprite.fillRect(PAGES_X(x + i), PAGES_Y(y + j), PAGES_SCALE, PAGES_SCALE, c);
            }
        }
    }

    void pagesText(int x, int y, const char* text, uint8_t size, bool inverted) {
        sprite.setTextFont(0);
        sprite.setTextSize(size * PAGES_SCALE);
        if (inverted) sprite.setTextColor(pagesPal->onInk, pagesInkColor);
        else sprite.setTextColor(pagesInkColor);      // sin fondo: no borra lo dibujado debajo
        sprite.drawString(String(text), PAGES_X(x), PAGES_Y(y));
    }
#else
    bool pagesHasColor() { return false; }
    void pagesSetLight(bool) {}
    void pagesInk(uint8_t) {}
    void pagesZone(uint8_t) {}
    void pagesImage565(int, int, int, int, const uint16_t*) {}
    void pagesIcon4(int, int, int, const uint16_t*, const uint8_t*, const uint8_t*, bool) {}

    void pagesBegin() {
        display.clearDisplay();
    }

    void pagesFlush() {
        #ifdef ssd1306
            display.ssd1306_command(SSD1306_SETCONTRAST);
            display.ssd1306_command(screenBrightness);
        #else
            display.setContrast(screenBrightness);
        #endif
        display.display();
    }

    void pagesFillRect(int x, int y, int w, int h, bool on) {
        display.fillRect(x, y, w, h, on ? 1 : 0);
    }

    void pagesDrawRect(int x, int y, int w, int h, bool on) {
        display.drawRect(x, y, w, h, on ? 1 : 0);
    }

    void pagesDrawLine(int x0, int y0, int x1, int y1, bool on) {
        display.drawLine(x0, y0, x1, y1, on ? 1 : 0);
    }

    void pagesDrawCircle(int x, int y, int r, bool on) {
        display.drawCircle(x, y, r, on ? 1 : 0);
    }

    void pagesFillCircle(int x, int y, int r, bool on) {
        display.fillCircle(x, y, r, on ? 1 : 0);
    }

    void pagesFillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, bool on) {
        display.fillTriangle(x0, y0, x1, y1, x2, y2, on ? 1 : 0);
    }

    // Mapa de bits de 2 bits por pixel (0 fondo, 1..3 capas): en monocromo se enciende todo lo que no es fondo
    void pagesBitmap2(int x, int y, int w, int h, const uint8_t* data) {
        for (int j = 0; j < h; j++) {
            for (int i = 0; i < w; i++) {
                uint16_t n = (uint16_t)(j * w + i);
                if ((pgm_read_byte(data + (n >> 2)) >> ((n & 3) * 2)) & 3) display.drawPixel(x + i, y + j, 1);
            }
        }
    }

    void pagesText(int x, int y, const char* text, uint8_t size, bool inverted) {
        display.setTextSize(size);
        if (inverted) display.setTextColor(0, 1);
        else display.setTextColor(1);
        display.setCursor(x, y);
        display.print(text);
    }
#endif
#endif

void startupScreen(uint8_t index, const String& version, const String& callsign) {
    #ifdef HAS_APRS_PAGES
        PAGES_Utils::showSplash(version, callsign);     // logo APRS LoRa+, version e indicativo
        delay(2000);
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Main", "APRS LoRa+ (c) 2026 Manuel - EA7JRS");
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Main", "Version: %s", version.c_str());
        return;
    #endif
    String workingFreq = "    LoRa Freq [";
    switch (index) {
        case 0: workingFreq += "EU]"; break;
        case 1: workingFreq += "PL]"; break;
        case 2: workingFreq += "UK]"; break;
        case 3: workingFreq += "US]"; break;
    }
    displayShow(" LoRa APRS", "      (TRACKER)", workingFreq, "", "", "  EA7JRS  " + version, 4000);
    logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Main", "APRS LoRa+ (c) 2026 Manuel - EA7JRS");
    logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Main", "Version: %s", version.c_str());
}

String fillMessageLine(const String& line, const int& length) {
    String completeLine = line;
    for (int i = 0; completeLine.length() <= length; i++) {
        completeLine = completeLine + " ";
    }
    return completeLine;
}