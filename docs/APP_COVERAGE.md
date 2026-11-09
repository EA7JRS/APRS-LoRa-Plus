# Cobertura de la app Android (APRS LoRa+)

**Regla del proyecto (Manuel, EA7JRS):** todas las funciones de este proyecto han de ser controladas por la aplicación de Android *APRS LoRa+*, y la app ha de poder **ver los datos** de todos los sensores de la T-Beam Supreme y del resto de placas, los mensajes APRS, DAPNET, etc.

Para cada función nueva del firmware hay que hacer tres cosas antes de darla por terminada:
1. Una orden BLE (o entrar en `act` / `cfg` si ya cabe) y, si tiene datos, un evento. Documentarla en `docs/BLE_CONTROL.md`.
2. Una pantalla o control en la app (o un hueco en la tabla de abajo con la fecha prevista).
3. Una fila en esta tabla.

Mientras no haya pantalla propia, la app puede usar los mecanismos genéricos: `actlist` / `act` (todas las opciones de menú), `screen` (espejo de la pantalla del equipo) y `cfgget` / `cfgset` (toda la configuración de la WebUI).

Estado: **FW** = el firmware lo expone por BLE; **App** = la app Android tiene el código (pestañas Tracker, Sensors, Heard, SAT, Remote, Config, DAPNET, Firmware). **Nada está probado todavía en el móvil ni con el hardware real.**

| Función | Orden / evento BLE | FW | App |
|---|---|---|---|
| Estado, modo, perfil de baliza, perfil LoRa, digi, baliza ahora, reinicio | `status`, `mode`, `profile`, `lora`, `digi`, `beacon`, `reboot` | sí | sí |
| Sensores (batería, PMU, carga, T/HR/P, gas, IAQ, IMU, magnetómetro, QNH, altitud, placa) | `sensors`, `sub` | sí | sí |
| GNSS | `gnss`, `sub` | sí | sí |
| A-GPS del receptor u-blox (hora y posición del móvil; Predictive Orbits con cuenta de u-blox) | `gpsaid` | sí | sí |
| Salida NMEA / AIS | canal NMEA | sí | sí |
| Mensajes APRS (ver, enviar, confirmación) | `msgs`, `msg`, evento `msg` | sí | sí |
| Estaciones oídas, meteo, equipos | `heard` | sí | sí |
| Tramas KISS / APRS | servicio KISS | sí | sí |
| DAPNET | `dapnet`, evento `page` | sí | sí |
| Telegrafía: mensajes, texto libre, manipulador remoto | `cw`, `k`, `kcfg` | sí | sí |
| Modo SAT: estado, lista, selección, receptor, emisión, transmisores, datos | `sat`, `satlist`, `satsel`, `satrx`, `sattx`, `sattxlist`, `satdata`, `pos` | sí | sí |
| Receptor AIS: barcos (lista, detalle, GO TO), alarmas de colisión y socorro, ajustes; frases del RTL-SDR de la app | modo 12, `aislist`, evento `ais`, `aisin`, `aisset`, `caps.ais` | sí | etapa 2 |
| Tiempo y calidad del aire de internet (OpenWeatherMap, IQAir) en la página TELEMETRY: la app lo descarga en la posición del tracker (o del móvil) con las claves del usuario | `wxnet`, `caps.wxnet` | sí | sí |
| Satélites: favoritos, categorías de la lista, archivo completo por grupos de CelesTrak (la app y la WebUI filtran) | `satfav`, `satcat`, `satlist` (`b`, `fav`, `in`), `sat` (`fav`, `cat`, `tot`, `nfav`, `stamp`), `caps.satMax` / `satFav` | sí | sí |
| T-TWR: el VFO de FM sigue al satélite (Doppler, subida, CTCSS), APRS por satélite (ISS, ruta `sat.aprsPath`), posición por satélite | `satrx` / `sattx` en FM y SAT, `sattx` `pos`, evento `fm` (`sat`), `caps.satFm` | sí | sí |
| Rotor y objetivos (inclinómetro con los sensores del móvil; repetidores de RepeaterBook como objetivos) | evento `rot`, `att`, `tgtlist`, `tgt` | sí | sí |
| rotctld desde el móvil: la app manda el apunte a un rotctld de la red (cliente) y acepta Look4Sat / GPredict en el puerto 4533 (servidor) | evento `rot` (`aaz`, `ael`), `rotext` | sí | sí |
| Receptores SDR (SDR++ / gqrx por rigctl, SDRconnect por WebSocket, SpyServer de Airspy; RTL-SDR por USB con el driver de Android o rtl_tcp, demodulado en la app): sintonía, modo, filtro, ganancia, grabación; siguen el satélite con Doppler, la LoRa del tracker o el objetivo del rotor | solo en la app (eventos `sat`, `sattxlist`, `info`, `status`, `targets`) | — | sí |
| Voz digital DMR, D-STAR, YSF/C4FM, P25 fase 1 y 2, NXDN, dPMR, M17 (dsd-fme + mbelib dentro de la app, con su receptor RTL-SDR) | solo en la app | — | sí |
| Identificación de señales (base SigID de Artemis / sigidwiki, actualizable; búsqueda por frecuencia del SDR) | solo en la app | — | sí |
| Recepción en la app de las balizas del tracker: APRS FSK G3RUH, CW, FT8, FT4, WSPR y FST4W (120 / 300 / 900 / 1800 s; receptor RTL-SDR o micrófono); APRS LoRa la reciben otros trackers conectados como receptores; LR-FHSS no (el SX1262 solo emite) | solo en la app | — | sí |
| Varios trackers como receptores a la vez (Bluetooth KISS o KISS TCP por WiFi) | KISS | sí | sí |
| Spots de lo recibido: FT8, FT4 y CW a PSK Reporter; WSPR y FST4W a WSPRnet; pasarela UDP de WSJT-X (GridTracker, JTAlert, RBN Aggregator); puerto telnet tipo CW Skimmer para el RBN (vía RBN Aggregator); CW de la app y de la escucha CW del tracker | `cwrx` (`f`) | — | sí |
| Libro de guardia en la app (QSO a mano y de CW, sin APRS; importar ADIF de qFT8) y confirmación: QRZ.com (API), LoTW (firmado en la app con el certificado .p12 de TQSL), eQSL, World Radio League (UDP de WSJT-X a su programa y vía QRZ); ADIF para qFT8 u otros; descarga de confirmaciones de LoTW, QRZ y eQSL | — (frecuencia CW de `cwrx`) | — | sí |
| Modos de texto al estilo fldigi (CW, RTTY, BPSK; más en curso) | solo en la app | — | parcial |
| Actualización de firmware (la app descarga y envía por BLE; el tracker no descarga) | `ota`, `ota_abort` | sí | sí |
| **Cualquier opción de menú del equipo** (mensajes, brújula, SOS, reloj, altímetro, GPS, LoRa, CW, WSPR, DAPNET, rotor, equipos, SYSTEM...) | `actlist`, `act` | sí | sí |
| Espejo de pantalla de cualquier página | `screen` | sí | sí |
| **Toda la configuración** (WebUI) | `cfgget`, `cfgset` | sí | sí |
| Escucha CW por RF (texto, nivel, SNR, velocidad, frecuencia) | `cwrx`, evento `cwrx` (`f`) | sí | sí |
| T-TWR (radio FM SA868): revisión, firmware, banda, módem y frecuencias en uso; modo M17 y SMS a otras radios M17 | `info.sa868`, `mode` 10, `m17sms` | sí | sí |
| T-TWR modo FM de voz: canal, sintonía, tonos, potencia, silenciador (manual / automático), reducción de ruido, memorias, escaneo, ARTS, EAI, balizas APRS durante la voz; QSY desde estaciones oídas | `mode` 11, `fm`, `fmset`, `fmdial`, `fmmem`, `fmscan`, `fmarts`, `fmeai`, `qsy`, eventos `fm` / `fmmems` | sí | sí |
| T-TWR: portadora de calibración de frecuencia | `carrier` | sí | sí |
| Radio comercial por I2C (RDA5807M, Si4703, Si4735): sintonía, búsqueda, volumen, banda, mono, presintonías, RDS (nombre, texto, hora), RSSI / SNR / estéreo | `bcast`, `bcastset`, evento `bcast` | sí | sí |
| T-TWR: DAPNET (VHF / UHF) y CW A1A / MCW (modo MORSE) | los de DAPNET y MORSE de siempre | sí | sí |
| Sesiones interactivas con el pulsador (entrenador, sintonías WSPR / CW-baliza) | pendiente de orden propia | no | no |

La orden `att` la manda la app con el móvil sujeto al soporte del rotor (boca arriba, parte de arriba del móvil hacia delante; ajuste de cero en la app). Comprobar los signos con el montaje real.

Pendiente en el firmware: sesiones interactivas (escucha CW y entrenador, sintonía WSPR / CW-baliza) con orden BLE propia y cancelación; espejo de los gráficos (brújula, mapa) como datos.
