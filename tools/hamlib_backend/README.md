# Backend de Hamlib para APRS LoRa+ / Hamlib backend for APRS LoRa+

Copyright (C) 2026 Manuel - EA7JRS. `aprslorap.c` es LGPL-2.1 o posterior (la licencia de la biblioteca Hamlib); el resto del repositorio es GPL-3.0.

## Qué es

Un *backend* nativo de Hamlib (`RIG_MODEL_APRSLORAP`, familia `dummy`, como `quisk.c`, `sdrsharp.c` o `gqrx.c`) que habla con el servidor CAT del firmware (`docs/CAT_HAMLIB.md`) por TCP. Hace lo mismo que `rigctl -m 2` (NET rigctl), pero con nombre propio, rangos de frecuencia de las bandas LoRa y sin tener que saber que la placa es "un rigctld". Frecuencia, modo (`PKTFM` = APRS FSK, `PKTFMN` = APRS LoRa), RSSI (`STRENGTH`), potencia (`RFPOWER`) y PTT = baliza APRS.

Hay dos modelos: **WiFi** (red, `aprs-lora-plus.local:4532`) y **USB** (puerto serie de la placa, 115200 baudios, DTR y RTS apagados por defecto para no reiniciar la placa). Mientras no esté aceptado en Hamlib, **para WiFi no hace falta**: usa el modelo 2 (`rigctl -m 2 -r aprs-lora-plus.local:4532`). Por USB sí hace falta el backend (Hamlib no trae modelo serie para este protocolo).

## Estado

- Probado con el `rigctl` real de Hamlib **4.7.2** (modelo 12 WiFi y modelo 13 USB con el parche, el USB probado a través de un seudoterminal enlazado al servidor de pruebas; también `-m 2`) y, antes, con 4.6.5, contra el servidor de pruebas de `tools/cat_net` (get/set de frecuencia, modo, STRENGTH, RFPOWER, PTT, info).
- El parche `hamlib-4.7.2-aprslorap.patch` usa los modelos 12 (WiFi) y 13 (USB) (el 11 es GQRX en 4.7.2) y aplica limpio sobre el árbol original.
- El número de modelo definitivo lo asigna Hamlib al aceptarlo.

## Probarlo en local

```
tar xf hamlib-4.7.2.tar.gz && cd hamlib-4.7.2
patch -p1 < /ruta/a/tools/hamlib_backend/hamlib-4.7.2-aprslorap.patch
./bootstrap && ./configure && make && sudo make install     # (bootstrap solo si compilas desde git)
rigctl -l | grep -i aprs
rigctl -m 12 -r aprs-lora-plus.local:4532 f
rigctl -m 13 -r /dev/ttyACM0 f          # por USB (COM3, etc. en Windows)
```

## Enviarlo a Hamlib (lo tiene que hacer el autor)

Pasos según `README.developer` de Hamlib: fork de <https://github.com/Hamlib/Hamlib>, rama nueva desde `master`, copiar `aprslorap.c` a `rigs/dummy/`, añadir las 4 líneas del parche (`riglist.h`, `dummy.h`, `dummy.c`, `Makefile.am`) y abrir un *pull request*. Puntos que piden los mantenedores y que ya están cubiertos:

1. Protocolo **público**: `docs/CAT_HAMLIB.md` (inglés).
2. Licencia LGPL-2.1+ del fichero.
3. Probado con un cliente real (`rigctl`), con servidor de pruebas reproducible (`tools/cat_net/test_cat_proto.cpp`).
4. Sin dependencias nuevas.

Texto sugerido para el PR (inglés): *"Add backend for the APRS LoRa+ tracker (EA7JRS): a LoRa/FSK APRS tracker with a WiFi CAT server speaking the rigctld text protocol. Frequency, mode (PKTFM = APRS FSK 9600, PKTFMN = APRS LoRa), STRENGTH, RFPOWER and PTT (= one APRS beacon). Protocol: docs/CAT_HAMLIB.md in https://github.com/EA7JRS/APRS-LoRa-Plus"*. Se puede proponer a la vez un `RIG_MODE_LORA` propio; hoy se usa `PKTFMN` como sustituto.
