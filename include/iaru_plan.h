/*
 * APRS LoRa+ - Regiones IARU: deteccion por posicion y planes de banda (2 m APRS de la T-TWR, 70 cm / 33 cm).
 * Copyright (C) 2026 Manuel - EA7JRS   (GPL-3.0-or-later)
 *
 * Codigo puro (sin Arduino): se prueba en el PC con tools/test_iaru_plan.cpp.
 * Las tablas de canales deben coincidir con las de data_embed/script.js (tools/check_iaru_tables.py lo comprueba).
 * Los limites de las regiones son una simplificacion del art. 5.2 del Reglamento de Radiocomunicaciones de la UIT:
 * en tierra pueden fallar decenas de km junto a las fronteras R1/R3, y las normas nacionales prevalecen siempre.
 */
#ifndef IARU_PLAN_H_
#define IARU_PLAN_H_

namespace IARU_Plan {

    enum Kind { LORA = 0, FSK = 1, CW = 2, CWB = 3, VHF = 4 };     // tipo de perfil: determina la lista de canales (VHF = APRS AFSK 1200 de la T-TWR)

    int  regionAt(float lat, float lon);                    // 1, 2 o 3 segun la posicion (grados decimales)
    bool allowed(int region, long hz);                      // dentro de la banda de aficionados de la region (2 m, 70 cm y, en R2, 33 cm)
    int  channelCount(Kind kind, int region);
    long channelHz(Kind kind, int region, int index);
    long defaultHz(Kind kind, int region);                  // canal por defecto de la region para ese tipo de perfil
    bool isPreset(Kind kind, int region, long hz);          // la frecuencia es un canal de la lista de la region
    // Frecuencia equivalente al pasar de una region a otra:
    //   - canal de la lista y existe en la nueva  -> el mismo
    //   - canal de la lista sin equivalente       -> canal por defecto de la nueva region
    //   - frecuencia personalizada (Custom)       -> se respeta si cae en la banda de la nueva region; si no, el canal por defecto
    long mapFrequency(Kind kind, int fromRegion, int toRegion, long hz);

}

#endif
