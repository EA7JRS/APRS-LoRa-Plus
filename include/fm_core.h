/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Modo FM (voz analogica) de la LilyGO T-TWR: la parte sin hardware, probada en el PC (tools/fm/test_fm.cpp).
 *  - Tonos: los 50 CTCSS y los 104 codigos DCS habituales; los 38 CTCSS que acepta el firmware de NiceRF del SA868.
 *  - Canal (VFO o memoria): frecuencia, desplazamiento de repetidor, tono, ancho, potencia; desplazamiento automatico (ARS) por
 *    region IARU; pasos del VFO.
 *  - QSY por APRS: especificacion de frecuencia de APRS 1.2 (WB4APR, "freqspec") en el comentario de una baliza u objeto
 *    ("146.940MHz T100 -060", "439.400MHz C071 -760 R25k", "145.50 MHz t077 Simplex"): lectura para sintonizar la emisora y
 *    escritura para anadirla a las balizas propias (como el QSY de Kenwood).
 *  - Escaneo (memorias o banda) con reanudacion por portadora o por tiempo, ARTS (sondeo DCS cada 15 / 25 s, "IN / OUT RANGE",
 *    como Yaesu) y EAI (emergencia: pitidos cada 2,5 s o microfono abierto, con indicativo en CW, como Yaesu) y "hombre caido".
 */

#ifndef FM_CORE_H_
#define FM_CORE_H_

#include <stddef.h>
#include <stdint.h>

namespace FmCore {

    // ---- Tonos ------------------------------------------------------------------------------------------------------
    constexpr int CTCSS_COUNT = 50;
    extern const uint16_t CTCSS[CTCSS_COUNT];           // decimas de Hz (670 = 67,0 Hz)
    constexpr int DCS_COUNT = 104;
    extern const uint16_t DCS[DCS_COUNT];               // codigos en octal escritos como decimal (23 = "023")

    int  ctcssIndex(uint16_t tenths);                   // -1 si no esta en la tabla
    int  niceRfCtcss(uint16_t tenths);                  // 1..38 (codigo del AT+DMOSETGROUP de NiceRF) o 0 si no lo admite
    bool validDcs(uint16_t code);
    int  dcsIndex(uint16_t code);
    uint16_t ctcssThreshold(uint16_t tenths);           // registro 0x5B del AT1846S (tabla de OpenRTX, firmware de TYT)
    uint16_t nextCtcss(uint16_t tenths, int dir);       // el siguiente / anterior de la tabla (da la vuelta)
    uint16_t nextDcs(uint16_t code, int dir);

    // ---- Canal ------------------------------------------------------------------------------------------------------
    enum ToneMode : uint8_t { TONE_OFF = 0, TONE_ENC = 1, TONE_TSQL = 2, TONE_DCS = 3, TONE_1750 = 4, TONE_MODES = 5 };
    const char* toneModeName(uint8_t mode);             // "OFF", "TONE", "TSQL", "DCS", "1750"

    struct Channel {
        char     name[9];                               // nombre (8 caracteres); vacio en el VFO
        long     rxHz;
        int8_t   shift;                                 // -1, 0 (simplex), +1
        long     offsetHz;                              // desplazamiento del repetidor (positivo)
        uint8_t  toneMode;
        uint16_t tone;                                  // CTCSS en decimas de Hz
        uint16_t dcs;                                   // DCS (23 = "023")
        bool     dcsInv;                                // DCS invertido
        bool     wide;                                  // 25 kHz (si no, 12,5 kHz)
        bool     high;                                  // potencia alta
    };
    void  clearChannel(Channel& c, long rxHz);
    long  txHz(const Channel& c);
    bool  validChannel(const Channel& c, long lo, long hi);

    // Banda del modulo: 136..174 MHz (VHF) o 400..480 MHz (UHF) para recibir
    bool  inVhf(long hz);
    bool  inUhf(long hz);
    // Emision: solo en las bandas de aficionado de la region IARU (Europa / R1: 144-146 y 430-440 MHz; America / R2: 144-148 y
    // 430-450 MHz; R3: 144-148 y 430-440 MHz). region 0 = sin saber donde se esta: solo lo comun a las tres (144-146, 430-440).
    // Fijo en el firmware: no se cambia desde la WebUI ni la configuracion
    bool  txAllowed(long hz, int region);
    // Desplazamiento habitual de los repetidores de la banda en cada region IARU (600 kHz en 2 m; 7,6 MHz en 70 cm en la R1, 5 MHz en la R2 / R3)
    long  standardOffset(long hz, int region);
    // ARS: si la frecuencia cae en un segmento de salidas de repetidor de la region, sentido y desplazamiento (si no, false)
    bool  autoRepeater(long hz, int region, int8_t& shift, long& offsetHz);

    // ---- VFO --------------------------------------------------------------------------------------------------------
    constexpr int STEP_COUNT = 9;
    extern const long STEPS[STEP_COUNT];                // 5, 6,25, 10, 12,5, 15, 20, 25, 50, 100 kHz
    long  snap(long hz, long step);                     // a la rejilla del paso
    long  tune(long hz, int dir, long step, long lo, long hi);       // un paso arriba / abajo, con vuelta dentro de [lo, hi]

    // ---- QSY por APRS -----------------------------------------------------------------------------------------------
    struct Qsy {
        long     hz;                                    // 0 = no hay frecuencia
        bool     hasOffset;                             // la baliza dice el desplazamiento ("+060", "-760", "-000" / Toff = simplex)
        long     offsetHz;                              // con signo
        uint8_t  toneMode;
        uint16_t tone;                                  // CTCSS (decimas de Hz)
        uint16_t dcs;
        bool     narrow;                                // primera letra del tono en minusculas
        int      rangeKm;                               // R25k / R25m (convertido a km); 0 = no lo dice
    };
    // name: nombre del objeto ("146.94-R", puede ser nullptr); comment: comentario de la baliza, objeto o estado
    bool  parseQsy(const char* name, const char* comment, Qsy& out);
    // Canal listo para sintonizar: el desplazamiento de la baliza o, si no lo dice, el habitual de la region (ARS)
    void  qsyToChannel(const Qsy& q, int region, Channel& out);
    // "146.940MHz T100 -060" (20 caracteres) para el principio del comentario de las balizas propias
    // (con el desplazamiento siempre escrito si la frecuencia cae en un segmento de repetidores: "-000" = simplex forzado)
    size_t formatQsy(const Channel& c, int region, char* out, size_t outMax);
    size_t formatFreq(long hz, char* out, size_t outMax);   // "145.5000"

    // ---- Escaneo ----------------------------------------------------------------------------------------------------
    enum Resume : uint8_t { RESUME_CARRIER = 0, RESUME_TIME = 1, RESUME_STOP = 2 };
    class Scanner {
    public:
        void     start(uint32_t now, uint8_t resume, uint32_t dwellMs = 150);
        void     stop() { active = false; }
        bool     running() const { return active; }
        bool     paused() const { return active && holding; }
        void     tuned(uint32_t now);                   // el llamador acaba de sintonizar el canal siguiente
        bool     tick(uint32_t now, bool busy);         // true = pasar al siguiente canal (el llamador sintoniza y llama a tuned())
        int8_t   dir = 1;
    private:
        bool     active = false, holding = false;
        uint8_t  mode = RESUME_CARRIER;
        uint32_t dwell = 150, tunedAt = 0, stopAt = 0, freeAt = 0;
    };

    // ---- ARTS -------------------------------------------------------------------------------------------------------
    enum ArtsState : uint8_t { ARTS_UNKNOWN = 0, ARTS_IN = 1, ARTS_OUT = 2 };
    class Arts {
    public:
        void     start(uint32_t now, int intervalS, bool cwId);
        void     stop() { active = false; }
        bool     running() const { return active; }
        bool     wantPoll(uint32_t now, bool busy);     // toca sondear (canal libre)
        void     polled(uint32_t now);                  // sondeo enviado (tambien al soltar el PTT)
        void     heard(uint32_t now);                   // senal con el DCS del grupo
        bool     wantCwId(uint32_t now);                // indicativo en CW cada 10 minutos
        void     cwIdSent(uint32_t now) { lastId = now; }
        bool     update(uint32_t now);                  // true si el estado ha cambiado
        uint8_t  state = ARTS_UNKNOWN;
        uint32_t lastHeard = 0;
    private:
        bool     active = false, id = false, everHeard = false;
        uint32_t interval = 25000, lastPoll = 0, startAt = 0, lastId = 0;
    };

    // ---- EAI (emergencia) y hombre caido --------------------------------------------------------------------------------
    enum EaiMode : uint8_t { EAI_OFF = 0, EAI_INTERVAL = 1, EAI_CONTINUOUS = 2 };
    enum EaiAction : uint8_t { EA_NONE = 0, EA_CWID, EA_BEEP, EA_MIC_ON, EA_MIC_OFF, EA_END };
    class Eai {
    public:
        void      start(uint32_t now, uint8_t mode, int minutes);
        void      stop() { active = false; micOn = false; }
        bool      running() const { return active; }
        uint32_t  remainingS(uint32_t now) const;
        // Lo siguiente que hay que hacer. Intervalo: un pitido de 0,5 s cada 2,5 s; continuo: microfono abierto en tramos de 50 s
        // con 10 s de escucha; indicativo en CW al empezar y cada 10 minutos; EA_END al acabar el tiempo
        EaiAction tick(uint32_t now);
        uint8_t   mode = EAI_INTERVAL;
        bool      micOn = false;
    private:
        bool      active = false;
        uint32_t  startAt = 0, endAt = 0, nextBeep = 0, nextId = 0, segAt = 0, segEnd = 0;
    };

    class ManDown {
    public:
        void     configure(int minutes) { limitMs = minutes > 0 ? (uint32_t)minutes * 60000UL : 0; }
        void     activity(uint32_t now) { lastAct = now; warned = false; fired = false; }
        // 0 nada, 1 aviso (30 s antes), 2 disparo
        uint8_t  tick(uint32_t now);
        bool     enabled() const { return limitMs > 0; }
        static constexpr uint32_t WARN_MS = 30000;
    private:
        uint32_t limitMs = 0, lastAct = 0;
        bool     warned = false, fired = false;
    };
}

#endif
