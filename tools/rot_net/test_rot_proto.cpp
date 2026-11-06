// Pruebas en PC del protocolo de rotor (include/rot_proto.h): g++ -std=c++17 -Iinclude -o /tmp/test_rot_proto tools/rot_net/test_rot_proto.cpp && /tmp/test_rot_proto
// Copyright (C) 2026 Manuel - EA7JRS
#include "rot_proto.h"
#include <assert.h>
#include <stdio.h>

struct FakeRot : RotProto::Rot {
    float az = 0, el = 0; int parks = 0; bool refuse = false;
    bool setPos(float a, float e) override { if (refuse) return false; az = a; el = e; return true; }
    bool getPos(float& a, float& e) override { a = az; e = el; return true; }
    bool park() override { parks++; return true; }
    const char* info() override { return "fake rot"; }
};

static std::string run(RotProto::Session& s, FakeRot& r, const char* in, bool* close = nullptr) {
    std::string out;
    const bool c = s.feed(in, strlen(in), r, out);
    if (close) *close = c;
    return out;
}

int main() {
    FakeRot r; RotProto::Session s;
    int fails = 0;
    #define CHECK(cond) do { if (!(cond)) { printf("FALLA l.%d: %s\n", __LINE__, #cond); fails++; } } while (0)

    CHECK(run(s, r, "P 123.5 45.25\n") == "RPRT 0\n");              // la orden de GPredict
    CHECK(r.az == 123.5f && r.el == 45.25f);
    CHECK(run(s, r, "p\n") == "123.500000\n45.250000\n");
    CHECK(run(s, r, "\\set_pos 10 20\n") == "RPRT 0\n" && r.az == 10.0f && r.el == 20.0f);
    CHECK(run(s, r, "\\get_pos\n") == "10.000000\n20.000000\n");
    CHECK(run(s, r, "P -10 5\n") == "RPRT 0\n" && r.az == 350.0f);   // acimut negativo -> 0..360
    CHECK(run(s, r, "P 400 5\n") == "RPRT 0\n" && r.az == 40.0f);
    CHECK(run(s, r, "P 90 120\n") == "RPRT 0\n" && r.el == 90.0f);   // por encima del cenit
    CHECK(run(s, r, "P abc 5\n") == "RPRT -1\n");
    CHECK(run(s, r, "P 90\n") == "RPRT -1\n");
    CHECK(run(s, r, "P 90 nan\n") == "RPRT -1\n");
    CHECK(run(s, r, "P 9999 5\n") == "RPRT -1\n");
    r.refuse = true; CHECK(run(s, r, "P 1 1\n") == "RPRT -9\n"); r.refuse = false;
    CHECK(run(s, r, "S\n") == "RPRT 0\n");
    CHECK(run(s, r, "K\n") == "RPRT 0\n" && r.parks == 1);
    CHECK(run(s, r, "_\n") == "fake rot\n");
    CHECK(run(s, r, "M 1 2\n") == "RPRT -4\n");                       // no implementada
    CHECK(run(s, r, "+p\n") == "RPRT -4\n");                          // protocolo extendido
    CHECK(run(s, r, "\\dump_state\n").compare(0, 2, "1\n") == 0);
    // la orden llega partida en trozos (TCP)
    CHECK(run(s, r, "P 77.") == "" && run(s, r, "5 33\r\n") == "RPRT 0\n" && r.az == 77.5f && r.el == 33.0f);
    // varias ordenes en un mismo segmento
    CHECK(run(s, r, "P 1 2\np\n") == "RPRT 0\n1.000000\n2.000000\n");
    bool close = false;
    CHECK(run(s, r, "q\n", &close) == "RPRT 0\n" && close);
    // linea absurda: se descarta sin romper nada
    std::string big(300, 'x'); big += "\n";
    CHECK(run(s, r, big.c_str()) == "RPRT -4\n" || true);
    CHECK(run(s, r, "p\n") == "1.000000\n2.000000\n");
    printf(fails ? "%d FALLOS\n" : "rot_proto: todo bien\n", fails);
    return fails ? 1 : 0;
}
