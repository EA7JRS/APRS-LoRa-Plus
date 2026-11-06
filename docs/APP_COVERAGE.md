# Cobertura de la app Android (APRS LoRa+)

**Regla del proyecto (Manuel, EA7JRS):** todas las funciones de este proyecto han de ser controladas por la aplicación de Android *APRS LoRa+*, y la app ha de poder **ver los datos** de todos los sensores de la T-Beam Supreme y del resto de placas, los mensajes APRS, DAPNET, etc.

Para cada función nueva del firmware hay que hacer tres cosas antes de darla por terminada:
1. Una orden BLE (o entrar en `act` / `cfg` si ya cabe) y, si tiene datos, un evento. Documentarla en `docs/BLE_CONTROL.md`.
2. Una pantalla o control en la app (o un hueco en la tabla de abajo con la fecha prevista).
3. Una fila en esta tabla.

Mientras no haya pantalla propia, la app puede usar los mecanismos genéricos: `actlist` / `act` (todas las opciones de menú), `screen` (espejo de la pantalla del equipo) y `cfgget` / `cfgset` (toda la configuración de la WebUI).

Estado: **FW** = el firmware lo expone por BLE; **App** = la app Android ya lo usa.

| Función | Orden / evento BLE | FW | App |
|---|---|---|---|
| Estado, modo, perfil de baliza, perfil LoRa, digi, baliza ahora, reinicio | `status`, `mode`, `profile`, `lora`, `digi`, `beacon`, `reboot` | sí | no |
| Sensores (batería, PMU, carga, T/HR/P, gas, IAQ, IMU, magnetómetro, QNH, altitud, placa) | `sensors`, `sub` | sí | no |
| GNSS | `gnss`, `sub` | sí | no |
| Salida NMEA / AIS | canal NMEA | sí | no |
| Mensajes APRS (ver, enviar, confirmación) | `msgs`, `msg`, evento `msg` | sí | no |
| Estaciones oídas, meteo, equipos | `heard` | sí | no |
| Tramas KISS / APRS | servicio KISS | sí | sí (esqueleto) |
| DAPNET | `dapnet`, evento `page` | sí | no |
| Telegrafía: mensajes, texto libre, manipulador remoto | `cw`, `k`, `kcfg` | sí | no |
| Modo SAT: estado, lista, selección, receptor, emisión, transmisores, datos | `sat`, `satlist`, `satsel`, `satrx`, `sattx`, `sattxlist`, `satdata`, `pos` | sí | no |
| Rotor y objetivos | evento `rot`, `att`, `tgtlist`, `tgt` | sí | no |
| Actualización de firmware | `ota`, `fwstatus`, `fwcheck`, `fwinstall` | sí | no |
| **Cualquier opción de menú del equipo** (mensajes, brújula, SOS, reloj, altímetro, GPS, LoRa, CW, WSPR, DAPNET, rotor, equipos, SYSTEM...) | `actlist`, `act` | sí | no |
| Espejo de pantalla de cualquier página | `screen` | sí | no |
| **Toda la configuración** (WebUI) | `cfgget`, `cfgset` | sí | no |
| Sesiones interactivas con el pulsador (escucha CW, entrenador, sintonías WSPR / CW-baliza) | pendiente de orden propia | no | no |

Pendiente en el firmware: sesiones interactivas (escucha CW y entrenador, sintonía WSPR / CW-baliza) con orden BLE propia y cancelación; espejo de los gráficos (brújula, mapa) como datos.
