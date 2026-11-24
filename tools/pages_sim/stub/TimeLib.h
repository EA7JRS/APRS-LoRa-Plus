#pragma once
#include <ctime>
typedef long long time_t_;
extern time_t fakeNow;
inline time_t now() { return fakeNow; }
inline int year(time_t t)   { struct tm m; gmtime_r(&t, &m); return m.tm_year + 1900; }
inline int month(time_t t)  { struct tm m; gmtime_r(&t, &m); return m.tm_mon + 1; }
inline int day(time_t t)    { struct tm m; gmtime_r(&t, &m); return m.tm_mday; }
inline int hour(time_t t)   { struct tm m; gmtime_r(&t, &m); return m.tm_hour; }
inline int minute(time_t t) { struct tm m; gmtime_r(&t, &m); return m.tm_min; }
inline int second(time_t t) { struct tm m; gmtime_r(&t, &m); return m.tm_sec; }
