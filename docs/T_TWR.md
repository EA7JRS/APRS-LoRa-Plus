# LilyGO T-TWR Plus: APRS analógico en VHF (AFSK 1200), 9600 y M17

Copyright (C) 2026 Manuel - EA7JRS. GPL-3.0-or-later.

Entorno de PlatformIO: `lilygo_t_twr_plus` (Rev2.0 y Rev2.1, una sola imagen). **Sin probar en la placa.**

La T-TWR no lleva radio LoRa. Lleva un módulo **SA868** de NiceRF: un transceptor FM de 1 a 1,8 W (VHF 134–174 MHz o UHF 400–480 MHz) con el audio conectado al ESP32-S3. En esta placa el tracker hace **APRS analógico de 1200 baudios** (AX.25, Bell 202), el de las radios de VHF y los digipeaters e iGates de 144,800 MHz (Región 1) o 144,390 MHz (Norteamérica). El módem AFSK lo hace el ESP32-S3.

## Qué funciona igual que en las demás placas

Balizas (SmartBeacon, perfiles), mensajes y ACK, digipeater, estaciones oídas, pantallas, GPS (L76K), Bluetooth (KISS y control de la app), WebUI, NMEA, telemetría. El modo de arranque se llama **APRS AFSK 1200** (es el modo 0, el que en las demás placas es APRS LoRa). Hay otros dos modos: **APRS FSK 9600** (G3RUH, el mismo AX.25 a 9600 baudios que las otras placas emiten con el SX126x) y **M17** (digital, 4FSK), ver más abajo.

## Qué no hay en esta placa

APRS LoRa, CW / Morse, WSPR, FT8, FT4, FST4W, LR-FHSS, DAPNET y la recepción o emisión hacia satélites: son funciones del chip SX126x. El modo SAT queda solo para la predicción de pasos y el rotor. La corrección global de frecuencia (ppm) no se aplica: el SA868 lleva su propio cristal.

## Ajustes (WebUI > *VHF radio (SA868)*, grupo `sa868` de la configuración)

| Ajuste | Por defecto | Notas |
|---|---|---|
| Región IARU y canal APRS | Región 1, 144,8000 | Lista de canales de 2 m de la región (tabla de abajo) o *Custom* |
| RX / TX (MHz) | 144,8000 | Dentro de la banda del módulo |
| Ancho de banda | Narrow 12,5 kHz | *Wide* 25 kHz o *Narrow* 12,5 kHz. Para APRS, Narrow |
| Potencia | Baja | Solo la Rev2.0 la puede cambiar (pin H/L). La alta calienta |
| Volumen | 6 (1–8) | Altavoz y audio hacia el ESP32 |
| Silenciador | 1 (0–8) | 0 = abierto: el demodulador oye también las señales más débiles |
| Nivel TX 9600 | 40 % | Banda base G3RUH: la desviación del perfil FSK (3 kHz por defecto) |
| Protocolo 1200 | AX.25 | AX.25, FX.25 (16 / 32 / 64) o IL2P al emitir en AFSK 1200; en 9600, el del perfil FSK. La recepción entiende los tres siempre |
| Nivel TX | 25 % | **Ajustar** con un receptor o SDR: unos 2,5 kHz de desviación en Narrow (3 kHz en Wide) |
| TXDELAY / cola | 300 / 30 ms | Banderas antes y después de la trama |
| Filtros de voz | Sí | Pre/desénfasis y paso alto / bajo del módulo |
| Altavoz | Sí | Rev2.1 y firmware OpenRTX; la Rev2.0 no lo puede silenciar |
| Placa | Auto | Rev2.0 / Rev2.1 si la detección falla |
| Frecuencia M17 | vacía | Vacía = la sugerida por la IARU R1: 144,875 MHz (VHF) o 433,475 MHz (UHF) |
| CAN M17 | 0 (0–15) | *Channel Access Number* de las tramas M17 |
| Nivel TX M17 | 40 % | Banda base 4FSK: unos ±2,4 kHz de desviación en los símbolos ±3 (medir) |

### Canales APRS de 2 m por región IARU

| Región | Canales (MHz) | Por defecto |
|---|---|---|
| 1 (Europa, África, Oriente Medio) | 144,800 | 144,800 |
| 2 (Américas) | 144,390 (Norteamérica, Chile, Colombia) · 144,930 (Argentina, Uruguay) · 145,570 (Brasil) | 144,390 |
| 3 (Asia-Pacífico) | 144,390 (Indonesia, Malasia, Tailandia) · 145,175 (Australia) · 144,575 (Nueva Zelanda) · 144,640 (China) · 144,660 (Japón, 1200 baudios) | 144,390 |

**Módulo de UHF** (la misma variante y el mismo firmware; la banda se detecta sola):

| Región | Canales APRS de 70 cm (MHz) | Por defecto |
|---|---|---|
| 1 | 432,500 (plan de 70 cm de la IARU R1, «APRS frequency») | 432,500 |
| 2 | sin canal común: se escribe el local | se conserva el que haya |
| 3 | 439,100 (Australia, plan del WIA) · 431,040 (Japón, 1200 baudios) | 439,100 |

Al arrancar, si la frecuencia guardada no es de la banda del módulo (la configuración de fábrica es la de VHF), el tracker pone el canal por defecto de la región para esa banda y lo guarda, así que la WebUI muestra lo que usa la radio. La versión «350» de la T-TWR (320–400 MHz) no está contemplada.

Fuentes: [sigidwiki](https://sigidwiki.com/wiki/APRS), VK2KFJ (Australia y Nueva Zelanda), la tabla japonesa de frecuencias APRS y el plan de VHF/UHF de la IARU R1. Banda de 2 m que se comprueba: 144–146 MHz en la Región 1 y 144–148 MHz en las Regiones 2 y 3 (las normas nacionales mandan). Con la **región IARU automática** activada (WebUI *IARU region (auto)*), al cambiar de región el canal pasa al equivalente: si existe en la nueva región se conserva (144,390 entre la 2 y la 3); si no, el de por defecto. Una frecuencia propia se respeta si cabe en la banda de la nueva región. Mismas tablas en el firmware (`src/iaru_plan.cpp`, tipos `VHF` y `UHF`) y en la WebUI; `tools/check_iaru_tables.py` comprueba que coinciden.

*Escuchar antes de transmitir* (WebUI, el mismo ajuste que el CAD de LoRa) espera a que el silenciador se cierre (Rev2.1) y a que no haya datos AFSK en el aire.

## Cómo funciona

- **Revisión de la placa:** la Rev2.1 alimenta el SA868 desde la batería y la Rev2.0 desde el DC3 de la PMU (3,4 V). Si el módulo contesta sin encender el DC3, es una Rev2.1; si no, se enciende el DC3 y se prueba otra vez. **La Rev2.1 necesita batería** para que funcione la radio.
- **Firmware del módulo:** el de fábrica de NiceRF (órdenes `AT+DMOCONNECT`, `AT+DMOSETGROUP`, `AT+DMOSETVOLUME`, `AT+SETFILTER`) o el abierto de OpenRTX (`sa8x8-fw`, que no tiene esas órdenes: se programan los registros del AT1846S con `AT+POKE` / `AT+PEEK`; secuencia de arranque, canal, frecuencia y modo tomados de OpenRTX, GPL-3.0). Se detecta solo. Los módulos «Open Edition» vienen borrados: hay que grabarles uno de los dos.
- **Banda:** la pantalla de las T-TWR de UHF está en la dirección I2C 0x3D y la de VHF en 0x3C (librería de LilyGO); con OpenRTX manda lo que diga `AT+MODEL`.
- **Emisión:** trama AX.25 → banderas + HDLC (relleno de bits, FCS) → NRZI → tonos de 1200 / 2200 Hz con fase continua a 48 kHz → PDM (I2S0 en modo DAC) por GPIO18 → filtro RC de la placa (1,5 kΩ / 10 nF) → conmutador analógico (`MIC_CH_SEL`, GPIO17, en alto) → entrada de micrófono del SA868. PTT por GPIO41. El micrófono de la placa (BLDO1) queda apagado para que no se mezcle.
- **Recepción:** salida de audio del SA868 → acoplo de 100 nF y polarización (10 kΩ / 10 kΩ) → GPIO1 (ADC1, canal 0) en modo continuo a 13 200 Hz → demodulador en una tarea propia del núcleo 0: paso banda de 8 símbolos, correladores de marca y espacio con filtro de coseno alzado en raíz, control de ganancia por tono (compensa el pre/desénfasis), PLL del reloj de bit con interpolación, NRZI y HDLC. Las ideas son las del perfil A de Dire Wolf (WB2OSZ), escritas de nuevo.
- **Comprobado en el PC** (`tools/afsk/`): ida y vuelta a 6 frecuencias de muestreo, niveles de 0,01 a 0,9, inclinación de ±6 dB por octava, ±2 % de error de reloj, ruido y sin tramas falsas con 30 s de ruido; frente a Dire Wolf decodifica las mismas tramas que su perfil A en las grabaciones de `gen_packets`, y Dire Wolf decodifica 100 de 100 tramas de nuestro modulador.

## FX.25 e IL2P

Igual que en las demás placas, al emitir se puede elegir AX.25, FX.25 (16, 32 o 64 bytes de control) o IL2P, y al recibir se entienden los tres a la vez. En **AFSK 1200** se elige en la sección del SA868 (*1200 baud protocol*); en **9600**, en el perfil FSK como en el resto. En 1200, FX.25 va con NRZI y sin aleatorizador e IL2P sin NRZI, como Dire Wolf. Comprobado frente a Dire Wolf en los dos sentidos (`gen_packets -X 16/32/64` e `-I 1`, a 1200 y a 9600): decodificamos lo mismo que `atest` o algo más, y `atest` decodifica 100 de 100 de las nuestras en todos los casos. Con ruido fuerte (3 dB) FX.25 (32) rescata 39/40 tramas y IL2P 35/40, frente a 30/40 de AX.25.

## APRS FSK 9600 (G3RUH)

SELECT MODE → **APRS FSK 9600**. Emite y recibe AX.25 a 9600 baudios G3RUH (aleatorizador y NRZI), compatible con los TNC y radios con puerto de datos de 9600 y con las demás placas APRS LoRa+ en su modo APRS FSK. Las emisoras de voz normales no lo oyen. Usa las mismas frecuencias de la sección *VHF radio (SA868)*: **elige una frecuencia de 9600** (144,800 y los canales de la lista son de 1200 baudios).

- **Camino de audio plano:** el módulo se pone sin filtros de voz ni pre/desénfasis y en canal de 25 kHz (aunque el ancho elegido sea Narrow). Con el firmware de NiceRF, `AT+SETFILTER=1,1,1`; con el de OpenRTX, los registros del modo DMR que usa OpenRTX para M17 en esta misma placa.
- **Compatible con las demás placas:** el mismo AX.25 a 9600 G3RUH (los mismos codificadores) y, con módulo de UHF, la **frecuencia FSK y el protocolo del perfil LoRa activo** (`fskFrequency` y `fskFec`: AX.25, FX.25 16/32/64 o IL2P), igual que en las placas con SX126x. La desviación de las demás placas es la del perfil (3 kHz por defecto): ajusta *TX level 9600* a esa desviación. Con módulo de VHF se usan las frecuencias de la sección del SA868.
- **Emisión:** bytes de `AX25G3::g3ruhEncode` → pulsos de coseno alzado (caída 0,5) a 48 kHz → PDM. **Recepción:** ADC a 48 kHz (5 muestras por bit) → paso bajo → control de ganancia → PLL de reloj con interpolación → `AX25G3::Decoder` (también FX.25 e IL2P al recibir).
- **Comprobado en el PC** (`tools/afsk/test_g3ruh.cpp`, en la CI): ida y vuelta a 38,4 / 44,1 / 48 / 96 kHz, polaridad invertida, niveles, acoplo de 20 Hz y paso bajo de 7 kHz, ±1 % de reloj y ruido. Frente a Dire Wolf (`gen_packets -B 9600 -n 100`, `atest -B 9600`): 66 / 66 tramas a 48 kHz y 60 / 62 a 44,1 kHz; Dire Wolf decodifica 100 de 100 de las nuestras.
- **Sin verificar:** que el SA868 deje pasar 9600 limpio (respuesta en baja frecuencia de la entrada de micrófono y de la salida de audio, sobre todo con el firmware de NiceRF). Si no se decodifica, probar con el firmware de OpenRTX y ajustar *TX level 9600*.

## M17 (etapa 1: paquetes y escucha)

SELECT MODE → **M17** (modo 10; BLE `mode` 10). Radio digital M17 (4FSK, 4800 símbolos/s, filtro RRC α 0,5) por el mismo audio plano que el 9600 (filtros de voz puenteados, canal de 25 kHz), en la **frecuencia M17** de la sección del SA868. El modo M17 es un modo APRS más: balizas, mensajes y ACK, digipeater, KISS y control de la app funcionan igual, pero por M17.

- **Emisión:** todo lo que emite el tracker va en **paquetes M17 de protocolo APRS** (0x02) con el texto TNC2 entero, dirigidos a `@ALL` y con el indicativo de origen de la trama en la cabecera (LSF). Otra T-TWR en modo M17 los lee como APRS normal. Delante, portadora sin modular hasta completar el TXDELAY.
- **Recepción:** se entienden los paquetes M17 de protocolo APRS (pasan tal cual) y **SMS** (0x05: pasan a un mensaje APRS `ORIGEN>APZM17,NOGATE::DESTINO :texto`, 67 caracteres como mucho; si va a nuestro indicativo aparece como mensaje recibido) y se oyen las **emisiones de voz y datos** de otras radios M17: el indicativo y, si la cabecera lleva posición GNSS (META), la posición, el rumbo, la velocidad y la altura pasan a la lista de estaciones como `ORIGEN>APZM17,NOGATE:!posición… M17 voice to DESTINO` (sin posición, un estado `>M17 voice to DESTINO`), como mucho cada 30 s por estación. La cabecera se toma del LSF del principio o, si se engancha a mitad, de los trozos del LICH. Lo que no es APRS de verdad lleva `NOGATE`: ningún iGate lo sube a APRS-IS. Los indicativos M17 con `/` o `.` pasan a `-`.
- **SMS a otras radios M17** (OpenRTX, M17 Project...): orden BLE `m17sms` (`to` = indicativo, vacío = `@ALL`; `t` = texto). Un solo envío, sin confirmación (el SMS M17 no la tiene); los mensajes APRS normales siguen con sus reintentos.
- **Sin voz** en esta etapa: el audio Codec2 de las emisiones no se reproduce (etapa 2, solo Rev2.1). Lo cifrado se ignora.
- **Código:** tramas, código convolucional, Viterbi, Golay, intercalado y aleatorizado de **libm17** (M17 Foundation, GPL-2.0-or-later, `lib/libm17`, sin cambios); filtro RRC a 48 kHz, búsqueda de las palabras de sincronía con normalización de nivel, armado de paquetes y paso a APRS en `src/m17_modem.cpp`.
- **Comprobado en el PC** (`tools/afsk/test_m17.cpp`, en la CI): SMS de 1 a 300 caracteres, niveles de 0,05 a 0,95, continua, ±0,1 % de reloj, ruido (30/30 a 10 dB), sin tramas falsas con 10 s de ruido, emisión de voz con GNSS (por el LSF, por el LICH y enganchando a mitad) y las conversiones a APRS. Frente a las herramientas de referencia (`m17-packet-encode` / `m17-packet-decode` de M17_Implementations): decodificamos 6 de 6 de las suyas (hasta 800 caracteres) y ellas 4 de 4 de las nuestras.
- **Frecuencia:** M17 no tiene canal fijo; la sugerencia de la IARU R1 (boletín VHF+ de junio de 2023) es 144,875 y 433,475 MHz. En otras regiones, escribe la que se use en tu zona.
- **Sin verificar:** como en 9600, que el SA868 deje pasar el 4FSK limpio (mejor con el firmware de OpenRTX, que es el que OpenRTX usa para su propio M17 en esta placa) y el nivel de emisión.

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
