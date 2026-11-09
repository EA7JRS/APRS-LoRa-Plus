# Radio comercial por I2C (RDA5807M, Si4703, Si4735)

Copyright (C) 2026 Manuel - EA7JRS. GPL-3.0-or-later. **Sin probar con los módulos.**

Un receptor de radio comercial conectado al I2C de la pantalla (SDA / SCL de la placa, 3,3 V y masa). El audio sale por la clavija o el amplificador del propio módulo: el tracker solo lo controla (y la app a través de él). Funciona a la vez que cualquier modo (APRS, FM de voz, MORSE...), en todas las placas con pantalla.

## Módulos

| Chip | Dirección I2C | Reinicio | Bandas | Notas |
|---|---|---|---|---|
| RDA5807M | 0x11 (registros) | no hace falta | FM 87,5–108 (o 76–108) | RDS. El más barato |
| Si4703 | 0x10 | **sí**: pin RST a un GPIO | FM | Para entrar en modo I2C, SDIO (SDA) tiene que estar bajo al soltar el RST: el tracker lo hace solo |
| Si4735 | 0x11 (SEN a masa) o 0x63 (SEN a 3,3 V) | **sí** | FM, onda media (520–1710 kHz), corta (2,3–26,1 MHz) y larga (153–279 kHz) | RDS en FM; necesita su cristal de 32,768 kHz |

En la WebUI (*Broadcast radio*): activar la búsqueda al arrancar, GPIO del reinicio (-1 = sin conectar), banda, volumen, desénfasis (50 µs Europa / 75 µs América), FM desde 76 MHz, onda media en pasos de 10 kHz y las 10 presintonías (MHz para FM, `101.5`; kHz para AM, `999`). La línea de estado dice qué receptor se encontró.

## Uso

- **Página BCAST:** frecuencia, `ST` (estéreo), presintonía (`P3`), nombre de la emisora y RadioText por RDS (el texto largo se desplaza), RSSI / SNR, volumen o `MUTE`, y la hora RDS. Pulsación larga: buscar arriba / abajo, volumen + / −, silencio, banda, mono, guardar presintonía, siguiente presintonía, apagar. En la T-TWR el encoder cambia de canal (FM de 100 en 100 kHz; onda media 9 / 10 kHz, corta 5 kHz).
- **App** (pestaña Tracker): lo mismo, con frecuencia directa y la lista de presintonías.
- **BLE:** `bcast`, `bcastset` y el evento `bcast` (ver `docs/BLE_CONTROL.md`).

## Cómo funciona

- Detección: 0x63 → Si4735; 0x11 con el registro 0x00 = 0x58xx → RDA5807M, si no Si4735; si no hay nada, reinicio con SDIO bajo y 0x10 con identificador 0x1242 → Si4703. El Si4735 se comprueba con `GET_REV`.
- RDS: grupos 0A / 0B (nombre: cada trozo se acepta al oírlo dos veces igual), 2A / 2B (RadioText, se borra con el indicador A/B) y 4A (hora: fecha juliana modificada, UTC y desfase). Los bloques que el chip no pudo corregir se descartan.
- La frecuencia y la banda se guardan unos segundos después del último cambio.
- Registros y órdenes: hojas de datos del RDA5807M y del Si4702/03 y guía de programación AN332 del Si47xx.
