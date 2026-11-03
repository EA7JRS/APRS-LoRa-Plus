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

#include "fst4w.h"
#include <string.h>
#include "fst4w_table.h"
#include "ft8/message.h"

namespace FST4W {

    bool validDbm(int dbm) {
        if (dbm < 0 || dbm > 60) return false;
        const int idbm = (int)(0.3f * (float)dbm + 0.5f);            // nint(0.3 * dbm)
        const int back = (int)((float)idbm * 10.0f / 3.0f + 0.5f);   // nint(idbm * 10 / 3)
        return back == dbm;
    }

    int nearestValidDbm(int dbm) {
        if (dbm < 0) dbm = 0;
        if (dbm > 60) dbm = 60;
        for (int d = 0; d <= 60; d++) {
            if (dbm - d >= 0 && validDbm(dbm - d)) return dbm - d;
            if (dbm + d <= 60 && validDbm(dbm + d)) return dbm + d;
        }
        return 0;
    }

    uint32_t crc24(const uint8_t* bits, size_t n) {
        uint32_t reg = 0;
        for (size_t i = 0; i < n; i++) {
            const uint32_t top = (reg >> 23) & 1U;
            reg = ((reg << 1) | (bits[i] & 1U)) & 0xFFFFFFUL;
            if (top) reg ^= 0x00065BUL;
        }
        return reg;
    }

    bool encodeBits(const char* call, const char* grid4, int dbm, uint8_t codeword[CODE_BITS]) {
        if (call == nullptr || grid4 == nullptr || codeword == nullptr || !validDbm(dbm)) return false;
        if (strlen(grid4) != 4) return false;
        if (grid4[0] < 'A' || grid4[0] > 'R' || grid4[1] < 'A' || grid4[1] > 'R' || grid4[2] < '0' || grid4[2] > '9' || grid4[3] < '0' || grid4[3] > '9') return false;
        // n28 del indicativo: los 28 primeros bits de un mensaje estandar "CALL CALL" de ft8_lib
        ftx_message_t msg;
        ftx_message_init(&msg);
        if (ftx_message_encode_std(&msg, nullptr, call, call, "") != FTX_MESSAGE_RC_OK) return false;
        const uint32_t n28 = ((uint32_t)msg.payload[0] << 20) | ((uint32_t)msg.payload[1] << 12) | ((uint32_t)msg.payload[2] << 4) | ((uint32_t)msg.payload[3] >> 4);
        const uint32_t igrid4 = (uint32_t)(grid4[0] - 'A') * 1800U + (uint32_t)(grid4[1] - 'A') * 100U + (uint32_t)(grid4[2] - '0') * 10U + (uint32_t)(grid4[3] - '0');
        const uint32_t idbm = (uint32_t)(int)(0.3f * (float)dbm + 0.5f);
        // 50 bits: n28 (28) | igrid4 (15) | idbm (5) | 0 | 0 ; despues 24 bits de CRC
        uint8_t m[74];
        memset(m, 0, sizeof(m));
        for (int i = 0; i < 28; i++) m[i] = (uint8_t)((n28 >> (27 - i)) & 1U);
        for (int i = 0; i < 15; i++) m[28 + i] = (uint8_t)((igrid4 >> (14 - i)) & 1U);
        for (int i = 0; i < 5; i++)  m[43 + i] = (uint8_t)((idbm >> (4 - i)) & 1U);
        const uint32_t crc = crc24(m, 74);
        for (int i = 0; i < 24; i++) m[50 + i] = (uint8_t)((crc >> (23 - i)) & 1U);
        memcpy(codeword, m, 74);
        for (int r = 0; r < 166; r++) {
            uint8_t p = 0;
            for (int j = 0; j < 74; j++) p ^= (uint8_t)(m[j] & ((GEN[r][j >> 3] >> (7 - (j & 7))) & 1U));
            codeword[74 + r] = p;
        }
        return true;
    }

    bool encode(const char* call, const char* grid4, int dbm, uint8_t tones[SYMBOLS]) {
        if (tones == nullptr) return false;
        uint8_t cw[CODE_BITS];
        if (!encodeBits(call, grid4, dbm, cw)) return false;
        static const uint8_t sync1[8] = { 0, 1, 3, 2, 1, 0, 2, 3 };
        static const uint8_t sync2[8] = { 2, 3, 1, 0, 3, 2, 0, 1 };
        uint8_t d[DATA_SYMS];
        for (int i = 0; i < DATA_SYMS; i++) {                          // mapa Gray: 00 -> 0, 01 -> 1, 11 -> 2, 10 -> 3
            const int b = cw[2 * i] * 2 + cw[2 * i + 1];
            d[i] = (uint8_t)(b == 0 ? 0 : (b == 1 ? 1 : (b == 3 ? 2 : 3)));
        }
        int k = 0;
        for (int blk = 0; blk < 4; blk++) {
            const uint8_t* s = (blk % 2 == 0) ? sync1 : sync2;
            for (int i = 0; i < 8; i++) tones[k++] = s[i];
            for (int i = 0; i < 30; i++) tones[k++] = d[blk * 30 + i];
        }
        for (int i = 0; i < 8; i++) tones[k++] = sync1[i];
        return true;
    }
}
