#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <string>
#include <algorithm>

#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105
#include <type_traits>
template<class A, class B> typename std::common_type<A,B>::type min(A a, B b) { return a < b ? a : b; }
template<class A, class B> typename std::common_type<A,B>::type max(A a, B b) { return a > b ? a : b; }
template<class T, class L, class H> T constrain(T v, L lo, H hi) { return v < lo ? (T)lo : v > hi ? (T)hi : v; }
extern uint32_t fakeMillis;
inline uint32_t millis() { return fakeMillis; }
typedef bool boolean;
typedef uint8_t byte;
class __FlashStringHelper;
class String {
public:
    std::string s;
    String(const char* c = "") : s(c) {}
    String(const std::string& x) : s(x) {}
    String(float v, int dec) { char b[32]; snprintf(b, sizeof(b), "%.*f", dec, v); s = b; }
    String(int v) : s(std::to_string(v)) {}
    String operator+(const String& o) const { return String(s + o.s); }
    const char* c_str() const { return s.c_str(); }
    size_t length() const { return s.size(); }
    char operator[](int i) const { return s[i]; }
    bool isEmpty() const { return s.empty(); }
    String& operator=(const char* c) { s = c; return *this; }
    bool operator==(const char* c) const { return s == c; }
    int indexOf(char c) const { auto p = s.find(c); return p == std::string::npos ? -1 : (int)p; }
    String substring(int a, int b = -1) const { return String(b < 0 ? s.substr(a) : s.substr(a, b - a)); }
    int toInt() const { return atoi(s.c_str()); }
    bool startsWith(const char* c) const { return s.rfind(c, 0) == 0; }
};
inline double radians(double d) { return d * DEG_TO_RAD; }
