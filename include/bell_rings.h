/*
 * Melodias de "campana APRS" para el zumbador pasivo (KY-006 o similar), inspiradas en la tabla «List of APRS Bell Rings»
 * del manual APRS del Yaesu FTM-400DR (ding = nota aguda, dong = nota grave; ritmo y numero de notas segun la tabla).
 * El manual da solo la notacion musical, no frecuencias: las alturas son una aproximacion propia (ver BELL_TABLE) y
 * se pueden afinar aqui. Reproductor sin bloqueos y sin dependencias de Arduino: se prueba en un PC (tools/test_bell.cpp).
 *
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#ifndef BELL_RINGS_H_
#define BELL_RINGS_H_

#include <stdint.h>

namespace Bell {

    // Orden = prioridad (el menor numero gana si dos suenan a la vez)
    enum Id : uint8_t {
        BELL_BULLETIN = 0,      // boletin / mensaje de grupo recibido        (ding ding ding ding ding dong)
        BELL_MSG_TO_ME,         // mensaje dirigido a mi indicativo           (ding ding ding dong)
        BELL_REJ_RX,            // REJ recibido                               (ding)
        BELL_ACK_RX,            // ACK recibido                               (ding)
        BELL_BEACON_RX,         // baliza recibida                            (ding dong)
        BELL_MSG_OTHER,         // mensaje de otra estacion                   (ding)
        BELL_MSG_DUP,           // mensaje duplicado                          (ding ding, corcheas)
        BELL_BEACON_DUP,        // baliza duplicada                           (ding, corchea)
        BELL_TX,                // baliza o mensaje enviado                   (ding)
        BELL_COUNT
    };

    struct Note {
        uint16_t hz;            // 0 = silencio
        uint16_t ms;
    };

    // Alturas aproximadas de la tabla del manual (ding agudo, dong grave)
    enum : uint16_t { N_DING = 1319, N_DING_MID = 1175, N_DING_LOW = 1047, N_DING_UP = 1568, N_DING_REJ = 988, N_DONG = 784 };
    enum : uint16_t { Q = 140, E = 70, H = 300 };              // negra, corchea, blanca (ms)
    const uint16_t GAP_MS = 20;                                // silencio entre notas

    static const Note M_BULLETIN[]   = { {N_DING_LOW, E}, {N_DING_MID, E}, {N_DING, E}, {N_DING_MID, E}, {N_DING, E}, {N_DONG, H} };
    static const Note M_MSG_TO_ME[]  = { {N_DING_LOW, E}, {N_DING, E}, {N_DING_LOW, E}, {N_DONG, H} };
    static const Note M_REJ_RX[]     = { {N_DING_REJ, Q} };
    static const Note M_ACK_RX[]     = { {N_DING, Q} };
    static const Note M_BEACON_RX[]  = { {N_DING, Q}, {N_DONG, H} };
    static const Note M_MSG_OTHER[]  = { {N_DING_MID, Q} };
    static const Note M_MSG_DUP[]    = { {N_DING_UP, E}, {N_DING, E} };
    static const Note M_BEACON_DUP[] = { {N_DING_UP, E} };
    static const Note M_TX[]         = { {N_DING_LOW, Q} };

    struct Melody { const Note* notes; uint8_t count; };

    #define BELL_ENTRY(m) { m, (uint8_t)(sizeof(m) / sizeof(m[0])) }
    static const Melody BELL_TABLE[BELL_COUNT] = {
        BELL_ENTRY(M_BULLETIN), BELL_ENTRY(M_MSG_TO_ME), BELL_ENTRY(M_REJ_RX), BELL_ENTRY(M_ACK_RX), BELL_ENTRY(M_BEACON_RX),
        BELL_ENTRY(M_MSG_OTHER), BELL_ENTRY(M_MSG_DUP), BELL_ENTRY(M_BEACON_DUP), BELL_ENTRY(M_TX)
    };
    #undef BELL_ENTRY

    // Reproductor sin bloqueos: start() arranca una melodia y tick() dice, en cada instante, que frecuencia debe sonar
    class Player {
    public:
        bool    active() const { return playing; }

        // false si ya suena una melodia de igual o mayor prioridad (no se pisa)
        bool start(Id id, uint32_t now) {
            if (id >= BELL_COUNT) return false;
            if (playing && id >= current) return false;
            current = id; index = 0; inGap = false; noteStart = now; playing = true;
            return true;
        }

        void stop() { playing = false; }

        // true mientras la melodia no ha terminado; hz recibe la frecuencia que debe sonar ahora (0 = silencio)
        bool tick(uint32_t now, uint16_t& hz) {
            hz = 0;
            if (!playing) return false;
            const Melody& m = BELL_TABLE[current];
            for (;;) {
                if (index >= m.count) { playing = false; return false; }
                const uint32_t length = inGap ? GAP_MS : m.notes[index].ms;
                if ((uint32_t)(now - noteStart) < length) { hz = inGap ? 0 : m.notes[index].hz; return true; }
                noteStart += length;                                // pasa a la fase siguiente sin perder tiempo acumulado
                if (inGap) { inGap = false; index++; }
                else       { inGap = true; }
            }
        }

    private:
        Id       current   = BELL_TX;
        uint8_t  index     = 0;
        bool     inGap     = false;
        bool     playing   = false;
        uint32_t noteStart = 0;
    };

}

#endif
