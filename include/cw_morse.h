/* Copyright (C) 2026 Manuel - EA7JRS
 * Morse (CW) encoder with PARIS timing. Pure C++ (no Arduino dependencies) so it can be unit tested on a PC.
 * Part of AIS/LoRa APRS Tracker - Manuel Canca EA7JRS (based on LoRa APRS Tracker by Ricardo Guzman CA2RXU, GPL-3.0)
 */
#ifndef CW_MORSE_H_
#define CW_MORSE_H_

#include <stddef.h>
#include <stdint.h>

namespace CWMorse {

    const uint8_t WPM_MIN = 5;
    const uint8_t WPM_MAX = 40;

    // ".-" style pattern of a character (case insensitive); nullptr if it has no Morse code
    const char* pattern(char c);

    // Milliseconds of one dot at the given speed (PARIS standard: 1200 / wpm)
    uint32_t dotMs(uint8_t wpm);

    // Character for a ".-" pattern; 0 if unknown
    char charForPattern(const char* pattern);

    // Total on-air time of a text in ms (including the final letter gap)
    uint32_t totalMs(const char* text, uint8_t wpm);

    struct Hooks {
        void  (*key)(void* ctx, bool on);                       // carrier on / off
        bool  (*wait)(void* ctx, uint32_t ms);                  // wait ms (accumulated schedule). false = abort the transmission
        void  (*progress)(void* ctx, size_t done, size_t total, const char* pattern, uint8_t element);   // called with the key up
        void*  ctx;
    };

    // Sends the text. Characters without Morse code are skipped. Returns true if it finished, false if aborted
    // gapWpm (Farnsworth): speed used for letter and word gaps; 0 or >= wpm = same speed as the characters
    bool send(const char* text, uint8_t wpm, const Hooks& hooks, uint8_t gapWpm = 0);

    // Koch method order (LCWO): the first N characters are the ones to learn at level N
    const char* const KOCH_ORDER = "KMRSUAPTLOWI.NJEF0Y,VG5/Q9ZH38B?427C1D6X";
    const uint8_t KOCH_COUNT = 41;

    // ---- Keyer: paddles -> timed carrier (iambic A / B). Call update() often (every ms) ----------------------------------
    enum KeyerMode : uint8_t { KEYER_IAMBIC_A = 0, KEYER_IAMBIC_B = 1 };

    class Keyer {
    public:
        void begin(uint8_t wpm, KeyerMode mode);
        bool update(uint32_t now, bool dit, bool dah);      // returns true while the carrier must be on
        char takeElement();                                 // '.', '-' or 0: element that has just started (for decoding)
    private:
        enum State : uint8_t { IDLE, ELEMENT, GAP };
        State     state;
        KeyerMode mode;
        uint32_t  unit, until;
        char      current, last, started;
        bool      latchDit, latchDah, prevDit, prevDah;
        void      startElement(uint32_t now, char type);
    };

    // ---- Envelope: instantaneous RSSI (dBm, ~1 kHz sampling) -> keyed on/off with adaptive threshold ----------------------
    class Envelope {
    public:
        void  begin();
        bool  update(float rssiDbm);            // returns true while a carrier is detected
        float noise() const { return noiseDb; }
        float peak() const { return peakDb; }
        float threshold() const { return noiseDb + (peakDb - noiseDb) * 0.5f; }
        bool  valid() const { return peakDb - noiseDb >= MIN_SNR_DB; }
        float level() const { return smooth; }
        static constexpr float MIN_SNR_DB = 6.0f;
    private:
        float smooth, noiseDb, peakDb;
        bool  on, init;
    };

    // ---- Decoder: key up/down timing -> characters ------------------------------------------------------------------------
    class Decoder {
    public:
        void begin(uint8_t wpm, bool adaptive);
        void keyDown(uint32_t now);
        void keyUp(uint32_t now);
        // Call often. Returns a character when a letter is complete, ' ' for a word gap, 0 otherwise
        char tick(uint32_t now);
        const char* pending() const { return pat; }         // elements of the letter in progress
        uint8_t wpm() const;                                // current speed estimate
    private:
        uint32_t unit, downAt, upAt;
        bool     down, spacePending;
        char     pat[10];
        uint8_t  len;
        bool     adaptive;
    };

}

#endif
