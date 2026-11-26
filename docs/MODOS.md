# Modos de funcionamiento

Tras el mensaje de bienvenida aparece **SELECT MODE**: *APRS LoRa*, *APRS FSK 9600*, *MORSE / CW*, *WSPR beacon*, *FT8 / FT4 / FST4W beacon*, *SSDV*, *RTTY beacon*, *LR-FHSS beacon* (si está activada) *DAPNET pager* y *Satellites 70cm* (ver `SAT_MODE.md`); con más de 4 opciones la lista se desplaza (flechas ▲▼). Pulsación corta = siguiente, pulsación larga = aceptar; a los 8 s sin pulsar se arranca el último modo APRS usado (LoRa o FSK; MORSE y WSPR nunca se guardan ni arrancan solos). Una opción solo aparece si la radio la soporta (FSK no en LLCC68; MORSE, WSPR, FT, SSDV, RTTY y LR-FHSS solo con SX1262/SX1268) **y si la placa los lleva** (ver «Modos mínimos» abajo). SSDV, RTTY, FT8/FT4/FST4W y los demás modos de baliza se activan y configuran en la **WebUI**.

> **Modos mínimos (placas de hasta 4 MB).** En placas de **4 MB o menos** (todas las ESP32 clásicas, las ESP32-C3 y la S3 de 4 MB) solo quedan **APRS LoRa, APRS FSK y CW**: SSDV, RTTY, POCSAG/DAPNET, WSPR, FT8/FT4/FST4W, LR-FHSS y SAT se compilan fuera (liberan flash y evitan los buffers de RAM que en un ESP32 sin PSRAM comprometen la memoria). Las placas de **más de 4 MB** (8/16 MB: Supreme, T-TWR, T-Deck, Heltec V3…) conservan todos los modos. No es configurable: depende del tamaño de flash de la placa.

| Modo | Modulación | Páginas | Balizas |
|---|---|---|---|
| APRS LoRa | LoRa (como siempre) | 11 | automáticas |
| APRS FSK 9600 | FSK G3RUH con tramas AX.25 (el perfil AIS sigue en LoRa) | 11 | automáticas |
| WSPR beacon | portadora con 4 tonos (4-FSK) a 1,46 baudios | 1: solo la pantalla **WSPR** (sin barra de iconos) | solo manuales; baliza WSPR automática (ver `WSPR.md`) |
| MORSE / CW | LoRa en reposo; CW por portadora | 1: solo la pantalla **MORSE** (sin barra de iconos) | solo manuales |
| DAPNET pager | FSK 1200 baud (POCSAG), solo recepción | 1: solo la pantalla **DAPNET** | ninguna: no emite |

- En modo MORSE la única pantalla es **MORSE** (decodificación); macros, escucha CW por RF, manipulador, trainer y *Change mode* están en su submenú (pulsación larga). Los modos APRS no la muestran.
- En modo WSPR la única pantalla es **WSPR** (cuenta atrás, progreso de la emisión, hora y locator); ver `WSPR.md`.
- En la WebUI desaparece el selector de modulación por perfil: SF, CR y BW valen para LoRa y los valores FSK (velocidad, desviación, ancho de banda de Rx) para el modo APRS FSK.
- Para cambiar de modo sin reiniciar a mano: MAIN → pulsación larga → **Change mode** (reinicia y vuelve a preguntar).
- Si el botón no existe en la placa, se arranca el último modo guardado.

## DAPNET (receptor POCSAG)

Solo recepción de la red DAPNET (hampager.de): 439,9875 MHz, 1200 baud. No emite nada. Detalle, ajustes y limitaciones en el capítulo 26 del manual (`MANUAL_USUARIO.md`).

## Qué decodifica cada tracker (RX)

El **SX1262/SX1268** (RadioLib 8.0.x) solo tiene dos demoduladores por hardware: **LoRa** y **(G)FSK**. Todo lo que el tracker recibe va montado sobre uno de esos dos; RadioLib entrega la lectura cruda y el *framing* lo hace el software del firmware. El **SA868** (LilyGO T-TWR) es un transceptor FM de VHF y aporta su propio bloque de modos FM/digitales.

| Modo | Función | Decodificación (RX) | Chips |
|---|---|---|---|
| APRS LoRa | TX + RX | paquete LoRa nativo | SX126x, SX127x, LLCC68 |
| APRS FSK (G3RUH 9600, Bell 1200) | TX + RX | FSK cruda + G3RUH/NRZI/HDLC/CRC AX.25 por software (FX.25, IL2P) | SX126x, SX127x (no LLCC68); SA868 (AFSK 1200) |
| CW / Morse | TX + RX | FSK de banda estrecha + envolvente RSSI + decodificador propio (evento `cwrx`: letra, dBm, SNR, wpm) | SX1262/68; SA868 |
| DAPNET (POCSAG) | **solo RX** | FSK cruda + sincronismo POCSAG por software (439,9875 MHz, 1200 bd) | SX1262/68; SA868 |
| SSDV (imágenes) | TX + RX | paquetes SSDV sobre LoRa (sin RS) o FSK (con RS); reconstruye el JPEG | SX1262/68 |
| M17 | TX + RX | demodulador M17 (`M17M::Rx`) | SA868 (T-TWR) |
| AIS | **solo RX** | demodulador AIS GMSK 9600 (`AisCore::Demod`), VHF marina | SA868 (T-TWR) o módulo externo |
| FM voz | TX + RX | recepción analógica (no es decodificar un modo de datos) | SA868 (T-TWR) |
| WSPR / FT8 / FT4 / FST4W | **solo TX** | el tracker no las decodifica: FFT/DSP en la **app Android** (librerías nativas) o WSJT-X/SDR | SX1262/68 |
| RTTY | **solo TX** | `RTTYClient` de RadioLib es solo emisión | SX1262/68 |
| LR-FHSS | **solo TX** | el SX1262/68 solo lo **emite**; no tiene demodulador de recepción (recepción con **RTL-SDR** en la app) | SX1262/68 |

> La variante **LLCC68** es solo LoRa (sin FSK): de la tabla solo le queda **APRS LoRa**.

## Alcance por modo (resumen)

Cifras orientativas del estudio completo ([`ESTUDIO_COBERTURA.md`](ESTUDIO_COBERTURA.md): tracker a 20 dBm, antena de −2 dBi, margen de 10 dB, 432,5 MHz). Dependen de potencia, antenas y terreno. En los modos **solo TX** el alcance es tracker → estación que los decodifica (iGate, pasarela, receptor de señal débil o RTL-SDR), no entre dos trackers.

| Modo | Sensibilidad efectiva (dBm) | Urbano | Campo abierto | Cima | Notas |
|---|---|---|---|---|---|
| LoRa SF12 (BW 125, CR 4/5) | −136 | ~5 km | ≥20 km | ≥120 km | mejor presupuesto de enlace de los modos de paquete |
| LoRa SF9 | −129 | ~3 km | ~11 km | ≥120 km | 6× más rápido, ~61 % del alcance de SF12 |
| LR-FHSS (CR 1/3) | −136 | ~5 km | ≥20 km | ≥120 km | **solo TX**: lo recibe una pasarela/RTL-SDR, no otro tracker |
| APRS FSK 9600 | −107 | ~0,8 km | ~5 km | ~41 km | sin FEC; mucho más corto que LoRa a igual potencia |
| DAPNET (POCSAG, RX) | −115 | — | — | — | solo recepción de la red DAPNET |
| CW / Morse | −142 | ~6–12 km | ≥20 km | ≥120 km | señal débil; con estación de señal débil (yagi) aún más |
| WSPR | −168 | ≥20 km | ≥20 km | ≥120 km | **solo TX**; señal débil extrema |
| FST4W-120 | −170 | ≥20 km | ≥20 km | ≥120 km | **solo TX** |
| FT8 / FT4 | −158 / −154 | ~15–18 km | ≥20 km | ≥120 km | **solo TX** |
| AIS / M17 / FM (SA868, ~1 W) | — | línea de vista | ~20–40 mn (AIS, mar) | — | VHF FM; depende de altura de antenas |

Las tablas completas (por potencia, por escenario y con margen sobrante) están en [`ESTUDIO_COBERTURA.md`](ESTUDIO_COBERTURA.md).
