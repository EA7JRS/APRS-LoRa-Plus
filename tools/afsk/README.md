# Modem AFSK 1200 (T-TWR)

Copyright (C) 2026 Manuel - EA7JRS - GPL-3.0-or-later

Pruebas en el PC de `include/afsk1200.h` / `src/afsk1200.cpp`:

```
g++ -std=c++17 -O2 -Wall -Wextra -Werror -Iinclude tools/afsk/test_afsk.cpp src/afsk1200.cpp src/ax25_g3ruh.cpp src/fx25.cpp src/il2p.cpp -o /tmp/test_afsk
/tmp/test_afsk                       # ida y vuelta, niveles, inclinacion +-6 dB/oct, reloj +-2 %, ruido, sin falsos
/tmp/test_afsk decode fichero.wav    # cuenta las tramas de un WAV mono de 16 bits
/tmp/test_afsk encode salida.wav 48000   # 100 tramas con nuestro modulador
```

Comparacion con Dire Wolf 1.7 (`gen_packets` y `atest -P A`, perfil A, un solo umbral), 2026-11-09:

| Prueba | Dire Wolf | APRS LoRa+ |
|---|---|---|
| `gen_packets -n 100 -r 13200` (ruido creciente) | 35 | 37-38 |
| `gen_packets -n 100 -r 26400` | 55 | 56 |
| `gen_packets -n 100 -r 44100` | 72 | 70-72 |
| 100 tramas de nuestro modulador a 13200 / 44100 / 48000 Hz | 100 / 100 / 100 | 100 / 100 / 100 |

`gen_packets` reparte el ruido hasta la frecuencia de Nyquist: a mas frecuencia de muestreo, menos ruido en la banda de audio, por
eso las cifras solo se comparan a la misma frecuencia. En la placa se muestrea a 13200 Hz (11 muestras por bit).
