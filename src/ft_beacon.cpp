/* Copyright (C) 2026 Manuel - EA7JRS
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

#include "ft_beacon.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "ft8/constants.h"
#include "ft8/encode.h"
#include "ft8/message.h"

namespace FTX {

    const char* name(Proto p) { return p == PROTO_FT4 ? "FT4" : "FT8"; }
    uint8_t     symbols(Proto p) { return p == PROTO_FT4 ? (uint8_t)FT4_NN : (uint8_t)FT8_NN; }
    uint32_t    symbolUs(Proto p) { return p == PROTO_FT4 ? 48000UL : 160000UL; }
    uint32_t    slotMs(Proto p) { return p == PROTO_FT4 ? 7500UL : 15000UL; }
    uint32_t    startDelayMs(Proto p) { return p == PROTO_FT4 ? 300UL : 500UL; }
    uint8_t     stepsPerSymbol(Proto p) { return p == PROTO_FT4 ? (uint8_t)20 : (uint8_t)64; }
    double      toneSpacingHz(Proto p) { return 1000000.0 / (double)symbolUs(p); }

    size_t buildText(const char* custom, const char* call, const char* grid, char* out, size_t cap) {
        if (out == nullptr || cap == 0) return 0;
        char tmp[MAX_TEXT + 4];
        tmp[0] = '\0';
        if (custom != nullptr && custom[0] != '\0') snprintf(tmp, sizeof(tmp), "%.*s", (int)MAX_TEXT - 1, custom);
        else snprintf(tmp, sizeof(tmp), "CQ %.13s %.4s", call != nullptr ? call : "", grid != nullptr ? grid : "");
        size_t n = 0;
        for (size_t i = 0; tmp[i] != '\0' && n + 1 < cap; i++) {         // mayusculas
            char c = tmp[i];
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            out[n++] = c;
        }
        out[n] = '\0';
        return n;
    }

    bool encode(Proto p, const char* text, uint8_t tones[MAX_SYMBOLS]) {
        if (text == nullptr || tones == nullptr) return false;
        ftx_message_t msg;
        ftx_message_init(&msg);
        const ftx_message_rc_t rc = ftx_message_encode(&msg, nullptr, text);
        if (rc != FTX_MESSAGE_RC_OK) return false;
        if (p == PROTO_FT4) ft4_encode(msg.payload, tones);
        else                ft8_encode(msg.payload, tones);
        return true;
    }

    uint32_t msUntilSlot(Proto p, uint32_t msOfDay, uint16_t everyCycles, uint16_t offset) {
        if (everyCycles < 1) everyCycles = 1;
        const uint32_t slot = slotMs(p);
        const uint32_t delay = startDelayMs(p);
        const uint32_t DAY = 86400000UL;
        const uint32_t base = msOfDay / slot;
        for (uint32_t k = 0; k <= (uint32_t)everyCycles + 1UL; k++) {
            const uint32_t start = (base + k) * slot + delay;
            if (start < msOfDay) continue;
            const uint32_t idx = (((start - delay) % DAY) / slot) % everyCycles;      // el numero de ranura se reinicia a las 00:00 UTC
            if (idx == (uint32_t)(offset % everyCycles)) return start - msOfDay;
        }
        return slot;
    }

    uint16_t slotOffset(const char* call, uint16_t everyCycles) {
        if (everyCycles < 1) everyCycles = 1;
        uint32_t h = 7;
        for (const char* q = call; q != nullptr && *q != '\0'; q++) h = h * 31 + (uint8_t)*q;
        return (uint16_t)(h % everyCycles);
    }

    void Shaper::begin(Proto p, const uint8_t* tones, double tone0Hz) {
        tones_ = tones;
        nsym_ = symbols(p);
        n_ = stepsPerSymbol(p);
        tone0_ = tone0Hz;
        spacing_ = toneSpacingHz(p);
        const double bt = (p == PROTO_FT4) ? 1.0 : 2.0;
        const double c = 5.336446;                                     // pi * sqrt(2 / ln 2)
        for (uint16_t i = 0; i < 3U * n_; i++) {                       // pulso gaussiano de 3 simbolos, muestreado a mitad de cada paso
            const double u = ((double)i + 0.5) / (double)n_ - 1.5;
            pulse_[i] = (float)(0.5 * (erf(c * bt * (u + 0.5)) - erf(c * bt * (u - 0.5))));
        }
    }

    double Shaper::hz(uint8_t sym, uint8_t step) const {
        if (tones_ == nullptr || sym >= nsym_ || step >= n_) return tone0_;
        const uint8_t prev = sym > 0 ? tones_[sym - 1] : tones_[0];                 // los simbolos ficticios de los extremos repiten el primero y el ultimo
        const uint8_t cur  = tones_[sym];
        const uint8_t next = (uint8_t)(sym + 1U) < nsym_ ? tones_[sym + 1] : tones_[nsym_ - 1];
        const double dev = (double)prev * (double)pulse_[2U * n_ + step] + (double)cur * (double)pulse_[n_ + step] + (double)next * (double)pulse_[step];
        return tone0_ + spacing_ * dev;
    }
}
