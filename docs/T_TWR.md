# LilyGO T-TWR Plus: APRS analógico en VHF (AFSK 1200)

Copyright (C) 2026 Manuel - EA7JRS. GPL-3.0-or-later.

Entorno de PlatformIO: `lilygo_t_twr_plus` (Rev2.0 y Rev2.1, una sola imagen). **Sin probar en la placa.**

La T-TWR no lleva radio LoRa. Lleva un módulo **SA868** de NiceRF: un transceptor FM de 1 a 1,8 W (VHF 134–174 MHz o UHF 400–480 MHz) con el audio conectado al ESP32-S3. En esta placa el tracker hace **APRS analógico de 1200 baudios** (AX.25, Bell 202), el de las radios de VHF y los digipeaters e iGates de 144,800 MHz (Región 1) o 144,390 MHz (Norteamérica). El módem AFSK lo hace el ESP32-S3.

## Qué funciona igual que en las demás placas

Balizas (SmartBeacon, perfiles), mensajes y ACK, digipeater, estaciones oídas, pantallas, GPS (L76K), Bluetooth (KISS y control de la app), WebUI, NMEA, telemetría. El modo de arranque se llama **APRS AFSK 1200** (es el modo 0, el que en las demás placas es APRS LoRa).

## Qué no hay en esta placa

APRS LoRa, APRS FSK 9600, CW / Morse, WSPR, FT8, FT4, FST4W, LR-FHSS, DAPNET y la recepción o emisión hacia satélites: son funciones del chip SX126x. El modo SAT queda solo para la predicción de pasos y el rotor. La corrección global de frecuencia (ppm) no se aplica: el SA868 lleva su propio cristal.

## Ajustes (WebUI > *VHF radio (SA868)*, grupo `sa868` de la configuración)

| Ajuste | Por defecto | Notas |
|---|---|---|
| RX / TX (MHz) | 144,8000 | Dentro de la banda del módulo |
| Canal | 25 kHz | APRS 1200: ±3 kHz de desviación |
| Potencia | Baja | Solo la Rev2.0 la puede cambiar (pin H/L). La alta calienta |
| Volumen | 6 (1–8) | Altavoz y audio hacia el ESP32 |
| Silenciador | 1 (0–8) | 0 = abierto: el demodulador oye también las señales más débiles |
| Nivel TX | 25 % | **Ajustar** para unos 3 kHz de desviación (receptor o SDR) |
| TXDELAY / cola | 300 / 30 ms | Banderas antes y después de la trama |
| Filtros de voz | Sí | Pre/desénfasis y paso alto / bajo del módulo |
| Altavoz | Sí | Rev2.1 y firmware OpenRTX; la Rev2.0 no lo puede silenciar |
| Placa | Auto | Rev2.0 / Rev2.1 si la detección falla |

*Escuchar antes de transmitir* (WebUI, el mismo ajuste que el CAD de LoRa) espera a que el silenciador se cierre (Rev2.1) y a que no haya datos AFSK en el aire.

## Cómo funciona

- **Revisión de la placa:** la Rev2.1 alimenta el SA868 desde la batería y la Rev2.0 desde el DC3 de la PMU (3,4 V). Si el módulo contesta sin encender el DC3, es una Rev2.1; si no, se enciende el DC3 y se prueba otra vez. **La Rev2.1 necesita batería** para que funcione la radio.
- **Firmware del módulo:** el de fábrica de NiceRF (órdenes `AT+DMOCONNECT`, `AT+DMOSETGROUP`, `AT+DMOSETVOLUME`, `AT+SETFILTER`) o el abierto de OpenRTX (`sa8x8-fw`, que no tiene esas órdenes: se programan los registros del AT1846S con `AT+POKE` / `AT+PEEK`; secuencia de arranque, canal, frecuencia y modo tomados de OpenRTX, GPL-3.0). Se detecta solo. Los módulos «Open Edition» vienen borrados: hay que grabarles uno de los dos.
- **Banda:** la pantalla de las T-TWR de UHF está en la dirección I2C 0x3D y la de VHF en 0x3C (librería de LilyGO); con OpenRTX manda lo que diga `AT+MODEL`.
- **Emisión:** trama AX.25 → banderas + HDLC (relleno de bits, FCS) → NRZI → tonos de 1200 / 2200 Hz con fase continua a 48 kHz → PDM (I2S0 en modo DAC) por GPIO18 → filtro RC de la placa (1,5 kΩ / 10 nF) → conmutador analógico (`MIC_CH_SEL`, GPIO17, en alto) → entrada de micrófono del SA868. PTT por GPIO41. El micrófono de la placa (BLDO1) queda apagado para que no se mezcle.
- **Recepción:** salida de audio del SA868 → acoplo de 100 nF y polarización (10 kΩ / 10 kΩ) → GPIO1 (ADC1, canal 0) en modo continuo a 13 200 Hz → demodulador en una tarea propia del núcleo 0: paso banda de 8 símbolos, correladores de marca y espacio con filtro de coseno alzado en raíz, control de ganancia por tono (compensa el pre/desénfasis), PLL del reloj de bit con interpolación, NRZI y HDLC. Las ideas son las del perfil A de Dire Wolf (WB2OSZ), escritas de nuevo.
- **Comprobado en el PC** (`tools/afsk/`): ida y vuelta a 6 frecuencias de muestreo, niveles de 0,01 a 0,9, inclinación de ±6 dB por octava, ±2 % de error de reloj, ruido y sin tramas falsas con 30 s de ruido; frente a Dire Wolf decodifica las mismas tramas que su perfil A en las grabaciones de `gen_packets`, y Dire Wolf decodifica 100 de 100 tramas de nuestro modulador.

## Pines (de la librería de LilyGO, `utilities.h`, y de los esquemas)

| Función | GPIO | | Función | GPIO |
|---|---|---|---|---|
| SA868 UART (ESP TX / RX) | 39 / 48 | | OLED SH1106 (I2C, con la PMU) | 8 / 9 |
| PTT (bajo = TX) | 41 | | GPS L76K (RX del ESP / TX del ESP / PPS) | 5 / 6 / 7 |
| PD (bajo = apagado) | 40 | | Pulsador del encoder (botón del tracker) | 21 |
| H/L de potencia (Rev2.0) | 38 | | PTT lateral | 3 |
| Silenciador (Rev2.1) | 2 | | Encoder A / B | 47 / 46 |
| MIC_CH_SEL | 17 | | LED WS2812 | 42 |
| Audio hacia el módulo (PDM) | 18 | | TF (MOSI / SCK / MISO / CS) | 11 / 12 / 13 / 10 |
| Audio desde el módulo (ADC) | 1 | | IRQ de la PMU | 4 |

PMU AXP2101: DC1 ESP32 y OLED, DC3 radio (Rev2.0), ALDO2 TF, ALDO3 conmutador del amplificador (Rev2.1: apagado = el altavoz lo gobierna la radio), ALDO4 GPS, BLDO1 micrófono.

## Sin verificar (mirar primero si algo no va)

1. **Nivel de audio de emisión:** la placa no atenúa la salida del ESP32 antes de la entrada de micrófono (el ejemplo de LilyGO le mete una onda cuadrada de 3,3 V). El 25 % es una estimación: medir la desviación.
2. **Nivel de audio de recepción:** en la Rev2.1 la polarización del ADC queda baja (unos 0,5 V con 47 kΩ hacia EXT_VDD); con mucho volumen el semiciclo negativo se recorta. Si se pierden tramas fuertes, bajar el volumen.
3. **Detección de la revisión** sin batería en la Rev2.1 (el módulo no tendría alimentación): fijar *Placa* en la WebUI.
4. **Firmware de OpenRTX:** la potencia del módulo no se controla por órdenes (solo el pin H/L de la Rev2.0); `AT+AMP` no se usa.
5. **Encoder:** solo el pulsador hace de botón del tracker; el giro no se usa todavía.
