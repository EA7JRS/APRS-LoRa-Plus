/*
 * APRS LoRa+ - Region IARU automatica: ajusta la region (y el plan de bandas) de todos los perfiles segun la posicion GPS.
 * Copyright (C) 2026 Manuel - EA7JRS   (GPL-3.0-or-later)
 *
 * Config.iaruAuto.mode: 0 apagada, 1 al arrancar y bajo peticion (SYSTEM o WebUI), 2 ademas en marcha (al cruzar un limite).
 * Config.iaruAuto.confirm: true = ventana de confirmacion en pantalla antes de cambiar una frecuencia; false = se aplica directamente.
 */
#ifndef IARU_AUTO_H_
#define IARU_AUTO_H_

#include <Arduino.h>

namespace IARU_Auto {

    void   loop();                      // desde el bucle principal; se limita a una vez por segundo
    bool   request();                   // "bajo peticion": evalua con la posicion actual (hace falta fix GPS); false si no lo hay
    int    detected();                  // ultima region detectada con fix estable (0 = ninguna todavia)
    bool   asking();                    // ventana de confirmacion abierta (menu 9100 / 9101)
    void   answer(bool apply);          // respuesta de la ventana: true aplica el cambio, false lo descarta
    String json();                      // estado para la WebUI
    String title();                     // textos de la ventana de confirmacion
    String applyText();
    String keepText();

}

#endif
