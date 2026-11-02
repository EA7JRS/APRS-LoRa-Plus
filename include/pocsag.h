/*
 * Decodificador POCSAG (CCIR Radiopaging Code No. 1) para el modo DAPNET: BCH(31,21) con paridad, busqueda de la palabra de sincronismo
 * a nivel de bit (el flujo del chip llega en bloques de bytes sin alinear), lotes de 16 palabras, direcciones (RIC) con su funcion,
 * mensajes numericos (BCD) y alfanumericos (7 bits, primero el bit menos significativo) y conversion del juego de caracteres.
 * Funciones sin dependencias de Arduino: se prueban en un PC (tools/test_pocsag.cpp).
 *
 * Estructura (la del estandar POCSAG, que usa DAPNET):
 *   - preambulo: 576 bits alternados; despues cada lote empieza con la palabra de sincronismo 0x7CD215D8 y lleva 16 palabras
 *     de 32 bits en 8 tramas de 2; la palabra de relleno es 0x7A89C197.
 *   - palabra: 31 bits BCD(31,21) (21 de datos + 10 de control, polinomio x^10+x^9+x^8+x^6+x^5+x^3+1) + 1 bit de paridad par.
 *   - direccion (bit 31 = 0): 18 bits altos del RIC + 2 bits de funcion; los 3 bits bajos del RIC son el numero de trama del lote.
 *   - mensaje (bit 31 = 1): 20 bits de datos, el primero transmitido es el de mayor peso.
 *
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#ifndef POCSAG_H_
#define POCSAG_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

namespace Pocsag {

    const uint32_t SYNC_WORD = 0x7CD215D8UL;
    const uint32_t IDLE_WORD = 0x7A89C197UL;
    const uint16_t MAX_RAW_BYTES = 176;                     // 1408 bits: 201 caracteres de 7 bits
    const uint8_t  MAX_TEXT = 200;

    // ---- BCH(31,21) y paridad --------------------------------------------------------------------------------------
    inline uint8_t parity32(uint32_t v) {
        v ^= v >> 16; v ^= v >> 8; v ^= v >> 4; v ^= v >> 2; v ^= v >> 1;
        return (uint8_t)(v & 1);
    }

    inline uint32_t syndrome31(uint32_t w) {                // resto de dividir la palabra de 31 bits por el generador
        for (int i = 30; i >= 10; i--) if ((w >> i) & 1) w ^= 0x769UL << (i - 10);
        return w & 0x3FFUL;
    }

    // Palabra de 32 bits a partir de 21 bits de datos
    inline uint32_t encodeWord(uint32_t data21) {
        uint32_t w = (data21 & 0x1FFFFFUL) << 10;
        w |= syndrome31(w);
        return (w << 1) | parity32(w);
    }

    // Comprueba y corrige hasta maxFix bits erroneos (0 a 2). Devuelve false si no es una palabra valida. errors = bits corregidos
    inline bool fixWord(uint32_t& cw, uint8_t& errors, uint8_t maxFix = 2) {
        errors = 0;
        uint32_t w = cw >> 1;
        if (syndrome31(w) == 0) {
            if (parity32(cw) == 0) return true;
            if (maxFix >= 1) { cw ^= 1; errors = 1; return true; }       // solo fallaba el bit de paridad
            return false;
        }
        if (maxFix >= 1) {
            for (int i = 0; i < 31; i++) {
                if (syndrome31(w ^ (1UL << i)) == 0) {
                    uint32_t fixed = ((w ^ (1UL << i)) << 1) | (cw & 1);
                    if (parity32(fixed) == 0) { cw = fixed; errors = 1; return true; }
                    if (maxFix >= 2) { cw = fixed ^ 1; errors = 2; return true; }
                    return false;
                }
            }
        }
        if (maxFix >= 2) {
            for (int i = 0; i < 31; i++) {
                for (int j = i + 1; j < 31; j++) {
                    if (syndrome31(w ^ (1UL << i) ^ (1UL << j)) == 0) {
                        uint32_t fixed = ((w ^ (1UL << i) ^ (1UL << j)) << 1) | (cw & 1);
                        if (parity32(fixed) == 0) { cw = fixed; errors = 2; return true; }     // con 2 correcciones se exige paridad correcta
                        return false;
                    }
                }
            }
        }
        return false;
    }

    // ---- Mensaje y decodificador -----------------------------------------------------------------------------------
    struct Message {
        uint32_t ric        = 0;
        uint8_t  func       = 0;            // 0 a 3
        uint16_t bits       = 0;            // bits de datos acumulados (multiplo de 20)
        bool     corrupt    = false;        // alguna palabra del mensaje no se pudo recuperar
        bool     truncated  = false;        // mas largo que el buffer
        uint8_t  corrected  = 0;            // bits corregidos por el BCH (suma)
        uint8_t  raw[MAX_RAW_BYTES];
        Message() { memset(raw, 0, sizeof(raw)); }
    };

    typedef void (*MessageFn)(const Message& message, void* context);

    class Decoder {
    public:
        // maxFix: bits que se corrigen por palabra (0 a 2)
        void reset(uint8_t maxFix = 2) {
            maxFix_ = maxFix > 2 ? 2 : maxFix;
            state_ = HUNT; raw_ = 0; invert_ = false; bitCount_ = 0; word_ = 0; cwIndex_ = 0;
            active_ = false; batches_ = 0; messages_ = 0; badWords_ = 0; syncs_ = 0;
        }

        // Bloque de bytes del chip: bits de mayor a menor peso, sin alineacion
        void feed(const uint8_t* data, size_t len, MessageFn fn, void* ctx) {
            for (size_t i = 0; i < len; i++)
                for (int b = 7; b >= 0; b--) pushBit((data[i] >> b) & 1, fn, ctx);
        }

        // Cierra el mensaje en curso (final de bloque sin mas lotes)
        void flush(MessageFn fn, void* ctx) { finish(fn, ctx); state_ = HUNT; bitCount_ = 0; }

        bool     inBatch() const    { return state_ != HUNT; }
        bool     inverted() const   { return invert_; }
        uint32_t batches() const    { return batches_; }
        uint32_t messages() const   { return messages_; }
        uint32_t badWords() const   { return badWords_; }
        uint32_t syncs() const      { return syncs_; }

    private:
        enum State : uint8_t { HUNT, BATCH, EXPECT_SYNC };

        static uint8_t dist(uint32_t a, uint32_t b) { uint32_t x = a ^ b; uint8_t n = 0; while (x) { n++; x &= x - 1; } return n; }

        void pushBit(uint8_t b, MessageFn fn, void* ctx) {
            raw_ = (raw_ << 1) | b;
            if (state_ == HUNT) {
                if (dist(raw_, SYNC_WORD) <= 2)         { invert_ = false; startBatch(); }
                else if (dist(~raw_, SYNC_WORD) <= 2)   { invert_ = true;  startBatch(); }
                return;
            }
            word_ = (word_ << 1) | (b ^ (invert_ ? 1 : 0));
            if (++bitCount_ < 32) return;
            bitCount_ = 0;
            if (state_ == EXPECT_SYNC) {                // tras el lote: otro sincronismo (sigue la emision) o fin de la transmision
                if (dist(word_, SYNC_WORD) <= 3) startBatch();
                else { finish(fn, ctx); state_ = HUNT; }
                return;
            }
            codeword(word_, fn, ctx);
            if (++cwIndex_ >= 16) state_ = EXPECT_SYNC;
        }

        void startBatch() { state_ = BATCH; bitCount_ = 0; word_ = 0; cwIndex_ = 0; batches_++; syncs_++; }

        void codeword(uint32_t cw, MessageFn fn, void* ctx) {
            uint8_t errors = 0;
            if (!fixWord(cw, errors, maxFix_)) {
                badWords_++;
                if (active_ && !msg_.corrupt) { msg_.corrupt = true; }
                return;
            }
            if (cw == IDLE_WORD) { finish(fn, ctx); return; }
            if ((cw >> 31) == 0) {                      // direccion
                finish(fn, ctx);
                msg_ = Message();
                msg_.ric  = (((cw >> 13) & 0x3FFFFUL) << 3) | (uint32_t)(cwIndex_ >> 1);
                msg_.func = (uint8_t)((cw >> 11) & 3);
                active_ = true;
                return;
            }
            if (!active_) return;                       // palabra de mensaje sin direccion (se perdio la cabecera)
            msg_.corrected = (uint8_t)(msg_.corrected + errors);
            if (msg_.bits + 20 > MAX_RAW_BYTES * 8) { msg_.truncated = true; return; }
            uint32_t data = (cw >> 11) & 0xFFFFFUL;
            for (int k = 19; k >= 0; k--) {
                const uint16_t pos = msg_.bits++;
                if ((data >> k) & 1) msg_.raw[pos >> 3] |= (uint8_t)(0x80 >> (pos & 7));
            }
        }

        void finish(MessageFn fn, void* ctx) {
            if (!active_) return;
            active_ = false;
            messages_++;
            if (fn) fn(msg_, ctx);
        }

        uint8_t  maxFix_ = 2;
        State    state_ = HUNT;
        uint32_t raw_ = 0, word_ = 0;
        bool     invert_ = false;
        uint8_t  bitCount_ = 0, cwIndex_ = 0;
        bool     active_ = false;
        Message  msg_;
        uint32_t batches_ = 0, messages_ = 0, badWords_ = 0, syncs_ = 0;
    };

    // ---- Contenido del mensaje --------------------------------------------------------------------------------------
    inline bool rawBit(const Message& m, uint16_t pos) { return (m.raw[pos >> 3] >> (7 - (pos & 7))) & 1; }

    // Juego de caracteres del texto: 0 = ASCII; 1 = ISO 646 aleman (DIN 66003: [ \ ] { | } ~ son A O U a o u ss), que sale sin acentos
    enum Charset : uint8_t { CHARSET_ASCII = 0, CHARSET_DIN66003 = 1 };

    inline size_t appendChar(char* out, size_t cap, size_t used, char c, uint8_t charset) {
        const char* seq = nullptr;
        if (charset == CHARSET_DIN66003) {
            switch (c) {
                case '[': seq = "Ae"; break; case '\\': seq = "Oe"; break; case ']': seq = "Ue"; break;
                case '{': seq = "ae"; break; case '|':  seq = "oe"; break; case '}': seq = "ue"; break;
                case '~': seq = "ss"; break;
                default: break;
            }
        }
        if (seq) { for (; *seq && used + 1 < cap; seq++) out[used++] = *seq; return used; }
        if (used + 1 < cap) out[used++] = c;
        return used;
    }

    // Alfanumerico: caracteres de 7 bits, el primero transmitido es el de menor peso. Termina en NUL o EOT
    inline size_t decodeAlpha(const Message& m, char* out, size_t cap, uint8_t charset = CHARSET_ASCII) {
        size_t used = 0;
        if (cap == 0) return 0;
        for (uint16_t pos = 0; pos + 7 <= m.bits; pos += 7) {
            uint8_t c = 0;
            for (uint8_t j = 0; j < 7; j++) if (rawBit(m, pos + j)) c |= (uint8_t)(1 << j);
            if (c == 0x00 || c == 0x04) break;
            if (c < 0x20) c = ' ';                       // saltos de linea y controles
            used = appendChar(out, cap, used, (char)c, charset);
        }
        out[used] = 0;
        return used;
    }

    // Numerico: BCD de 4 bits, el primero transmitido es el de menor peso
    inline size_t decodeNumeric(const Message& m, char* out, size_t cap) {
        static const char TABLE[] = "0123456789*U -][";
        size_t used = 0;
        if (cap == 0) return 0;
        for (uint16_t pos = 0; pos + 4 <= m.bits && used + 1 < cap; pos += 4) {
            uint8_t v = 0;
            for (uint8_t j = 0; j < 4; j++) if (rawBit(m, pos + j)) v |= (uint8_t)(1 << j);
            out[used++] = TABLE[v];
        }
        while (used > 0 && out[used - 1] == ' ') used--;     // el relleno final es 0 (= '0') o espacios segun el transmisor
        out[used] = 0;
        return used;
    }

    // ---- Hora difundida (tiempo UTC / local segun el RIC) ----------------------------------------------------------------
    struct TimeStamp { uint16_t year; uint8_t month, day, hour, minute, second; };

    inline bool plausible(const TimeStamp& t) {
        return t.year >= 2024 && t.year <= 2099 && t.month >= 1 && t.month <= 12 && t.day >= 1 && t.day <= 31 && t.hour < 24 && t.minute < 60 && t.second < 61;
    }

    inline int digitsOf(const char* text, char* digits, size_t cap) {      // solo los digitos, en orden
        size_t n = 0;
        for (const char* p = text; *p; p++) if (*p >= '0' && *p <= '9') { if (n + 1 >= cap) break; digits[n++] = *p; }
        digits[n] = 0;
        return (int)n;
    }

    inline int num(const char* s, int len) { int v = 0; for (int i = 0; i < len; i++) v = v * 10 + (s[i] - '0'); return v; }

    // Formatos: "HHMMSS DDMMAA" (Skyper), AAMMDDHHMMSS, HHMMSSDDMMAA (12 digitos) y AAAAMMDDHHMMSS (14 digitos).
    // Con 12 digitos se prueba primero HHMMSSDDMMAA (mismo orden que el de Skyper) y despues AAMMDDHHMMSS; gana el primero con valores plausibles
    inline bool parseTime(const char* text, TimeStamp& out) {
        char d[24];
        int n = digitsOf(text, d, sizeof(d));
        TimeStamp t;
        if (n >= 14) {                                  // AAAAMMDDHHMMSS (se ignora el relleno que pueda haber detras)
            t = { (uint16_t)num(d, 4), (uint8_t)num(d + 4, 2), (uint8_t)num(d + 6, 2), (uint8_t)num(d + 8, 2), (uint8_t)num(d + 10, 2), (uint8_t)num(d + 12, 2) };
            if (plausible(t)) { out = t; return true; }
        }
        if (n >= 12) {                                  // los 12 primeros digitos (el relleno numerico del final no cuenta)
            t = { (uint16_t)(2000 + num(d + 10, 2)), (uint8_t)num(d + 8, 2), (uint8_t)num(d + 6, 2), (uint8_t)num(d, 2), (uint8_t)num(d + 2, 2), (uint8_t)num(d + 4, 2) };
            if (plausible(t)) { out = t; return true; }
            t = { (uint16_t)(2000 + num(d, 2)), (uint8_t)num(d + 2, 2), (uint8_t)num(d + 4, 2), (uint8_t)num(d + 6, 2), (uint8_t)num(d + 8, 2), (uint8_t)num(d + 10, 2) };
            if (plausible(t)) { out = t; return true; }
        }
        return false;
    }

    // Lista de RIC separados por coma, espacio o punto y coma. Devuelve cuantos hay (maximo max)
    inline uint8_t parseRicList(const char* text, uint32_t* out, uint8_t maxCount) {
        uint8_t n = 0;
        uint64_t value = 0;
        bool inNumber = false;
        for (const char* p = text ? text : ""; ; p++) {
            const char c = *p;
            if (c >= '0' && c <= '9') { value = value * 10 + (uint64_t)(c - '0'); inNumber = true; if (value > 0x1FFFFFULL) value = 0x1FFFFFULL + 1; }
            else {
                if (inNumber && value <= 0x1FFFFFULL && n < maxCount) out[n++] = (uint32_t)value;      // RIC de 21 bits (0 a 2097151)
                value = 0; inNumber = false;
            }
            if (c == 0) break;
        }
        return n;
    }

}

#endif
