# Modo FSK 9600 AX.25 (G3RUH) y plan de bandas IARU

Cada perfil LoRa puede trabajar en **LoRa** (por defecto) o en **FSK 9600 AX.25 (G3RUH)**. Se elige al arrancar, en el selector de modo (ver `MODOS.md`).

## Qué se transmite
- Las balizas APRS se convierten a tramas **AX.25 UI** (PID 0xF0) con FCS CRC‑16, HDLC con bit‑stuffing, NRZI y scrambler **G3RUH** (x^17 + x^12 + 1), a 9,6 kbps, GFSK BT 0,5, desviación 3 kHz.
- No se envía la cabecera LoRa APRS `<\xff\x01`; al recibir se repone internamente.
- El perfil AIS siempre es LoRa.

## Limitaciones
- Compatible con receptores de packet 9600 baudios G3RUH (TNC/radios con puerto de datos 9600). **No** lo oyen equipos APRS analógicos de 1200 baudios (AFSK).
- El chip solo mueve bloques de 255 bytes: preámbulo máximo ~50 ms (60 banderas) y tramas de hasta ~150 bytes.
- La recepción trabaja sin sincronismo de chip (bloques de 255 bytes decodificados por software). Es experimental y no está comprobada en hardware real: hay que verificarla con el equipo.
- No hay CAD/escucha previa en FSK.
- El SX1262 de la T‑Beam S3 Supreme es el objetivo; LLCC68 no tiene FSK.
- Los q‑constructs (`qAR`…) y minúsculas en indicativos no existen en AX.25 y se transmiten en mayúsculas.

## FX.25 e IL2P (versión 5.3.1)
Cada perfil FSK tiene el selector **Packet protocol (TX)**:

| Opción | Qué se emite | Quién lo oye |
|---|---|---|
| AX.25 | trama AX.25 normal (por defecto) | cualquier estación packet 9600 |
| FX.25, 16 / 32 / 64 bytes de control | la misma trama AX.25 dentro de un bloque Reed-Solomon con etiqueta de correlación; corrige hasta 8 / 16 / 32 bytes erróneos | **todas** (las que no conocen FX.25 leen la trama AX.25 de dentro); las que sí lo conocen (Dire Wolf…) además corrigen errores |
| IL2P | cabecera comprimida + Reed-Solomon, sin HDLC | solo estaciones con IL2P (Dire Wolf 1.7 o posterior) |

- La **recepción** entiende AX.25, FX.25 e IL2P a la vez, sin configurar nada. Si una trama FX.25 se decodifica por los dos caminos solo se entrega una vez.
- **FX.25** es la opción recomendada: no pierde compatibilidad y gana alcance. Con el scrambler G3RUH un bit erróneo del canal estropea hasta 3 bytes, así que conviene el modo de 32 bytes de control en enlaces dudosos.
- **IL2P** tiene menos sobrecarga. Se emite **sin el scrambler G3RUH ni NRZI** (lleva su propio aleatorizador), como Dire Wolf y la NinoTNC: así un bit erróneo del canal es un solo byte erróneo y el Reed-Solomon lo corrige siempre. **Hasta la 5.3.5 se aleatorizaba**: solo lo entendían otras placas APRS LoRa+ con ese mismo firmware (no Dire Wolf); las versiones nuevas y las antiguas no se entienden en IL2P (sí en AX.25 y FX.25). Úsalo solo si el otro extremo es IL2P.
- El chip emite 255 bytes como máximo por transmisión: una trama FX.25 admite unos 120 bytes de AX.25 (200 bytes en IL2P). Si no cabe se emite como AX.25 normal.
- La recepción de bloques FX.25 e IL2P es la de siempre, por bloques de 255 bytes; una trama partida entre dos bloques se pierde.
- Verificación en PC (`tools/fx25`, `tools/il2p`): el control Reed-Solomon de los 11 formatos FX.25 coincide con el de Dire Wolf, el decodificador lee los 11 ficheros de prueba de Dire Wolf (con 8 bytes dañados) y el receptor de Dire Wolf lee nuestros bloques; las 12 tramas IL2P de prueba salen idénticas byte a byte. **Probado en radio**: FX.25 e IL2P funcionan en placa.
- Tamaño en flash: unos pocos KB más (se medirá al compilar).

## Plan de bandas IARU en la WebUI
Cada perfil tiene *IARU region* (1, 2, 3) y *IARU band plan channel* (canales digitales de 70 cm según el plan IARU R1 / WIA, y frecuencias LoRa APRS habituales). La WebUI avisa en rojo si la frecuencia queda fuera de la asignación de aficionados de la región (R1 430–440 MHz; R2 420–450 y 902–928 MHz; R3 430–450 MHz). Las normas nacionales prevalecen; el aviso es solo informativo y no impide guardar.
