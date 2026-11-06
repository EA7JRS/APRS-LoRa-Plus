# Control por Bluetooth LE (protocolo v1)

Copyright (C) 2026 Manuel - EA7JRS. GPL-3.0-or-later.

Segundo servicio BLE del tracker, junto al de KISS. Lo usa la app **APRS LoRa+ para Android** para ver el estado, los sensores, el GNSS, los mensajes DAPNET y el NMEA, y para cambiar el modo de radio, el perfil de baliza y el perfil LoRa.
Las apps KISS de terceros (aprs.fi, APRSdroid...) no lo ven: siguen usando solo el servicio KISS.

El servicio existe siempre que el Bluetooth esté activo en modo BLE (con KISS o con TNC2).

## Seguridad: emparejamiento con PIN

El servicio de control **exige un enlace cifrado y autenticado**: sin emparejar, el tracker rechaza las escrituras en *Órdenes* y *Firmware* (error ATT «insufficient authentication») y no emite eventos ni NMEA. Con ello nadie en alcance puede cambiar el modo, reiniciar ni instalar un firmware.

- Emparejamiento LE Secure Connections con clave fija: el tracker es «mostrar solo» y su clave es el **PIN de 6 cifras** de la WebUI (*Bluetooth > Control PIN*, por defecto `123456`: **cámbialo**). El móvil pide el PIN al emparejar. Con MITM + bonding: se pide una sola vez por móvil.
- La app debe emparejar (`BluetoothDevice.createBond()`) antes de usar el servicio, o provocarlo leyendo la característica *Eventos* (lectura cifrada). Hasta que el enlace esté autenticado no llega ningún evento aunque se active la suscripción.
- Si se cambia el PIN, el tracker olvida los móviles emparejados (hay que olvidar el dispositivo en el móvil y volver a emparejar).
- El servicio KISS no se toca: aprs.fi, APRSdroid y demás siguen sin PIN.
- Es una clave fija de 6 cifras: protege de curiosos y de órdenes accidentales, no de un atacante que pueda escuchar el emparejamiento.

## UUID

| | UUID | Propiedades | Sentido |
|---|---|---|---|
| Servicio | `4c6f5261-2b00-4ea7-a000-000000000001` | | |
| Órdenes | `4c6f5261-2b00-4ea7-a000-000000000002` | WRITE, WRITE_NR (cifrado + PIN) | móvil → tracker |
| Eventos | `4c6f5261-2b00-4ea7-a000-000000000003` | NOTIFY, READ (cifrada, solo para provocar el emparejamiento) | tracker → móvil |
| NMEA | `4c6f5261-2b00-4ea7-a000-000000000004` | NOTIFY | tracker → móvil |
| Firmware | `4c6f5261-2b00-4ea7-a000-000000000005` | WRITE, WRITE_NR (cifrado + PIN) | móvil → tracker (imagen en binario) |

(`4c6f5261` = "LoRa", `2b` = "+".)

## Formato

- Órdenes y eventos: **un objeto JSON por línea**, terminado en `\n`, en UTF-8. Una línea puede llegar troceada en varias escrituras o notificaciones (tamaño del MTU); se recompone por el `\n`. Longitud máxima de una orden: 255 bytes.
- NMEA: texto NMEA 0183 tal cual (`$GPRMC...\r\n`, `!AIVDM...\r\n`), el mismo que el tracker da por USB. Solo en placas con salida NMEA (`NMEA_OUTPUT`, hoy la T-Beam Supreme) y solo mientras el móvil está suscrito a la característica (CCCD).

Toda orden lleva `"c"` (nombre) y puede llevar `"id"` (número que se devuelve en la respuesta).
Respuesta: `{"e":"ok","c":"...","id":n}` o `{"e":"err","c":"...","id":n,"msg":"..."}`. Las órdenes de consulta responden con su evento (sin `ok`).

## Órdenes

| Orden | Campos | Respuesta / efecto |
|---|---|---|
| `info` | | evento `info` |
| `status` | | evento `status` |
| `sensors` | | evento `sensors` |
| `gnss` | | evento `gnss` |
| `dapnet` | | evento `dapnet` (historial) |
| `sub` | `sensors` (s), `gnss` (s) | envío periódico de `sensors` (mínimo 2 s) y `gnss` (mínimo 1 s); 0 = parar. Se anula al desconectar |
| `mode` | `m` | guarda el modo y **reinicia** el tracker (ver Modos) |
| `profile` | `i` | perfil de baliza (indicativo) número `i` |
| `lora` | `i` | perfil LoRa número `i` (no con el perfil náutico AIS) |
| `beacon` | | envía una baliza ahora (modos APRS) |
| `digi` | `on` | digipeater WIDE1-1 de relleno sí / no |
| `cw` | `i` o `t` | en modo MORSE: envía el mensaje CW `i` (0-4) o el texto `t` (máx. 60 caracteres). Bloquea el tracker mientras emite |
| `k` | `t` (ms del móvil), `d` (1 pulsado, 0 suelto) | manipulador remoto en modo MORSE (ver abajo); sin respuesta |
| `kcfg` | `delay` (ms, 0-1000) | retardo de reproducción del manipulador remoto (150 ms por defecto; vuelve a 150 al desconectar) |
| `ota` | `size` (bytes), `sha` (SHA-256 en hexadecimal) | empieza una actualización por BLE; responde `ok` con `block` y `buffer` (ver abajo) |
| `ota_abort` | | cancela la actualización en curso |
| `fwcheck` / `fwinstall` | | busca (e instala) la última versión con la WiFi del tracker, como los botones de la WebUI |
| `fwstatus` | | evento `fw` con el estado de `fwcheck` / `fwinstall` |
| `reboot` | | reinicia |

## Eventos

`info`:
```json
{"e":"info","v":1,"fw":"5.3.2","board":"ttgo_t_beam_s3_SUPREME_v3",
 "caps":{"nmea":true,"dapnet":true,"cw":true,"modes":true,"rkey":true,"ota":true,"wifiUpd":false},
 "mode":0,"modes":[{"m":0,"n":"APRS LoRa"},{"m":1,"n":"APRS FSK 9600"},{"m":5,"n":"DAPNET pager"}],
 "profile":0,"profiles":[{"call":"EA7JRS-7","label":"Coche"}],
 "lora":0,"loras":[{"f":433775000,"sf":12,"cr":5,"fsk":false}],
 "cwMsgs":["CQ CQ DE {CALL}","..."]}
```

`status`:
```json
{"e":"status","mode":0,"profile":0,"lora":0,"digi":false,"smart":true,"sos":false,"batt":4.05,"up":3600,"bleKiss":true}
```
(`batt` falta si no hay batería.)

`sensors` (cada campo solo si hay dato válido):
```json
{"e":"sensors","batt":4.05,"temp":21.4,"hum":55.0,"press":1013.2,"qnh":1015.0,"baroAlt":512.3,"heading":87.0,"moving":false,"imuTemp":30.1}
```

`gnss`:
```json
{"e":"gnss","fix":true,"lat":37.191512,"lon":-3.936701,"alt":512.0,"spd":0.0,"crs":0.0,"sats":11,"hdop":0.8,"age":420,"utc":"2026-11-05T07:45:00Z"}
```

`dapnet` (más reciente primero) y `page` (uno nuevo, en el momento, solo en modo DAPNET):
```json
{"e":"dapnet","items":[{"ric":12345,"f":3,"own":true,"bad":false,"age":42,"rssi":-97,"t":"Texto del aviso"}]}
{"e":"page","ric":12345,"f":3,"own":true,"bad":false,"age":0,"rssi":-97,"t":"Texto del aviso"}
```

Solo en modo SAT, con *Send the rot event* activo (WebUI), cada vez que cambia el objetivo del rotor (y cada 10 s):
```json
{"e":"rot","az":123.4,"el":45.0,"trk":true,"sat":"ISS (ZARYA)"}
```
`az` / `el` en grados (ya con los desfases de la WebUI), `trk` = el satélite está sobre el horizonte, `sat` = nombre del satélite elegido. La app los retransmite a su rotor (p. ej. un `rotctld`).

## Modos

`m`: 0 APRS LoRa, 1 APRS FSK, 2 MORSE, 3 WSPR, 4 LR-FHSS, 5 DAPNET, 6 FT8, 7 FT4, 8 FST4W, 9 SAT (satélites 70 cm). `info.modes` dice cuáles admite la placa.

- APRS LoRa y APRS FSK se guardan en la configuración (modo de arranque por defecto) y el tracker reinicia en ese modo.
- Los demás se piden para **el siguiente arranque** (memoria RTC, sin escribir la flash): el tracker reinicia directamente en ese modo sin mostrar el menú de selección. El arranque siguiente vuelve al modo APRS guardado, como al elegirlos con el botón.
- Tras el reinicio el móvil vuelve a conectar solo.

## Manipulador remoto (orden `k`)

El manipulador va conectado al móvil (adaptador USB de teclado, MIDI o serie). El móvil hace de manipulador automático si hace falta y manda cada cambio de la línea de manipulación, en cuanto ocurre, como `{"c":"k","t":123456,"d":1}` (escritura sin respuesta). `t` es un reloj del móvil en milisegundos; solo importan las diferencias.

- La línea debe empezar exactamente por `{"c":"k",`: el tracker la atiende en la tarea de NimBLE, sin pasar por el bucle principal, y no responde.
- El tracker abre una sesión de manipulación (como con el manipulador en su jack): con el primer evento fija el desfase entre relojes y reproduce cada evento en `t + desfase`, con un retardo de `kcfg.delay` ms (150 por defecto) que absorbe las variaciones del enlace. Un evento que llega más tarde se ejecuta al momento.
- Usa el perfil de Telegrafía y respeta *key TX* y *sidetone* de la WebUI. Sin *key TX* solo suena el tono (y el tracker avisa con un `err` de `k`).
- Seguridades: portadora como máximo 10 s seguidos; se suelta si cae el enlace; la sesión acaba tras 2,5 s sin manipular; el botón la corta.
- Fuera del modo MORSE los eventos se descartan con un único `{"e":"err","c":"k",...}`.

## Actualizar el firmware desde la app

`caps.ota` dice si la placa tiene segunda ranura de firmware (T-Beam Supreme, T-Beam 1W); las demás solo se actualizan con el instalador web por USB.

1. El móvil descarga `ota/<board>.json` y `firmware.bin` del sitio de versiones (`https://ea7jrs.github.io/APRS-LoRa-Plus/`) y comprueba tamaño y SHA-256.
2. `{"c":"ota","size":1834016,"sha":"<64 hex>"}` → `{"e":"ok","c":"ota","block":4096,"buffer":16384}`.
3. El móvil escribe la imagen en la característica *Firmware* (escrituras sin respuesta, trozos del MTU). El tracker confirma cada bloque con `{"e":"ota","off":<bytes escritos>}`; el móvil no deja más de 2 bloques (8 KB) sin confirmar.
4. Con el último byte el tracker comprueba el SHA-256 y que la imagen es de esta placa (como al subirla por la WebUI), la activa, envía `{"e":"ota","off":<size>,"done":true}` y reinicia a los 1,5 s.
5. Cualquier fallo: `{"e":"err","c":"ota","msg":...}` y la ranura en uso no se toca. También se cancela al desconectar o tras 20 s sin datos.

`fwstatus` → `{"e":"fw","state":"idle|working|uptodate|available|done|error","progress":0-100,"message":"...","latest":"5.3.3","current":"5.3.2","size":1834016}`.

## Tramas KISS del móvil

Desde esta versión el tracker guarda hasta 4 tramas recibidas del móvil por BLE (antes una sola: una segunda trama llegada antes de transmitir la primera la pisaba).
