// Prueba en PC (sin placa) del codificador FST4W: imprime la palabra de codigo (240 bits) y los 160 tonos.
// Compilar (desde la raiz): gcc -w -c -Ilib/ft8lib lib/ft8lib/ft8/*.c (en un directorio vacio) y
//   g++ -std=gnu++17 -Iinclude -Ilib/ft8lib tools/ft/test_fst4w.cpp src/fst4w.cpp *.o -o t_fst4w
// Uso: ./t_fst4w INDICATIVO LOCATOR4 DBM ; la comprobacion contra la matriz de paridad de WSJT-X es tools/ft/verify_fst4w.py
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "fst4w.h"
int main(int argc, char** argv) {
    const char* call = argc > 1 ? argv[1] : "K1ABC";
    const char* grid = argc > 2 ? argv[2] : "FN42";
    int dbm = argc > 3 ? atoi(argv[3]) : 37;
    uint8_t cw[240], t[160];
    if (!FST4W::encodeBits(call, grid, dbm, cw)) { puts("ENCFAIL"); return 1; }
    FST4W::encode(call, grid, dbm, t);
    for (int i = 0; i < 240; i++) putchar('0' + cw[i]);
    putchar('\n');
    for (int i = 0; i < 160; i++) putchar('0' + t[i]);
    putchar('\n');
    return 0;
}
