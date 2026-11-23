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

#include <OneButton.h>
#include <logger.h>
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    #include "esp32-hal-periman.h"
#endif
#include "keyboard_utils.h"
#include "configuration.h"
#include "board_pinout.h"
#include "button_utils.h"
#include "power_utils.h"
#include "display.h"
#include "pages_utils.h"
#include "pages_core.h"
#ifdef LILYGO_T_TWR_PLUS
    #include "fm_utils.h"
    #include "sa868_radio.h"
#endif

#ifdef BUTTON_PIN

    extern Configuration    Config;
    extern int              menuDisplay;
    extern uint32_t         displayTime;
    extern uint32_t         menuTime;
    extern logging::Logger  logger;
    extern bool             displayState;
    extern uint32_t         refreshDisplayTime;

    namespace BUTTON_Utils {

        OneButton userButton = OneButton(BUTTON_PIN, true, true);

        // Cada pulsacion cuenta como actividad: reinicia el temporizador del modo ECO de la pantalla (EA7JRS). Antes solo lo
        // hacia la pulsacion que la encendia, asi que la pantalla se apagaba mientras se pasaban paginas y la siguiente
        // pulsacion solo la volvia a encender (parecia que el boton no respondia)
        static void touched() {
            menuTime = displayTime = millis();
        }

        // Pantalla apagada por el modo ECO (EA7JRS): se enciende en cuanto se aprieta el boton, sin esperar a que OneButton
        // decida si es un clic, un doble clic o una pulsacion larga (hasta ~0,6 s), y se redibuja enseguida. Esa pulsacion
        // solo enciende: sus eventos se descartan, para no pasar de pagina ni abrir menus a ciegas. Si en la misma rafaga
        // llega un doble clic, la segunda pulsacion cuenta como 1P (pagina siguiente)
        static bool     wakeDown        = false;
        static bool     wakeArmed       = false;
        static uint32_t wakeReleasedMs  = 0;

        static void wakeOnPress() {
            const bool down = digitalRead(BUTTON_PIN) == LOW;
            if (down && !wakeDown) {
                wakeDown = true;
                if (!displayState) {
                    displayToggle(true);
                    touched();
                    refreshDisplayTime = 0;             // redibujar ya: la OLED guarda la ultima imagen de antes de apagarse
                    wakeArmed = true;
                }
            } else if (!down && wakeDown) {
                wakeDown = false;
                if (wakeArmed) wakeReleasedMs = millis();
            }
            if (wakeArmed && !wakeDown && millis() - wakeReleasedMs > 1500) wakeArmed = false;   // OneButton no dio evento
        }

        static bool swallowWake() {
            if (!wakeArmed) return false;
            wakeArmed = false;
            return true;
        }

        // Gesto de emergencia (5 pulsaciones cortas o SOS en morse): se lee el boton en bruto, aparte de OneButton.
        // Al reconocerlo se ignoran las pulsaciones que OneButton aun entregue de ese mismo gesto.
        #ifdef HAS_APRS_PAGES
            PAGES_Core::SosGesture  sosGesture;
            uint32_t                suppressUntil   = 0;
            bool                    rawDown         = false;
            uint32_t                rawEdgeMs       = 0;
            uint32_t                rawDownMs       = 0;
            uint32_t                rawUpMs         = 0;
            bool                    rawHaveUp       = false;

            bool suppressed() {
                return (int32_t)(suppressUntil - millis()) > 0;
            }

            void pollSosGesture() {
                uint32_t now = millis();
                bool down = digitalRead(BUTTON_PIN) == LOW;
                if (down == rawDown || now - rawEdgeMs < 25) return;           // antirrebote
                rawDown = down;
                rawEdgeMs = now;
                if (down) {
                    rawDownMs = now;
                } else {
                    uint32_t gap = rawHaveUp ? rawDownMs - rawUpMs : 100000UL;
                    rawUpMs = now;
                    rawHaveUp = true;
                    if (sosGesture.onPress(now - rawDownMs, gap)) {
                        suppressUntil = now + 900;
                        PAGES_Utils::onEmergencyGesture();
                    }
                }
            }
        #else
            bool suppressed() { return false; }
        #endif

        #ifdef RPC_Electronics_1W_LoRa_GPS
            OneButton userButton2 = OneButton(BUTTON2_PIN, true, true);
            OneButton userButton3 = OneButton(BUTTON3_PIN, true, true);
            OneButton userButton4 = OneButton(BUTTON4_PIN, true, true);
        #endif
        #ifdef LILYGO_T_TWR_PLUS
            // T-TWR Plus (EA7JRS): el pulsador del encoder es el boton de siempre (1P / LP / 2P); ademas, girar el encoder =
            // pagina u opcion siguiente / anterior, PTT lateral = 2P (atras / menu) fuera de FM (en FM es el PTT), DOWN = LP
            OneButton userButtonPtt  = OneButton(BUTTON2_PIN, true, true);
            OneButton userButtonDown = OneButton(TTWR_BUTTON_DOWN, true, true);
        #endif

        void singlePress1() {
            if (suppressed() || swallowWake()) return;
            touched();
            KEYBOARD_Utils::downArrow();
        }
        #ifdef RPC_Electronics_1W_LoRa_GPS
            void singlePress2() {
                touched();
                KEYBOARD_Utils::upArrow();
            }
            void singlePress3() {
                touched();
                KEYBOARD_Utils::rightArrow();
            }
            void singlePress4() {
                touched();
                KEYBOARD_Utils::leftArrow();
            }
        #endif

        void longPress1() {
            if (suppressed() || swallowWake()) return;
            touched();
            KEYBOARD_Utils::rightArrow();
        }

        void doublePress1() {
            if (suppressed()) return;
            if (swallowWake()) {                    // la primera pulsacion encendio la pantalla: la segunda es 1P
                touched();
                KEYBOARD_Utils::downArrow();
                return;
            }
            touched();
            displayToggle(true);
            if (menuDisplay == 0) {
                #ifdef HAS_APRS_PAGES
                    if (PAGES_Utils::back()) return;        // primero cierra el submenu de la pagina
                #endif
                menuDisplay = 1;
            } else if (menuDisplay > 0) {
                menuDisplay = 0;
                displayTime = millis();
            }
        }

        void multiPress1() {
            if (suppressed()) return;
            swallowWake();
            touched();
            displayToggle(true);
            menuDisplay = 9000;
        }

        #ifdef LILYGO_T_TWR_PLUS
            static void encoderNav() {
                #ifdef HAS_APRS_PAGES
                    if (PAGES_Utils::encoderWanted()) return;              // la pagina lo usa (FM, radio comercial, AIS, SAT)
                #endif
                int clicks = FM_Utils::takeEncoder();
                if (!clicks) return;
                FM_Utils::activity();                                      // cuenta como actividad (hombre caido)
                if (clicks > 3) clicks = 3; else if (clicks < -3) clicks = -3;
                menuTime = millis();
                for (; clicks > 0; clicks--) KEYBOARD_Utils::downArrow();
                for (; clicks < 0; clicks++) KEYBOARD_Utils::upArrow();
            }
        #endif

        // El GPIO del boton no lo debe tomar ningun otro periferico (EA7JRS). Si otro modulo lo reconfigura, OneButton deja
        // de ver las pulsaciones y el equipo parece dormido hasta reiniciar: se detecta, se anota en el registro y se recupera
        static void guardPin() {
            #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
                static uint32_t lastCheck = 0;
                const uint32_t now = millis();
                if (now - lastCheck < 2000) return;
                lastCheck = now;
                const peripheral_bus_type_t bus = perimanGetPinBusType(BUTTON_PIN);
                if (bus != ESP32_BUS_TYPE_GPIO) {
                    logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "Button", "GPIO %d taken by %s: restored as button input",
                               BUTTON_PIN, perimanGetTypeName(bus));
                    pinMode(BUTTON_PIN, INPUT_PULLUP);
                }
            #endif
        }

        void loop() {
            guardPin();
            if (!Config.simplifiedTrackerMode) {
                #ifdef LILYGO_T_TWR_PLUS
                    if (!SA868::modeFm()) userButtonPtt.tick();
                    userButtonDown.tick();
                    encoderNav();
                #endif
                wakeOnPress();
                userButton.tick();
                #ifdef HAS_APRS_PAGES
                    pollSosGesture();
                #endif
                #ifdef RPC_Electronics_1W_LoRa_GPS
                    userButton2.tick();
                    userButton3.tick();
                    userButton4.tick();
                #endif
            }
        }

        void setup() {
            pinMode(BUTTON_PIN, INPUT_PULLUP);          // OneButton lo configura en su constructor, antes de setup(): se repite aqui
            if (!Config.simplifiedTrackerMode) {
                userButton.attachClick(singlePress1);
                userButton.attachLongPressStart(longPress1);
                userButton.attachDoubleClick(doublePress1);
                userButton.attachMultiClick(multiPress1);
                #ifdef RPC_Electronics_1W_LoRa_GPS
                    userButton2.attachClick(singlePress2);
                    userButton3.attachClick(singlePress3);
                    userButton4.attachClick(singlePress4);
                #endif
                #ifdef LILYGO_T_TWR_PLUS
                    FM_Utils::encoderBegin();
                    userButtonPtt.attachClick(doublePress1);
                    userButtonDown.attachClick(longPress1);
                #endif
            }
        }

    }

#endif