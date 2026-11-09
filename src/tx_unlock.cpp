/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Desbloqueo del rango de emision. Ver include/tx_unlock.h
 */

#ifdef HAS_SA868

#include "tx_unlock.h"
#include "tx_unlock_key.h"
#include <Preferences.h>
#include <esp_mac.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/bignum.h>
#include <mbedtls/sha256.h>
#include <logger.h>

extern logging::Logger logger;

namespace TX_Unlock {

    static int8_t   state = -1;            // -1 sin leer, 0 bloqueado, 1 desbloqueado
    static char     macHex[13] = "";
    static uint8_t  fails = 0;
    static uint32_t lockoutUntil = 0;

    static int hexVal(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    // cifras hexadecimales de text (sin separadores) -> bytes; devuelve cuantos
    static size_t hexBytes(const char* text, uint8_t* out, size_t max) {
        size_t n = 0; int hi = -1;
        for (const char* p = text ? text : ""; *p; p++) {
            const int v = hexVal(*p);
            if (v < 0) continue;
            if (hi < 0) hi = v;
            else { if (n >= max) return max + 1; out[n++] = (uint8_t)(hi << 4 | v); hi = -1; }
        }
        return hi >= 0 ? max + 1 : n;
    }

    bool available() { return strlen(TX_UNLOCK_PUBKEY) == 130; }

    const char* deviceId() {
        if (!macHex[0]) {
            uint8_t m[6] = { 0 };
            esp_read_mac(m, ESP_MAC_WIFI_STA);
            snprintf(macHex, sizeof(macHex), "%02X%02X%02X%02X%02X%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
        }
        return macHex;
    }

    static bool verify(const uint8_t sig[64]) {
        uint8_t pub[65];
        if (hexBytes(TX_UNLOCK_PUBKEY, pub, sizeof(pub)) != 65) return false;
        char msg[48];
        snprintf(msg, sizeof(msg), "APRS-LoRa-Plus TX unlock %s", deviceId());
        uint8_t hash[32];
        mbedtls_sha256((const uint8_t*)msg, strlen(msg), hash, 0);
        mbedtls_ecp_group grp; mbedtls_ecp_point q; mbedtls_mpi r, s;
        mbedtls_ecp_group_init(&grp); mbedtls_ecp_point_init(&q); mbedtls_mpi_init(&r); mbedtls_mpi_init(&s);
        bool good = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
                    mbedtls_ecp_point_read_binary(&grp, &q, pub, sizeof(pub)) == 0 &&
                    mbedtls_mpi_read_binary(&r, sig, 32) == 0 && mbedtls_mpi_read_binary(&s, sig + 32, 32) == 0 &&
                    mbedtls_ecdsa_verify(&grp, hash, sizeof(hash), &q, &r, &s) == 0;
        mbedtls_mpi_free(&r); mbedtls_mpi_free(&s); mbedtls_ecp_point_free(&q); mbedtls_ecp_group_free(&grp);
        return good;
    }

    bool unlocked() {
        if (state < 0) {
            Preferences p;
            p.begin("txunlock", true);
            const String id = p.getString("id", "");
            state = (available() && p.getBool("on", false) && id == deviceId()) ? 1 : 0;
            p.end();
        }
        return state == 1;
    }

    bool tryUnlock(const char* code, const char** why) {
        if (why) *why = "";
        if (!available()) { if (why) *why = "this firmware cannot be unlocked"; return false; }
        if (lockoutUntil && (int32_t)(millis() - lockoutUntil) < 0) { if (why) *why = "too many attempts: wait 10 min"; return false; }
        uint8_t sig[64];
        const bool good = hexBytes(code, sig, sizeof(sig)) == sizeof(sig) && verify(sig);
        if (!good) {
            if (++fails >= 5) { fails = 0; lockoutUntil = millis() + 600000UL; if (!lockoutUntil) lockoutUntil = 1; }
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "TX", "Wrong TX unlock code");
            if (why) *why = "wrong code for this device";
            return false;
        }
        fails = 0;
        Preferences p;
        p.begin("txunlock", false);
        p.putBool("on", true);
        p.putString("id", deviceId());
        p.end();
        state = 1;
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "TX", "TX range UNLOCKED: the whole band of the radio module (your licence)");
        return true;
    }

    void lock() {
        Preferences p;
        p.begin("txunlock", false);
        p.putBool("on", false);
        p.end();
        state = 0;
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "TX", "TX range locked to the amateur bands");
    }
}

#endif
