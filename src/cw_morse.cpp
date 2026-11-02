/* Copyright (C) 2026 Manuel - EA7JRS
 * Morse (CW) encoder with PARIS timing. See cw_morse.h
 * Part of AIS/LoRa APRS Tracker - Manuel Canca EA7JRS (based on LoRa APRS Tracker by Ricardo Guzman CA2RXU, GPL-3.0)
 */
#include <string.h>
#include <ctype.h>
#include "cw_morse.h"

namespace CWMorse {

    namespace {
        struct Entry { char c; const char* p; };
        const Entry TABLE[] = {
            {'A', ".-"},   {'B', "-..."}, {'C', "-.-."}, {'D', "-.."},  {'E', "."},    {'F', "..-."}, {'G', "--."},
            {'H', "...."}, {'I', ".."},   {'J', ".---"}, {'K', "-.-"},  {'L', ".-.."}, {'M', "--"},   {'N', "-."},
            {'O', "---"},  {'P', ".--."}, {'Q', "--.-"}, {'R', ".-."},  {'S', "..."},  {'T', "-"},    {'U', "..-"},
            {'V', "...-"}, {'W', ".--"},  {'X', "-..-"}, {'Y', "-.--"}, {'Z', "--.."},
            {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"}, {'4', "....-"},
            {'5', "....."}, {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."},
            {'.', ".-.-.-"}, {',', "--..--"}, {'?', "..--.."}, {'/', "-..-."}, {'=', "-...-"}, {'+', ".-.-."},
            {'-', "-....-"}, {'@', ".--.-."}, {':', "---..."}, {'\'', ".----."}, {'(', "-.--."}, {')', "-.--.-"},
            {'"', ".-..-."}
        };
    }

    const char* pattern(char c) {
        c = (char)toupper((unsigned char)c);
        for (size_t i = 0; i < sizeof(TABLE) / sizeof(TABLE[0]); i++) if (TABLE[i].c == c) return TABLE[i].p;
        return nullptr;
    }

    char charForPattern(const char* p) {
        for (size_t i = 0; i < sizeof(TABLE) / sizeof(TABLE[0]); i++) if (strcmp(TABLE[i].p, p) == 0) return TABLE[i].c;
        return 0;
    }

    static uint8_t clampWpm(uint8_t wpm) {
        return wpm < WPM_MIN ? WPM_MIN : (wpm > WPM_MAX ? WPM_MAX : wpm);
    }

    uint32_t dotMs(uint8_t wpm) {
        return 1200u / clampWpm(wpm);
    }

    uint32_t totalMs(const char* text, uint8_t wpm) {
        uint32_t dot = dotMs(wpm), units = 0;
        for (const char* t = text; *t; t++) {
            if (*t == ' ') { units += 4; continue; }                // 7 units between words: 3 of the previous letter + 4
            const char* p = pattern(*t);
            if (!p) continue;
            for (const char* e = p; *e; e++) units += (*e == '-' ? 3 : 1) + 1;   // element + 1 unit gap
            units += 2;                                                         // letter gap = 3 units in total
        }
        return units * dot;
    }

    bool send(const char* text, uint8_t wpm, const Hooks& h, uint8_t gapWpm) {
        uint32_t dot = dotMs(wpm);
        uint32_t gapDot = (gapWpm == 0 || gapWpm >= wpm) ? dot : dotMs(gapWpm);
        size_t total = strlen(text);
        if (h.progress) h.progress(h.ctx, 0, total, "", 0);
        for (size_t i = 0; text[i]; i++) {
            if (text[i] == ' ') {
                if (!h.wait(h.ctx, 4 * gapDot)) { h.key(h.ctx, false); return false; }
                if (h.progress) h.progress(h.ctx, i + 1, total, " ", 0);
                continue;
            }
            const char* p = pattern(text[i]);
            if (!p) { if (h.progress) h.progress(h.ctx, i + 1, total, "", 0); continue; }
            for (uint8_t e = 0; p[e]; e++) {
                h.key(h.ctx, true);
                bool ok = h.wait(h.ctx, (p[e] == '-' ? 3 : 1) * dot);
                h.key(h.ctx, false);
                if (!ok) return false;
                if (p[e + 1] != 0) {                            // gap inside the letter (the last one is counted in the letter gap)
                    if (!h.wait(h.ctx, dot)) return false;
                } else {
                    if (h.progress) h.progress(h.ctx, i + 1, total, p, e + 1);   // key is up: time to redraw the screen
                    if (!h.wait(h.ctx, 3 * gapDot)) return false;
                }
            }
        }
        return true;
    }


    // ---- Keyer --------------------------------------------------------------------------------------------------------------

    void Keyer::begin(uint8_t wpm, KeyerMode m) {
        mode = m; unit = dotMs(wpm); state = IDLE; until = 0;
        current = last = started = 0; latchDit = latchDah = prevDit = prevDah = false;
    }

    char Keyer::takeElement() { char e = started; started = 0; return e; }

    void Keyer::startElement(uint32_t now, char type) {
        current = type; started = type; state = ELEMENT;
        until = now + (type == '.' ? 1 : 3) * unit;
        if (type == '.') latchDit = false; else latchDah = false;
    }

    bool Keyer::update(uint32_t now, bool dit, bool dah) {
        bool ditEdge = dit && !prevDit, dahEdge = dah && !prevDah;
        prevDit = dit; prevDah = dah;
        // paddle memory: a press on the other paddle during the element, or any press during the gap
        if (state == ELEMENT) {
            if (ditEdge && current != '.') latchDit = true;
            if (dahEdge && current != '-') latchDah = true;
            if (dit && dah) { if (current == '.') latchDah = true; else latchDit = true; }     // squeeze
        } else if (state == GAP) {
            if (ditEdge) latchDit = true;
            if (dahEdge) latchDah = true;
            if (dit && dah) { if (last == '.') latchDah = true; else latchDit = true; }
        }
        switch (state) {
            case IDLE:
                if (dit || dah) startElement(now, (dit && !dah) ? '.' : (dah && !dit) ? '-' : (last == '.' ? '-' : '.'));
                break;
            case ELEMENT:
                if ((int32_t)(now - until) >= 0) {
                    last = current; state = GAP; until = now + unit;
                    if (mode == KEYER_IAMBIC_A && !dit && !dah) latchDit = latchDah = false;   // mode A: released before the end -> no extra element
                }
                break;
            case GAP:
                if ((int32_t)(now - until) >= 0) {
                    bool wantDit = dit || latchDit, wantDah = dah || latchDah;
                    if (wantDit && wantDah) startElement(now, last == '.' ? '-' : '.');
                    else if (wantDit) startElement(now, '.');
                    else if (wantDah) startElement(now, '-');
                    else { state = IDLE; last = 0; latchDit = latchDah = false; }
                }
                break;
        }
        return state == ELEMENT;
    }

    // ---- Decoder ------------------------------------------------------------------------------------------------------------

    void Decoder::begin(uint8_t wpm, bool adapt) {
        unit = dotMs(wpm); down = false; downAt = upAt = 0; spacePending = false; len = 0; pat[0] = 0; adaptive = adapt;
    }

    uint8_t Decoder::wpm() const { return (uint8_t)(1200u / (unit ? unit : 1)); }

    void Decoder::keyDown(uint32_t now) { if (!down) { down = true; downAt = now; } }

    void Decoder::keyUp(uint32_t now) {
        if (!down) return;
        down = false;
        uint32_t d = now - downAt;
        if (d < 12) return;                                     // rebote de contacto o pico de ruido (< 12 ms; un punto a 40 WPM dura 30 ms)
        upAt = now;
        bool dash = d >= 2 * unit;
        if (len < sizeof(pat) - 1) { pat[len++] = dash ? '-' : '.'; pat[len] = 0; }
        if (adaptive && d <= unit * 6) {                        // una pulsacion larguisima (portadora, manipulador atascado) no desajusta la velocidad
            uint32_t est = dash ? d / 3 : d;
            unit = (unit * 7 + est * 3) / 10;
            uint32_t lo = 1200u / WPM_MAX, hi = 1200u / WPM_MIN;
            if (unit < lo) unit = lo;
            if (unit > hi) unit = hi;
        }
    }

    char Decoder::tick(uint32_t now) {
        if (down) return 0;
        uint32_t quiet = now - upAt;
        if (len > 0 && quiet >= unit * 5 / 2) {                  // 2.5 units: letter complete
            char c = charForPattern(pat);
            len = 0; pat[0] = 0; spacePending = true;
            return c ? c : '*';
        }
        if (len == 0 && spacePending && quiet >= unit * 11 / 2) {     // 5.5 units: word gap
            spacePending = false;
            return ' ';
        }
        return 0;
    }


    // ---- Envelope -----------------------------------------------------------------------------------------------------------

    void Envelope::begin() { smooth = noiseDb = peakDb = 0; on = false; init = false; }

    bool Envelope::update(float rssi) {
        if (!init) { smooth = noiseDb = peakDb = rssi; init = true; return false; }
        smooth += 0.4f * (rssi - smooth);
        // noise floor: follows the minimum quickly, rises very slowly
        if (smooth < noiseDb) noiseDb += 0.2f * (smooth - noiseDb);
        else                  noiseDb += 0.0004f * (smooth - noiseDb);
        // peak: follows the maximum quickly, decays slowly towards the noise floor
        if (smooth > peakDb)  peakDb += 0.3f * (smooth - peakDb);
        else                  peakDb += 0.0003f * (smooth - peakDb);
        if (peakDb < noiseDb) peakDb = noiseDb;
        if (!valid()) { on = false; return false; }
        const float hyst = 1.0f;
        float thr = threshold();
        if (!on && smooth > thr + hyst) on = true;
        else if (on && smooth < thr - hyst) on = false;
        return on;
    }

}
