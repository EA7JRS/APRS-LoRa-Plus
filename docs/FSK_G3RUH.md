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

## Plan de bandas IARU en la WebUI
Cada perfil tiene *IARU region* (1, 2, 3) y *IARU band plan channel* (canales digitales de 70 cm según el plan IARU R1 / WIA, y frecuencias LoRa APRS habituales). La WebUI avisa en rojo si la frecuencia queda fuera de la asignación de aficionados de la región (R1 430–440 MHz; R2 420–450 y 902–928 MHz; R3 430–450 MHz). Las normas nacionales prevalecen; el aviso es solo informativo y no impide guardar.
