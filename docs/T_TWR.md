# LilyGO T-TWR Plus: APRS analógico (AFSK 1200), 9600, M17, FM de voz, DAPNET y CW

Copyright (C) 2026 Manuel - EA7JRS. GPL-3.0-or-later.

Entorno de PlatformIO: `lilygo_t_twr_plus` (Rev2.0 y Rev2.1, una sola imagen). **Sin probar en la placa.**

La T-TWR no lleva radio LoRa. Lleva un módulo **SA868** de NiceRF: un transceptor FM de 1 a 1,8 W (VHF 136–174 MHz o UHF 400–480 MHz para recibir) con el audio conectado al ESP32-S3. En esta placa el tracker hace **APRS analógico de 1200 baudios** (AX.25, Bell 202), el de las radios de VHF y los digipeaters e iGates de 144,800 MHz (Región 1) o 144,390 MHz (Norteamérica). El módem AFSK lo hace el ESP32-S3.

## Qué funciona igual que en las demás placas

Balizas (SmartBeacon, perfiles), mensajes y ACK, digipeater, estaciones oídas, pantallas, GPS (L76K), Bluetooth (KISS y control de la app), WebUI, NMEA, telemetría. El modo de arranque se llama **APRS AFSK 1200** (es el modo 0, el que en las demás placas es APRS LoRa). Otros modos: **APRS FSK 9600** (G3RUH, el mismo AX.25 a 9600 baudios que las otras placas emiten con el SX126x), **M17** (digital, 4FSK), **FM voice** (la T-TWR como emisora de mano), **DAPNET** (buscapersonas POCSAG, recepción) y **MORSE / CW** (A1A o MCW), ver más abajo.

## Qué no hay en esta placa

APRS LoRa, WSPR, FT8, FT4, FST4W, LR-FHSS y la recepción o emisión hacia satélites: son funciones del chip SX126x (WSPR y los modos de WSJT-X necesitan pasos de 1,46 Hz que el SA868 no hace). El modo SAT queda solo para la predicción de pasos y el rotor.

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
| Emisión CW | A1A | A1A (portadora) o MCW (tono, 800 Hz) en el modo MORSE |
| DAPNET | vacía | 144,8625 (VHF) / 439,9875 MHz (UHF); acoplo de la entrada de audio 320 Hz |
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

## FM voice (la T-TWR como emisora de mano)

SELECT MODE → **FM voice** (modo 11; BLE `mode` 11). El modo se guarda como los modos APRS: la T-TWR vuelve a arrancar en FM.

- **Voz:** PTT lateral (GPIO3) con el micrófono de la placa; el altavoz suena por la radio (o por el ESP32 en la Rev2.1, ver abajo). Tiempo máximo de emisión (TOT, 180 s por defecto): pasado, corta hasta soltar el PTT.
- **Página FM** (se abre al arrancar): frecuencia grande, VFO o memoria (`M03`), desplazamiento, tono, ancho (`W`/`N`), potencia (`H`/`L`), paso o nombre de la memoria, silenciador (`SQLA3` = automático, nivel 3), estado (RX con S-meter, TX con segundos, escaneo, ARTS, EAI) y lo que mueve el encoder. Las demás páginas APRS siguen ahí (mensajes, estaciones, GPS…).
- **Encoder:** en la página FM mueve lo elegido en *Dial*: frecuencia del VFO (en pasos de 5 a 100 kHz) o memoria siguiente, tono CTCSS / DCS, desplazamiento (pasos de 100 kHz) o nivel del silenciador. En el VFO, **ARS**: dentro de un segmento de salidas de repetidor de la región IARU pone solo el desplazamiento (R1: 145,600–145,7875 −600 kHz, 438,650–439,425 −7,6 MHz; R2 y R3 sus segmentos habituales).
- **Submenú (pulsación larga):** VFO / memoria, *Dial*, desplazamiento, tono (sin tono, TONE, TSQL, DCS, 1750 Hz), paso, potencia, ancho, silenciador automático o manual, reducción de ruido, escaneo, guardar en memoria, ARTS, EAI, balizas APRS, *Tune carrier*, cambio de modo.
- **Tonos:** 50 CTCSS y 104 DCS (normal o invertido); 1750 Hz = medio segundo de tono al pulsar el PTT. Con el firmware de **NiceRF**, los 38 CTCSS de su tabla y todos los DCS; con el de **OpenRTX**, cualquier CTCSS (registros del AT1846S, como OpenRTX) pero **no DCS** (avisa y emite sin él).
- **Potencia:** Low / High por canal (pin H/L IO38; la Rev2.1 sin verificar). **Silenciador:** manual 0–8 o automático (abajo).
- **32 memorias** con nombre (8 caracteres): se guardan desde el submenú, la WebUI o la app; *Scan memories* las recorre.
- **Escaneo:** memorias o banda (la de aficionado de la región, o los límites de la WebUI); reanuda a los 2 s de quedar libre (*Carrier*), a los 5 s (*Time*) o se para en la primera señal (*Stop*). El encoder cambia el sentido; el PTT lo para.
- **ARTS** (como Yaesu): sondea con el código DCS del grupo cada 25 (o 15) s y al soltar el PTT; **IN RANGE** mientras se oye al otro, **OUT RANGE** tras cuatro sondeos sin oírlo (aviso en pantalla y en la app). Indicativo en CW cada 10 minutos (opcional). Con OpenRTX (sin DCS) usa TSQL de 100 Hz: solo entre T-TWR.
- **EAI** (emergencia, como Yaesu): *Interval* = pitido de 0,5 s cada 2,5 s; *Continuous* = micrófono abierto en tramos de 50 s con 10 s de escucha; indicativo en CW al empezar y cada 10 minutos; dura lo elegido (1–50 min). Arranca desde el submenú, la app o el **hombre caído** (minutos sin tocar nada, con aviso 30 s antes). Con *APRS beacon* también lanza la baliza de emergencia (SOS) por APRS. A diferencia de los Yaesu, no hay disparo remoto por pareja de tonos; el disparo remoto existente por mensaje APRS (`EAI <código>`) sigue disponible en los modos APRS.
- **APRS durante la voz:** con *APRS beacon* las balizas, los mensajes y el KISS salen en la frecuencia APRS de la sección del SA868 (AFSK 1200, sin tono) y la radio vuelve al canal de voz; si se está hablando, esperan a soltar el PTT. No hay digipeater en FM. Lo que llegue en AFSK por el propio canal de voz se decodifica igualmente.
- **QSY** (especificación de frecuencia de APRS 1.2, como Kenwood): con *Frequency in beacon* el comentario de la baliza empieza con el canal (`145.650MHz T088 -060`, minúscula = estrecho, `-000` = simplex en un segmento de repetidores). Al revés: en la página LAST APRS, *QSY (FM voice)* sintoniza lo que anuncia la estación (comentario `146.940MHz T100 -060 R25k` o nombre de objeto `146.94-R`; sin desplazamiento escrito, el ARS de la región); desde otro modo, guarda el canal en el VFO y arranca en FM. La app tiene el botón *QSY* en las estaciones oídas.
- **CAT (Hamlib)** en FM: `F` sintoniza el VFO.

### Silenciador automático y reducción de ruido

- **Rev2.1:** el ESP32 alimenta el amplificador del altavoz (GPIO45 por sigma-delta a 13,2 kHz, ALDO3 elige el ESP32) con el audio del ADC. El silenciador por software abre con dos bloques de 20 ms sin aspecto de soplido y cierra con tres seguidos (el detector mira la energía de alta frecuencia y el nivel frente al soplido aprendido sin portadora; con portadora el ruido baja y no cierra en las pausas); con OpenRTX además el RSSI frente al suelo de ruido medido + 6 dB. La **reducción de ruido** (Low / Mid / High, se aplica al arrancar el modo) es una resta espectral (STFT de 256 puntos, estimación del ruido por mínimos, ganancia suavizada); unos 20 ms de retardo. En el PC: +5 dB de relación señal / ruido con voz sintética a 7 dB y −10 a −14 dB de soplido sin voz.
- **Rev2.0:** el altavoz lo alimenta solo la radio: el automático **ajusta el nivel del silenciador del módulo** (sube uno si se abre con soplido más de 1 s; con el canal libre un minuto prueba a bajar uno y lo deshace si vuelve el soplido, y no lo intenta otra vez en 5 min). Sin reducción de ruido.
- Con OpenRTX (sin silenciador en el módulo): manual = umbral de RSSI (nivel 1 = −124 dBm … 8 = −96 dBm), y el altavoz se calla con `AT+AUDIO`.
- El micrófono no pasa por el ESP32: su entrada (GPIO15) es del ADC2, sin lectura continua.

## DAPNET (recepción POCSAG)

SELECT MODE → **DAPNET pager** (modo 5), en las dos bandas: **144,8625 MHz** con el módulo de VHF y **439,9875 MHz** con el de UHF (o la de *DAPNET* en la WebUI). El SA868 se pone en audio plano y canal de 25 kHz; el ESP32 demodula el POCSAG (512 / 1200 / 2400 baudios, el de la sección DAPNET) desde el audio: compensación del acoplo de alterna de la entrada (100 nF con 10 k / 10 k, unos 320 Hz, ajustable), paso bajo, umbral adaptativo y PLL de reloj; los bits van al mismo decodificador POCSAG que las demás placas (RIC propios, todos los mensajes, hora, juego de caracteres, página DAPNET, avisos y app). Solo recepción, como en las demás. En el PC (`tools/afsk/test_pocsag_audio.cpp`): las tres velocidades, polaridad invertida, ±0,5 % de reloj, acoplo de 250–400 Hz, 20/20 a 12 dB; sin compensar el acoplo, 0/20.

## MORSE / CW en VHF y UHF

SELECT MODE → **MORSE / CW** (modo 2). Todo lo del modo MORSE de la Supreme: mensajes CW, texto libre, manipulador (jack y app), escucha con decodificación, entrenador, baliza CW y spots. Frecuencias y potencia de la sección *CW* (fuera de la banda del módulo: 144,050 / 432,050 MHz; potencia > 31 dBm = High).

- **Emisión A1A** (por defecto): portadora sin modular que se enciende y apaga; con NiceRF por el PTT (hasta unas 15–18 ppm por el tiempo de subida del módulo), con OpenRTX por el registro de modo del AT1846S (una orden por flanco, hasta unas 12 ppm). **MCW**: PTT puesto toda la sesión y tono de 800 Hz (ajustable) por el micrófono del módulo, sin límite de velocidad.
- **Recepción:** un receptor de FM no da el tono de la CW, pero la portadora calla el soplido: el ESP32 mide la energía de alta frecuencia del audio cada 2 ms frente al soplido sin portadora (silenciador del módulo abierto durante la escucha) y esa envolvente va al decodificador Morse de siempre. En el PC: PARIS a 20 ppm con todos los flancos y ~3 ms de error por flanco.
- **Rev2.1:** tono local de 700 Hz por el altavoz al manipular y tono de 700 Hz cuando se oye la portadora.

## Calibración de frecuencia

WebUI > *Frequency calibration*: la misma corrección en ppm que las demás placas, aplicada al SA868 en emisión y recepción (es el mismo cristal), en todos los modos. *Tune carrier* (submenú de RADIO y de FM, o la app, orden `carrier`) emite 30 s de portadora sin modular en la frecuencia de emisión en uso: mídela con un SDR, escribe el error y *Apply to ppm*. Resolución: 100 Hz con NiceRF (formato del `AT+DMOSETGROUP`), 62,5 Hz con OpenRTX.

## GPS L76K

Configurado como en la Supreme: el tracker detecta el L76K (no contesta a UBX: órdenes CASIC `$PCAS`), lo sube a 115200 baudios, elige constelaciones (GPS / BeiDou / GLONASS) desde la página GPS o la WebUI, lo apaga y enciende por el ALDO4, filtra la velocidad parado y guarda la última posición.

## Límites de emisión (fijos en el firmware)

La T-TWR recibe en 136–174 MHz (módulo de VHF) o 400–480 MHz (UHF), pero **solo emite en las bandas de aficionado** de la región IARU donde está, según la posición GPS (o la última guardada): Europa (Región 1) 144–146 y 430–440 MHz; América (Región 2) 144–148 y 430–450 MHz; Región 3 144–148 y 430–440 MHz. Sin posición, solo lo común a las tres (144–146 y 430–440 MHz). No se cambia desde la WebUI ni desde la configuración: la región que se elige en la WebUI sirve para las frecuencias por defecto, no amplía lo que se puede emitir. Fuera de banda el VFO escucha (marina, aviación no, meteorológicos, AIS...) y el PTT dice *No TX: out of band*; toda emisión (voz, APRS, M17, CW, tonos, portadora de calibración) pasa por la misma comprobación. Las normas de cada país pueden ser más estrictas que la región IARU (subbandas, potencia): eso es responsabilidad del operador.

**Desbloqueo con código** (para quien tenga licencia fuera de las bandas de aficionado): con el código de *ese* equipo la radio emite en toda la banda del módulo (136–174 / 400–480 MHz). El código es la firma ECDSA P-256 de «APRS-LoRa-Plus TX unlock» + la MAC del equipo (128 cifras hexadecimales); lo genera Manuel - EA7JRS con su clave privada (`tools/tx_unlock_code.py sign <privada> <MAC>`) y el firmware solo lleva la pública (`include/tx_unlock_key.h`): leer el binario no permite sacar códigos ni sirve el de otro equipo. Se introduce en la WebUI (*VHF radio (SA868)*: *Unlock TX range*, que muestra la MAC) o por BLE (`txunlock`); se guarda en la NVS, fuera de la configuración (ni la WebUI ni `cfgset` lo activan), y *Lock again* vuelve a los límites. Cinco códigos erróneos bloquean el intento 10 minutos. Hasta poner la clave pública en `tx_unlock_key.h` (`tools/tx_unlock_code.py genkey <archivo>` en tu PC; la privada no se sube nunca), el firmware no se puede desbloquear.

## AIS (modo 12)

Con el módulo de VHF, *AIS receiver* escucha los barcos en 161,975 / 162,025 MHz (audio plano, canal de 25 kHz, un canal cada vez o alternando). Página AIS, alarmas de colisión y socorro, GO TO y `!AIVDM` a la app. Detalle en `docs/AIS.md`.

## Satélites (modos SAT y FM)

El modo SAT funciona con el VFO de FM: lista (favoritos, *This radio*, 2 m…), pasos, azimut / elevación, rotor y, con *Listen*, el VFO sigue al satélite con Doppler, subida precompensada y el CTCSS del transmisor; FM de voz con el PTT y **APRS por satélite** (ISS 145,825 MHz, ruta ARISS, posición o mensaje). En el modo FM, *Satellite: ON / SAT: next satellite / SAT: next transmitter / SAT: send position*. El archivo completo de satélites cabe entero (~900 kB, grupos de CelesTrak) y se envía desde la app o desde la WebUI por WiFi. Detalle en `docs/SAT_MODE.md`.

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
4. **Firmware de OpenRTX:** la potencia del módulo no se controla por órdenes (solo el pin H/L); `AT+AMP` no se usa.
5. **Encoder:** el giro sintoniza en la página FM (tabla de paso completo de la librería Rotary, la del ejemplo de LilyGO): si da dos pasos por clic o ninguno, avisar.
6. **Potencia en la Rev2.1:** el pin H/L (IO38) se maneja también; LilyGO solo lo documenta en la Rev2.0.
7. **Altavoz desde el ESP32 (Rev2.1, GPIO45 + ALDO3):** nivel y filtrado del sigma-delta; si se oye mal, poner *Noise red.* en *Off* y el silenciador en manual (la radio vuelve a alimentar el altavoz).
8. **Detector de soplido, silenciamiento de la CW y POCSAG:** probados con señales sintéticas; los umbrales pueden necesitar ajuste con el audio real del SA868.
9. **Velocidad de la CW A1A:** depende del tiempo de subida del módulo; medir con un SDR.
