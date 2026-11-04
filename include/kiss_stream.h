/* Copyright (C) 2026 Manuel - EA7JRS
 * KISS over WiFi (5.3.2): pure helpers, tested on a PC (tools/kiss_net/test_kiss_stream.cpp).
 *   - FrameSplitter: cuts a TCP / UDP byte stream into complete KISS frames (FEND ... FEND).
 *   - ipAllowed: checks the address of a client against the list set in the WebUI.
 * Part of AIS/LoRa APRS Tracker - Manuel Canca EA7JRS (based on LoRa APRS Tracker by Ricardo Guzman CA2RXU, GPL-3.0)
 */
#ifndef KISS_STREAM_H_
#define KISS_STREAM_H_

#include <string>
#include <stddef.h>
#include <string.h>

namespace KissNetUtil {

    constexpr size_t MAX_BUFFER = 1024;                 // same limit as the BLE KISS buffer
    constexpr char   FEND_BYTE  = (char)0xC0;

    class FrameSplitter {
    public:
        // Adds received bytes. Returns false (and drops everything) if the buffer would overflow.
        bool feed(const unsigned char* data, size_t length) {
            if (buffer.size() + length > MAX_BUFFER) { buffer.clear(); return false; }
            buffer.append(reinterpret_cast<const char*>(data), length);
            return true;
        }

        // Takes the next complete frame (FEND, command, data, FEND; at least 4 bytes). Garbage before a FEND and empty frames
        // (FEND FEND) are skipped. The closing FEND stays in the buffer: it may also open the next frame.
        bool next(std::string& frame) {
            for (;;) {
                const size_t start = buffer.find(FEND_BYTE);
                if (start == std::string::npos) { buffer.clear(); return false; }
                if (start > 0) buffer.erase(0, start);
                const size_t end = buffer.find(FEND_BYTE, 1);
                if (end == std::string::npos) return false;                 // incomplete: wait for more bytes
                if (end == 1) { buffer.erase(0, 1); continue; }              // FEND FEND: the second one opens the frame
                frame = buffer.substr(0, end + 1);
                buffer.erase(0, end);
                if (frame.size() >= 4) return true;
            }
        }

        void clear() { buffer.clear(); }
        size_t pending() const { return buffer.size(); }

    private:
        std::string buffer;
    };

    // list: addresses separated by commas, semicolons or spaces. An entry ending in '.' is a prefix ("192.168.1." = that whole network).
    // Empty list = any client of the network the tracker has joined.
    inline bool ipAllowed(const char* ip, const char* list) {
        if (ip == nullptr) return false;
        if (list == nullptr) return true;
        bool any = false;
        const char* p = list;
        while (*p) {
            while (*p == ',' || *p == ';' || *p == ' ' || *p == '\t') p++;
            const char* tokenStart = p;
            while (*p && *p != ',' && *p != ';' && *p != ' ' && *p != '\t') p++;
            const size_t n = (size_t)(p - tokenStart);
            if (n == 0) continue;
            any = true;
            if (tokenStart[n - 1] == '.') {
                if (strncmp(ip, tokenStart, n) == 0) return true;
            } else if (strlen(ip) == n && strncmp(ip, tokenStart, n) == 0) {
                return true;
            }
        }
        return !any;
    }

}

#endif
