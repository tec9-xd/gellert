#pragma once
#include <cstdio>
#include <cstdarg>

inline void print(const char* fmt, ...) {
    static FILE* f = fopen("/tmp/cs2.log", "w");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fflush(f);
}
