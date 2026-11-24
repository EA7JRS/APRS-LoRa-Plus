/*
 * Modo SSDV (imagenes JPEG en paquetes SSDV de UKHAS): solo en placas con radio SX12xx (RadioLib) y sitio en flash.
 * Usa el SSDVClient de RadioLib 8.0.x. Se puede quitar de una placa con #define NO_SSDV en su board_pinout.h
 * (la T-Beam Supreme va muy justa de particion). Las placas de radio FM (SA868, T-TWR) no lo llevan: SSDV necesita
 * una capa PhysicalLayer de RadioLib, que el SA868 no es.
 *
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#ifndef SSDV_CONFIG_H_
#define SSDV_CONFIG_H_

#ifndef ARDUINO
    #define HAS_SSDV                 // pruebas en el PC
#else
    #include <Arduino.h>
    #include "board_pinout.h"
    #if !defined(HAS_SA868) && !defined(NO_SSDV)
        #define HAS_SSDV
    #endif
#endif

#endif
