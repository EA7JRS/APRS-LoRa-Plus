// Prueba en PC de include/kiss_stream.h (Manuel - EA7JRS). Compilar: g++ -std=c++17 -Wall -Werror -Iinclude tools/kiss_net/test_kiss_stream.cpp -o t && ./t
#include "kiss_stream.h"
#include <cstdio>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FALLO linea %d: %s\n", __LINE__, #c); fails++; } } while (0)
using namespace KissNetUtil;
static bool feedStr(FrameSplitter& s, const std::string& d) { return s.feed(reinterpret_cast<const unsigned char*>(d.data()), d.size()); }
int main() {
    std::string f;
    {   // una trama completa
        FrameSplitter s;
        CHECK(feedStr(s, std::string("\xC0\x00" "ABC" "\xC0", 6)));
        CHECK(s.next(f) && f == std::string("\xC0\x00" "ABC" "\xC0", 6));
        CHECK(!s.next(f));
    }
    {   // llega en trozos
        FrameSplitter s;
        feedStr(s, std::string("\xC0\x00" "A", 3));
        CHECK(!s.next(f));
        feedStr(s, std::string("BC\xC0", 3));
        CHECK(s.next(f) && f.size() == 6);
    }
    {   // basura antes, tramas vacias y dos tramas seguidas (con FEND compartido)
        FrameSplitter s;
        feedStr(s, std::string("xx\xC0\xC0\xC0\x00" "AB" "\xC0\x00" "CD" "\xC0", 13));
        CHECK(s.next(f) && f == std::string("\xC0\x00" "AB" "\xC0", 5));
        CHECK(s.next(f) && f == std::string("\xC0\x00" "CD" "\xC0", 5));
        CHECK(!s.next(f));
    }
    {   // trama demasiado corta (FEND cmd FEND) se descarta
        FrameSplitter s;
        feedStr(s, std::string("\xC0\x00\xC0", 3));
        CHECK(!s.next(f));
        feedStr(s, std::string("\xC0\x00" "Z" "\xC0", 5));
        CHECK(s.next(f) && f.size() == 4);
    }
    {   // desbordamiento: se vacia y sigue funcionando
        FrameSplitter s;
        std::string big(MAX_BUFFER + 1, 'a');
        CHECK(!feedStr(s, big));
        CHECK(s.pending() == 0);
        feedStr(s, std::string("\xC0\x00" "OK" "\xC0", 5));
        CHECK(s.next(f));
    }
    {   // sin ningun FEND: no se acumula basura
        FrameSplitter s;
        feedStr(s, "hello");
        CHECK(!s.next(f));
        CHECK(s.pending() == 0);
    }
    // lista de direcciones permitidas
    CHECK(ipAllowed("192.168.1.20", ""));
    CHECK(ipAllowed("192.168.1.20", nullptr));
    CHECK(ipAllowed("192.168.1.20", "192.168.1.20"));
    CHECK(!ipAllowed("192.168.1.200", "192.168.1.20"));                  // no vale como prefijo sin punto final
    CHECK(ipAllowed("192.168.1.200", "10.0.0.5, 192.168.1."));
    CHECK(!ipAllowed("192.168.2.7", "10.0.0.5, 192.168.1."));
    CHECK(ipAllowed("10.0.0.5", " 10.0.0.5 ;192.168.1.9"));
    CHECK(ipAllowed("10.0.0.6", " , ; "));                                   // lista solo con separadores = vacia
    CHECK(!ipAllowed(nullptr, ""));
    printf("fails=%d\n", fails);
    return fails ? 1 : 0;
}
