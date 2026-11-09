/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * LilyGO T-TWR Plus (Rev2.0 y Rev2.1): ESP32-S3 N16R8, radio FM SA868 (VHF 134-174 MHz o UHF 400-480 MHz), PMU AXP2101,
 * GNSS L76K, OLED SH1106 de 1,3" (0x3C en VHF, 0x3D en UHF), encoder con pulsador, LED WS2812, TF.
 * Pines: libreria de LilyGO (Xinyuan-LilyGO/T-TWR, lib/LilyGo_TWR_Library/src/utilities.h) y esquemas de la Rev2.0 y la Rev2.1.
 * HAS_SA868 y LILYGO_T_TWR_PLUS vienen de platformio.ini (cambian la configuracion en todas las unidades de compilacion).
 */

#ifndef BOARD_PINOUT_H_
#define BOARD_PINOUT_H_

    //  Radio FM SA868 (APRS AFSK 1200; sin radio LoRa)
    #define SA868_TX_PIN        39      // UART del ESP32 hacia el modulo
    #define SA868_RX_PIN        48      // UART del modulo hacia el ESP32
    #define SA868_PTT_PIN       41      // bajo = emision
    #define SA868_PD_PIN        40      // bajo = modulo apagado
    #define SA868_HL_PIN        38      // solo Rev2.0: alto = potencia alta
    #define SA868_SQL_PIN       2       // solo Rev2.1: bajo = silenciador abierto (portadora)
    #define SA868_MIC_SEL_PIN   17      // MIC_CH_SEL: alto = audio del ESP32 hacia el microfono del modulo
    #define SA868_AUDIO_OUT_PIN 18      // ESP2MIC: audio AFSK (PDM) -> filtro RC -> microfono del modulo
    #define SA868_AUDIO_IN_PIN  1       // AUDIO2ESP: salida de audio del modulo (polarizada) -> ADC1
    #define SA868_AUDIO_ADC_CHANNEL ADC_CHANNEL_0   // GPIO1 = ADC1_CH0

    //  Display
    #undef  OLED_SDA
    #undef  OLED_SCL
    #undef  OLED_RST

    #define OLED_SDA            8       // I2C compartido con la PMU
    #define OLED_SCL            9
    #define OLED_RST            -1

    //  GPS L76K (alimentado por el ALDO4 de la PMU)
    #define GPS_RX              6       // ESP32 TX -> L76K RX
    #define GPS_TX              5       // L76K TX -> ESP32 RX
    #define GPS_PPS             7

    //  Pulsadores y otros
    #define BUTTON_PIN          21      // pulsador del encoder (el boton de un solo pulsador del tracker)
    #define BUTTON2_PIN         3       // PTT lateral
    #define TTWR_ENCODER_A      47
    #define TTWR_ENCODER_B      46
    #define TTWR_PIXEL_PIN      42      // LED WS2812
    #define BOARD_SDCARD_CS     10      // TF (SPI 11 / 12 / 13), alimentada por el ALDO2
    #define PMU_IRQ_PIN         4

    #define HAS_AXP2101
    #ifndef BOARD_HAS_PSRAM
        #define BOARD_HAS_PSRAM
    #endif

#endif
