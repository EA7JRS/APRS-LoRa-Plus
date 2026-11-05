/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of LoRa APRS Tracker.
 * Licensed under the GNU General Public License, version 3 or later.
 */
// Pruebas en PC del protocolo CAT (include/cat_proto.h).
//   g++ -std=c++17 -Wall -Werror -Iinclude tools/cat_net/test_cat_proto.cpp -o /tmp/test_cat && /tmp/test_cat
//   /tmp/test_cat --serve 4540     (servidor TCP de prueba: se conecta con "rigctl -m 2 -r 127.0.0.1:4540 f")
#include "cat_proto.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <cstdio>

using namespace CatProto;

struct MockRig : Rig {
    uint32_t f = 434775000;
    bool fsk = false;
    int pw = 14;
    int beacons = 0;
    bool ptt = false;
    uint32_t freqHz() override { return f; }
    bool setFreqHz(uint32_t hz) override { if (hz < 430000000UL || hz > 440000000UL) return false; f = hz; return true; }
    bool isFsk() override { return fsk; }
    bool setFsk(bool v) override { fsk = v; return true; }
    int passbandHz() override { return fsk ? 20000 : 125000; }
    int rssiDbm() override { return -100; }
    int powerDbm() override { return pw; }
    bool setPowerDbm(int d) override { if (d < 2 || d > 22) return false; pw = d; return true; }
    int powerMinDbm() override { return 2; }
    int powerMaxDbm() override { return 22; }
    bool pttEnabled() override { return ptt; }
    bool triggerBeacon() override { beacons++; return true; }
    size_t ranges(Range* o, size_t) override { o[0] = {430000000, 440000000}; return 1; }
    const char* info() override { return "APRS LoRa+ test"; }
};

static int failures = 0, checks = 0;
static void check(bool c, const std::string& what) { checks++; if (!c) { failures++; printf("FAIL: %s\n", what.c_str()); } }

static std::string ask(Session& s, MockRig& r, const std::string& cmd, bool* closed = nullptr) {
    std::string out;
    const bool c = s.feed(cmd.data(), cmd.size(), r, out);
    if (closed) *closed = c;
    return out;
}

static void tests() {
    MockRig r; Session s;
    check(ask(s, r, "\\chk_vfo\n") == "0\n", "chk_vfo");
    check(ask(s, r, "f\n") == "434775000\n", "get_freq");
    check(ask(s, r, "F 434900000\n") == "RPRT 0\n" && r.f == 434900000, "set_freq");
    check(ask(s, r, "F 434775000.000000\n") == "RPRT 0\n", "set_freq with decimals");
    check(ask(s, r, "F 145000000\n") == "RPRT -9\n" && r.f == 434775000, "set_freq refused outside the allowed ranges");
    check(ask(s, r, "F abc\n") == "RPRT -1\n", "set_freq invalid");
    check(ask(s, r, "m\n") == "PKTFMN\n125000\n", "get_mode LoRa");
    check(ask(s, r, "M PKTFM 20000\n") == "RPRT 0\n" && r.fsk, "set_mode FSK");
    check(ask(s, r, "m\n") == "PKTFM\n20000\n", "get_mode FSK");
    check(ask(s, r, "M PKTFMN 125000\n") == "RPRT 0\n" && !r.fsk, "set_mode LoRa");
    check(ask(s, r, "M FM-D 20000\n") == "RPRT 0\n" && r.fsk, "set_mode FM-D is the name Hamlib 4.6 sends for PKTFM");
    check(ask(s, r, "M USB 2400\n") == "RPRT -1\n", "set_mode unsupported");
    check(ask(s, r, "l STRENGTH\n") == "-7\n", "STRENGTH = rssi + 93");
    check(ask(s, r, "l RFPOWER\n") == "0.600000\n", "get RFPOWER");
    check(ask(s, r, "L RFPOWER 1.0\n") == "RPRT 0\n" && r.pw == 22, "set RFPOWER");
    check(ask(s, r, "L RFPOWER 0\n") == "RPRT 0\n" && r.pw == 2, "set RFPOWER min");
    check(ask(s, r, "l AF\n") == "RPRT -11\n", "level not available");
    check(ask(s, r, "t\n") == "0\n", "get_ptt");
    check(ask(s, r, "T 1\n") == "RPRT -11\n" && r.beacons == 0, "PTT off by default: no beacon");
    check(ask(s, r, "T 0\n") == "RPRT 0\n", "PTT off is always accepted");
    r.ptt = true;
    check(ask(s, r, "T 1\n") == "RPRT 0\n" && r.beacons == 1, "PTT on triggers a beacon when enabled");
    check(ask(s, r, "v\n") == "VFOA\n", "get_vfo");
    check(ask(s, r, "f VFOA\n") == "434775000\n", "VFO word is skipped");
    check(ask(s, r, "\\get_vfo_info VFOA\n") == "434775000\nPKTFM\n20000\n0\n0\n", "get_vfo_info");
    check(ask(s, r, "_\n") == "APRS LoRa+ test\n", "get_info");
    check(ask(s, r, "\\get_powerstat\n") == "1\n", "powerstat");
    check(ask(s, r, "\\get_lock_mode\n") == "0\nRPRT 0\n", "get_lock_mode (Hamlib asks it before set_mode)");
    check(ask(s, r, "\\set_lock_mode 0\n") == "RPRT 0\n", "set_lock_mode");
    check(ask(s, r, "s\n") == "0\nNone\n", "get_split_vfo");
    check(ask(s, r, "+f\n") == "RPRT -4\n", "extended protocol is not supported");
    check(ask(s, r, "\\nonsense\n") == "RPRT -4\n", "unknown command");
    // fragmented input and several commands in one packet
    std::string o;
    s.feed("f", 1, r, o); s.feed("\nf\r", 3, r, o); s.feed("\n", 1, r, o);
    check(o == "434775000\n434775000\n", "fragments and several commands");
    bool closed = false;
    check(ask(s, r, "q\n", &closed) == "RPRT 0\n" && closed, "quit answers and closes");
    // dump_state: 7 fields in the ranges, ends with done
    const std::string d = ask(s, r, "\\dump_state\n");
    check(d.compare(0, 6, "1\n0\n0\n") == 0, "dump_state header");
    check(d.size() > 10 && d.substr(d.size() - 5) == "done\n", "dump_state ends with done");
    check(d.find("430000000.000000 440000000.000000 0x400001000 -1 -1 0x1 0x1\n") != std::string::npos, "dump_state rx range");
    check(d.find("ptt_type=0x1\n") != std::string::npos, "dump_state ptt_type when enabled");
    r.ptt = false;
    check(ask(s, r, "\\dump_state\n").find("ptt_type=0x0\n") != std::string::npos, "dump_state ptt_type when disabled");
}

static int serve(int port) {
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1; setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons((uint16_t)port); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(ls, (sockaddr*)&a, sizeof(a)) != 0 || listen(ls, 2) != 0) { perror("bind"); return 1; }
    printf("listening on %d\n", port); fflush(stdout);
    MockRig rig; rig.ptt = true;
    for (;;) {
        int c = accept(ls, nullptr, nullptr);
        if (c < 0) continue;
        Session s; char buf[256]; ssize_t n;
        while ((n = read(c, buf, sizeof(buf))) > 0) {
            if (getenv("CAT_TRACE")) { fwrite("<< ", 1, 3, stdout); fwrite(buf, 1, (size_t)n, stdout); fflush(stdout); }
            std::string out;
            const bool close_ = s.feed(buf, (size_t)n, rig, out);
            if (getenv("CAT_TRACE")) { fwrite(">> ", 1, 3, stdout); fwrite(out.data(), 1, out.size() > 60 ? 60 : out.size(), stdout); fputc('\n', stdout); fflush(stdout); }
            if (!out.empty()) write(c, out.data(), out.size());
            if (close_) break;
        }
        close(c);
        printf("freq=%u fsk=%d pw=%d beacons=%d\n", rig.f, rig.fsk, rig.pw, rig.beacons); fflush(stdout);
    }
}

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--serve") return serve(atoi(argv[2]));
    tests();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
