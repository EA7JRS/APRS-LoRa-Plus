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
| `sky` | | evento `sky` (satélites a la vista, DOP y error) |
| `dapnet` | | evento `dapnet` (historial) |
| `sub` | `sensors` (s), `gnss` (s), `sky` (s) | envío periódico de `sensors` (mínimo 2 s), `gnss` (mínimo 1 s) y `sky` (mínimo 2 s); 0 = parar. Se anula al desconectar |
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
| `tgt` | alta: `n` (nombre, 20), `lat`, `lon`, `alt` (m), `f` (Hz), `i` (hueco 0-7; sin él, el primero libre), `sel` (true = elegirlo y seguirlo); `del` (hueco); `sel` (número de hueco, sin `n`); `src` (0, 1, 2, 4, 5: ver el evento `rot`; otro valor = `bad src`); `ff` (filtro de indicativos de la fuente 4), `fm` (antigüedad máxima, min), `pr` (true = estimar la posición de las estaciones en movimiento), `pm` (tope de la estimación, s) | objetivos fijos del rotor (repetidores, balizas...): se guardan en el tracker; responde `ok` y el evento `targets` |
| `att` | `p` (cabeceo, + morro arriba), `r` (alabeo, + lado derecho abajo), grados | medida del inclinómetro del soporte del rotor (modo SAT / rotor); caduca a los 2 s, repetir. Solo se usa con *Tilt compensation* activada en la WebUI; sin respuesta |
| `rotext` | `az`, `el` (grados) · `park`:true | objetivo del rotor mandado por un programa externo a través de la app (servidor rotctld del móvil: Look4Sat, GPredict...), igual que el servidor rotctld por WiFi del tracker: manda sobre la fuente elegida mientras se repita (caduca a los 15 s). Solo responde si hay error (`rotor disabled`, `bad position`). `caps.rotor` = rotor activado en la WebUI |
| `alarm` | `s`:`off` | calla la alarma de emergencia recibida (zumbador, LED y flash); la pantalla de alarma sigue abierta hasta que se pulse el botón. No transmite |
| `eai` | `s`:`stop` · `s`:`ok` · `s`:`start` | `stop` termina la baliza SOS propia, la EAI remota y el EAI de voz de la T-TWR; `ok` = «estoy bien» (reinicia el hombre caído); `start` arranca el EAI de voz (T-TWR en FM) o la baliza SOS (con su cuenta atrás de 5 s). `stop` y `ok` no transmiten; `start` sí |
| `cwrx` | `s`:`start` · `s`:`stop` | escucha CW por RF (modo MORSE, SX126x): la misma sesión que *Listen CW (RF)* del menú. Mientras dura, el bucle del tracker está ocupado (no hay balizas ni otras órdenes) y cada letra llega como evento `cwrx`; termina con `stop`, el botón (pulsación larga) o a los 10 minutos. No transmite |
| `satrx` | `on` (true/false), `tx` (índice del transmisor del satélite elegido, 0 por defecto), `fine` (Hz, se suma al ajuste fino) | receptor de satélites (modo SAT; en la T-TWR también en el modo FM, `caps.satFm`: el VFO sigue al satélite, FM de voz y APRS AFSK 1200, con la subida y el CTCSS del transmisor): empieza / para la escucha con Doppler; responde `ok` (o un `err` con el motivo) y el evento `satrx` (en la T-TWR `k` es `FM` o `APRS`) |
| `sattx` | `tx` (índice del transmisor), `txt` (mensaje, 64; si falta, el de la WebUI); T-TWR: `pos`:true = posición APRS por un digipetidor (ISS) | emite hacia el satélite (modo SAT; solo con *Allow transmitting* y las seguridades de `SAT_MODE.md`); bloquea mientras emite; responde `ok` o un `err` con el motivo, y el evento `sattx` |
| `sat` | | evento `sat` con el estado del modo SAT (también cada 2 s en ese modo, y en la T-TWR en FM mientras el VFO sigue a un satélite) |
| `satlist` | `from` (0), `cnt` (20, máx. 40) | evento `satlist`: `{"e":"satlist","tot":239,"sel":3,"from":0,"items":[{"i":0,"id":25544,"n":"ISS (ZARYA)","c":"U","t":4,"b":15,"fav":true,"in":true}]}` (`c`: U 70 cm, V 2 m / VHF, L LoRa, G geoestacionario; `t`: transmisores; `b`: bandas de sus transmisores, bits 1 = 144–148 MHz, 2 = 420–450, 4 = 134–174 (SA868 VHF), 8 = 400–480 (SA868 UHF); `fav` favorito; `in` en la categoría elegida) |
| `wxnet` | `src` (`OWM`, `IQAir`, `OWM+IQAir`), `pl` lugar, `t` °C, `h` %, `p` hPa, `ws` / `wg` viento / racha m/s, `wd` grados, `cl` nubes %, `r` lluvia mm/h, `ds` descripción, `aq` AQI de OpenWeatherMap (1–5), `aqus` AQI de EE. UU. (IQAir), `mp` contaminante principal, `pm25` / `pm10` µg/m³, `age` s de la observación (todos opcionales) | tiempo y calidad del aire de internet que descarga la app (OpenWeatherMap, IQAir AirVisual) para la página TELEMETRY: se muestra con «NET» (alternando cada 6 s con el sensor si lo hay), caduca a las 2 h y **no** va en las balizas APRS. Su presión (al nivel del mar) es además el **QNH del altímetro** cuando no hay QNH manual ni uno aprendido del GPS en los últimos 20 min (`qs` = `net` en el evento `sensors`, «QNH 1015N» en la página). El evento `sensors` lo devuelve en `net` para los demás clientes (APRS LoRa+ Display), con `fc` (pronóstico del tracker con esos datos) y `d3` (cambio de la presión en 3 h, de observaciones de 1 h a 3,5 h). La WebUI hace lo mismo con `POST /api/wxnet` (`d` = el JSON de la orden). Sin datos útiles, `err` «no data». `caps.wxnet`. No emite |
| (evento `netreq`) | — | `{"e":"netreq","what":"wx"}` o `"sat"`: el tracker pide a la app que baje con el internet del móvil el tiempo de TELEMETRY (*Update from phone* en esa página) o el archivo de satélites (*Update data (phone)* en SAT); la app lo baja con sus ajustes (claves, grupos) y lo manda con `wxnet` / `satdata`. `caps.netreq` |
| `aislist` | `from` (0), `cnt` (1–15, 10) | barcos AIS por distancia: `{"e":"aislist","tot":7,"from":0,"msgs":120,"src":"SA868 A","ch":"A","items":[{"m":224123450,"cl":"A","n":"NOMBRE","c":"EA1234","t":70,"st":0,"lat":36.5,"lon":-6.3,"pa":12,"sog":10.2,"cog":185.0,"hdg":184,"age":5,"nm":2.3,"brg":210,"cpa":0.4,"tcpa":8.5,"dng":true}]}` (`cl` A, B, S base, N ayuda a la navegación, R aeronave SAR, T SART / MOB / EPIRB; `t` tipo de barco, `st` estado de navegación, `pa` / `age` s, `nm` millas, `brg` rumbo verdadero, `cpa` millas / `tcpa` minutos, `dng` alarma). `caps.ais` |
| (evento `ais`) | — | un barco nuevo o actualizado (como mucho cada 3 s por barco): los campos de `aislist` más `ds` destino, `len` / `bm` eslora y manga (m), `dr` calado, `eta` (`MM-DD HH:MM`), `imo`, `alt` (aeronave SAR), `txt` (mensaje 14), `ch`, `msgs` |
| `aisin` | `l` (frase `!AIVDM`) | frases del receptor AIS de la app (RTL-SDR); sin respuesta |
| `aisset` | `ch` (0 A, 1 B, 2 alternar), `cpa` (décimas de milla), `tcpa` (min), `guard` (décimas de milla, 0 apagada), `alarm`, `sart`, `clear`:true, `goto` (MMSI) | ajustes del receptor AIS (se guardan), borrar la lista o GO TO a un barco; `ok`. Modo 12 = *AIS receiver* (T-TWR con el módulo de VHF) |
| `txunlock` | `code` (128 cifras hexadecimales) · `lock`:true | T-TWR: desbloquea el rango de emisión con el código de este equipo (firma ECDSA de su MAC, ver `T_TWR.md`) o vuelve a las bandas de aficionado; `ok` o `err` (`wrong code for this device`, `too many attempts: wait 10 min`, `this firmware cannot be unlocked`). `info.sa868` lleva `txul` (desbloqueado), `txulav` (el firmware lo admite) y `mac` |
| `satfav` | `id` (NORAD; si falta, el elegido), `on` (true) | añade / quita un favorito (hasta 32, `sat.favorites`); `ok` y evento `sat`; `err` `favorites full (32)`. `caps.satFav` |
| `satcat` | `k` (0 todos, 1 70 cm, 2 LoRa, 3 GEO, 4 favoritos, 5 2 m, 6 los de la banda de esta radio) | categoría de la lista del tracker (la que recorren su botón y su encoder); `ok` y evento `sat` |
| `satsel` | `i` (posición en la lista) o `id` (NORAD) | elige el satélite (para la escucha en curso); responde `ok` y el evento `sat` |
| `sattxlist` | | evento `sattxlist` con los transmisores del satélite elegido: `i`, `m` modo, `b` baudios, `d` descripción, `up` / `dn` Hz nominales, `rx` / `tx` clase (CW, FSK, LoRa, FM, APRS, n/a), `rxd` / `txd` Hz con Doppler, `tone` CTCSS de la subida (Hz, si lo tiene), `aprs`:true = digipetidor APRS |
| `gpsaid` | `s`:`begin` · `s`:`c` (+ `d` = mensajes UBX en **base64**, máx. 180 caracteres por orden) · `s`:`end` · `s`:`abort` | A-GPS del receptor u-blox (`caps.gpsaid`): el tracker escribe los bytes tal cual en el puerto del GNSS. La app manda primero UBX-MGA-INI-TIME_UTC y MGA-INI-POS_LLH (hora y posición del móvil) y, si hay cuenta de u-blox, las Predictive Orbits (UBX-MGA-ANO...) descargadas con el internet del móvil. Espera el `ok` de cada orden. El tracker no bloquea su bucle: guarda hasta 1 KB y lo pasa a la UART según hay hueco (el M10 va a 115200 baudios, ~11 KB/s; a 9600 serían ~1 KB/s); si el trozo no cabe responde el error `busy` y la app repite ese trozo poco después |
| `satdata` | `s`:`begin` (+ `n` bytes) · `s`:`c` (+ `d` texto, **máx. 180 caracteres por orden**) · `s`:`end` · `s`:`abort` | sube el archivo `sats.txt` (formato en `SAT_MODE.md`, máx. `caps.satMax` bytes: 64 000 en la mayoría de placas, ~900 000 en la T-TWR) sin WiFi: espera el `ok` de cada orden antes de mandar la siguiente; con `end` valida, sustituye al archivo y recarga la lista |
| `tgtlist` | | evento `targets` |
| `pos` | `lat`, `lon` (grados), `alt` (m), `acc` (precisión en m) | posición GPS del móvil para el modo SAT (ver `SAT_MODE.md`); caduca a los 2 min, así que la app debe repetirla (p. ej. cada 5–10 s) |
| `reboot` | | reinicia |

## Eventos

`info`:
```json
{"e":"info","v":1,"fw":"5.3.2","board":"ttgo_t_beam_s3_SUPREME_v3",
 "caps":{"nmea":true,"dapnet":true,"cw":true,"modes":true,"rkey":true,"ota":true,"gpsaid":true,"rotor":true},
 "mode":0,"modes":[{"m":0,"n":"APRS LoRa"},{"m":1,"n":"APRS FSK 9600"},{"m":5,"n":"DAPNET pager"}],
 "profile":0,"profiles":[{"call":"EA7JRS-7","label":"Coche"}],
 "lora":0,"loras":[{"f":433775000,"sf":12,"cr":5,"fsk":false}],
 "cwMsgs":["CQ CQ DE {CALL}","..."]}
```

`rst` (solo si el último reinicio no fue normal): `CRASH`, `INT WDT`, `TASK WDT`, `WDT` o `LOW VOLTAGE` (caída de tensión).

`caps.radio` dice qué radio lleva la placa: `"lora"` (SX12xx / LLCC68) o `"sa868"` (LilyGO T-TWR Plus, radio FM con APRS AFSK 1200). Con `"sa868"` el evento añade `"sa868":{"rev":21,"fw":"NiceRF","band":"VHF","ok":true,"modem":"AFSK 1200","rx":144800000,"tx":144800000,"m17":144875000}` (revisión de la placa, firmware del módulo: `NiceRF` u `OpenRTX`, banda, si el módulo contesta, módem en uso: `AFSK 1200`, `G3RUH 9600` o `M17`, frecuencias en uso y la de M17), el modo 0 se llama `APRS AFSK 1200` y los otros modos de radio son 1 (APRS FSK 9600) y 10 (M17). `caps.m17` = la placa tiene M17 (orden `m17sms`); `caps.fm` = modo FM de voz (órdenes `fm*`, `qsy`, `carrier`). `caps.bcast` = receptor de radio comercial por I2C (`none`, `RDA5807M`, `Si4703`, `Si4735`; en todas las placas). `sa868` lleva además `dapnet` (frecuencia DAPNET en Hz) y `spkEsp` (el ESP32 alimenta el altavoz: Rev2.1 con silenciador automático o reducción de ruido). En la T-TWR también hay modo MORSE (A1A / MCW) y DAPNET. Sus ajustes (frecuencias, potencia, volumen, silenciador, nivel de emisión, TXDELAY...) están en el grupo `sa868` de la configuración (`cfgget` / `cfgset`); ver `docs/T_TWR.md`.

`status`:
```json
{"e":"status","mode":2,"profile":0,"lora":0,"digi":false,"smart":true,"sos":false,"batt":4.05,"up":3600,"bleKiss":true,"f":432050000,"mod":"CW"}
```
`f` = frecuencia (Hz) de la radio en el modo en curso y `mod` = su modulación (`LoRa`, `AFSK` en la T-TWR, `FSK`, `CW`, `POCSAG`, `M17`,
`FM`, `WSPR`, `FT8`, `FT4`, `FST4W`, `LR-FHSS`, `SAT`, `AIS`): en MORSE la del perfil CW Beacon o Telegrafía, en DAPNET la suya, en
WSPR / FT8 / FT4 / FST4W el dial de su perfil, en SAT la del receptor de satélites. Sin dato no aparece `f`.
(`batt` falta si no hay batería.)

`sensors` (cada campo solo si hay dato válido):
```json
{"e":"sensors","batt":4.05,"temp":21.4,"hum":55.0,"press":1013.2,"qnh":1015.0,"qs":"gps","baroAlt":512.3,"heading":87.0,"moving":false,"imuTemp":30.1,
 "net":{"src":"OWM","pl":"Sevilla","ds":"clear sky","t":24.1,"h":40,"p":1016.0,"ws":5.1,"wg":9.3,"wd":250,"aq":2,"age":420,"fc":"Fair, stable","d3":0.3}}
```

`gnss`:
```json
{"e":"gnss","fix":true,"lat":37.191512,"lon":-3.936701,"alt":512.0,"spd":0.0,"crs":0.0,"sats":11,"hdop":0.8,"age":420,"utc":"2026-11-05T07:45:00Z"}
```

`sky` (de las frases GSV / GSA / GST del receptor, `src/gnss_sky.cpp`): cada satélite visto en los últimos 12 s como
`[sistema, número, elevación °, azimut °, SNR dB-Hz, usado]`; sistema `G` GPS, `R` GLONASS, `E` Galileo, `C` BeiDou, `J` QZSS,
`S` SBAS, `N` otro; −1 = desconocido o sin señal. `pd` / `hd` / `vd` = PDOP / HDOP / VDOP de GSA, `err` = error horizontal en
metros de GST (solo si el receptor envía GST), `used` = satélites en el fix (GGA). Lo que no se sabe no aparece.
```json
{"e":"sky","pd":2.0,"hd":1.0,"vd":1.6,"used":8,"s":[["G",5,45,310,38,1],["E",16,55,330,42,1],["C",13,25,230,28,0],["R",73,15,100,25,0]]}
```

`dapnet` (más reciente primero) y `page` (uno nuevo, en el momento, solo en modo DAPNET):
```json
{"e":"dapnet","items":[{"ric":12345,"f":3,"own":true,"bad":false,"age":42,"rssi":-97,"t":"Texto del aviso"}]}
{"e":"page","ric":12345,"f":3,"own":true,"bad":false,"age":0,"rssi":-97,"t":"Texto del aviso"}
```

Solo en modo SAT, con *Send the rot event* activo (WebUI), cada vez que cambia el objetivo del rotor (y cada 10 s):
```json
{"e":"rot","az":123.4,"el":45.0,"trk":true,"src":0,"name":"ISS (ZARYA)","km":0}
```
`az` / `el` en grados (ya con los desfases de la WebUI), `trk` = sigue un objetivo visible, `src` = fuente (0 satélite, 1 estación APRS de la brújula, 2 objetivo fijo, 3 programa externo / GPredict, 4 última estación APRS oída con posición, LoRa o FSK, con el filtro `rotor.followFilter` y la antigüedad `rotor.followMaxMin`, 5 última baliza CW oída con locator; en la 5 `name` es «indicativo locator»), `name` = nombre del objetivo, `km` = distancia a un objetivo terrestre. `aaz` / `ael` = posición que devuelve el rotor (si la conoce). La app los retransmite a su rotor (p. ej. un `rotctld`). Con el montaje móvil el `az` ya es relativo al rumbo del vehículo y el evento añade `mob:true`, `abs` (azimut absoluto) y `hdg` (rumbo usado). Fuera del modo SAT el evento solo sale con una fuente terrestre (1, 2, 4 o 5). El acimut y la elevación se calculan desde la posición de la estación (GPS del tracker por defecto, `sat.posSource`). Con la estimación activada (`pr`), en las fuentes 1 y 4 una estación que se mueve se sigue donde estará ahora según la velocidad y el rumbo de su última posición, y `name` lleva `~` detrás. Evento `targets`: `{"e":"targets","sel":0,"src":2,"ff":"","fm":30,"pr":true,"pm":300,"items":[{"i":0,"n":"Repetidor X","lat":37.1,"lon":-5.9,"alt":850,"f":145600000}]}`.

Evento `cwrx` (escucha CW): `{"e":"cwrx","on":true,"t":"E","dbm":-104,"snr":15,"wpm":18}`: `on` = sesión en curso (false al terminar), `t` = letra decodificada (falta si solo es el nivel, una vez por segundo), `dbm` = nivel (RSSI), `snr` = dB sobre el ruido, `wpm` = velocidad estimada, `f` = frecuencia de escucha en Hz (solo en los eventos sin letra; la app la usa en los spots CW para PSK Reporter y RBN).

Evento `rf` (luces TX / RX de la app, al momento): `{"e":"rf","s":"tx"}` al empezar a emitir una trama (LoRa, FSK, AFSK, LR-FHSS), `{"e":"rf","s":"idle"}` al terminar y `{"e":"rf","s":"rx","rssi":-97,"snr":7.5}` con cada trama recibida. En el modo FM de la T-TWR la app usa además `ptt` / `busy` del evento `fm`.

Evento `led` (luces MSG / EMG y ventanas de alerta de la app, solo cuando algo cambia y al emparejar): `{"e":"led","msg":true,"unread":2,"emg":true,"ec":"EA7ZZZ-9","et":"Mic-E: Emergency!","ek":"emergency","em":false,"eq":7,"eo":true,"fl":true}`: `msg` = LED de mensajes sin leer del tracker, `unread` = mensajes APRS guardados, `emg` = alarma de emergencia sonando, `fl` = flash blanco encendido (avisos, lámpara Morse o linterna). Con una alarma sonando o su pantalla abierta: `ec` estación (indicativo, o nombre / MMSI en AIS) y `et` detalle, como en la pantalla del tracker; `ek` tipo (`emergency` = estado Mic-E de emergencia o SOS / EMERGENCY / MAYDAY en el texto, `mob` = MOB / MAN OVERBOARD, `ais_sos` = AIS-SART / MOB / EPIRB, `ais_cpa` = riesgo de colisión AIS); `em` = llegó en un mensaje (si no, en una baliza); `eq` cambia con cada alarma nueva (la misma estación no vuelve a sonar hasta 10 min sin alarmas suyas); `eo` = pantalla de alarma abierta en el tracker. La posición está en la lista `heard` (las alarmas AIS también entran ahí).

Evento `eai` (emergencia propia, al cambiar y cada 5 s mientras dura): `{"e":"eai","sos":2,"sent":3,"left":581,"by":"EA7URM-9","fm":true,"fmLeft":590,"fmBy":"mandown","fmMode":1,"md":false}`: `sos` = baliza SOS propia (0 no, 1 cuenta atrás de 5 s, 2 en curso; `sent` balizas enviadas; `left` segundos que quedan y `by` quién la inició, solo con la EAI remota por mensaje `EAI <código>`); `fm` = EAI de voz de la T-TWR en curso (`fmLeft` segundos, `fmBy` = `menu`, `app` o `mandown`; `fmMode` 1 intervalo, 2 continuo); `md` = aviso de hombre caído (`mdLeft` segundos hasta el EAI, `mdMin` minutos sin tocar nada). Órdenes `eai` y `alarm` arriba.

Mientras se escucha un satélite (modo SAT), cada línea recibida (texto CW, trama AX.25 en TNC2, paquete LoRa) y cada 5 s:
```json
{"e":"satrx","on":true,"k":"FSK","f":437425310,"dop":-3120,"fine":0,"rssi":-104,"n":7,"tx":2,"txt":"EA4XYZ-1>APRS:>telemetry ok"}
```
`k` = CW / FSK / LoRa, `f` = frecuencia sintonizada en Hz (con Doppler), `dop` = Doppler aplicado, `n` = tramas recibidas, `tx` = transmisor; `txt` solo cuando hay una línea nueva (el binario va en hexadecimal tras `x:`). Con `on:false` solo llega `on`.

Evento `sat` (modo SAT, cada 2 s y tras `satsel`): `{"e":"sat","ok":true,"n":"ISS (ZARYA)","id":25544,"i":3,"geo":false,"tx":4,"tle":1.2,"fav":true,"cat":4,"tot":412,"nfav":5,"stamp":"2026-11-09T03:17Z","pos":"GPS","az":123.4,"el":45.0,"rng":850,"rr":-1.234,"aos":1790000000,"los":1790000600,"max":62}` — `rr` velocidad radial (km/s, + se aleja), `aos`/`los` Unix UTC del paso actual o siguiente; `fav` el elegido es favorito, `cat` categoría de la lista (`satcat`), `tot` satélites en el tracker, `nfav` favoritos, `stamp` fecha del archivo. `info.caps`: `satMax` (bytes del archivo que caben), `satFav` (favoritos y categorías), `satFm` (T-TWR: el VFO de FM sigue al satélite).

## Control y datos completos para la app (`act`, `screen`, `msgs`, `heard`, `cfg`)

Regla del proyecto: **todas las funciones del tracker se controlan desde la app APRS LoRa+ de Android, y la app muestra todos sus datos** (sensores, mensajes APRS, DAPNET...). Matriz de cobertura en `docs/APP_COVERAGE.md`.

| Orden | Parámetros | Respuesta / evento |
|---|---|---|
| `actlist` | — | Un evento `actlist` por página: `{"e":"actlist","p":2,"pt":"MORSE","items":[{"n":"CW0","l":"Send CW 1","ok":false?}]}`; al final `{"e":"actlist","end":true,"pages":N}`. Solo salen las páginas visibles en el modo actual. `ok:false` = la opción existe pero no se ejecuta por BLE. |
| `act` | `a` = nombre (`n` de `actlist`, sin distinguir mayúsculas) | `ok` o `err` (`accion desconocida`, `no disponible por BLE`, `no disponible en este modo`). Ejecuta **cualquier** opción de los menús del equipo (baliza, perfil, mensajes, brújula, SOS, rotor, SAT, DAPNET, CW, reloj...). Si la opción es de otra página, el equipo cambia a esa página. No se ejecutan por `act`: abrir submenús, `MENU`, `MODE_CHANGE` (usa `mode`), escucha / entrenador CW interactivos y las sintonías WSPR / CW-baliza (tienen su orden propia) |
| `screen` | `p` página (opc.), `every` segundos (opc., 0 = no) | `ok` si cambia algo y evento `screen`: `{"e":"screen","p","cur","t":título,"r":derecha,"age":ms,"on":pantalla encendida,"rows":["...", ...]}`: el texto de la última pantalla dibujada (las filas de texto; gráficos como brújula o mapas no salen). También `sub` con `"screen":s` |
| `msgs` | `from`, `cnt` (1–20, 10) | `{"e":"msgs","tot","unread","from","items":[{"i","d":"R"\|"T","ep","st","ack","c","t"}]}`; `st`: 0 sin leer, 1 leído, 2 enviado, 3 esperando ack, 4 con ack, 5 fallido. 0 = el más reciente |
| `msg` | `to`, `t` (≤ 67), `ack` (true) | `{"e":"ok","c":"msg","ack":n}` y el mensaje sale por el aire con la siguiente ventana. Solo en modos APRS (también M17: va como paquete M17 de protocolo APRS) |
| `m17sms` | `to` (≤ 9; vacío = `@ALL`), `t` (≤ 800, una línea) | SMS M17 (protocolo 0x05) a otra radio M17; un solo envío, sin confirmación. `ok` cuando ha salido; `err` `not in M17 mode`, `not sent (callsign not valid for M17?)`. Solo con `caps.m17` y en el modo M17. Emite: la app exige indicativo validado |
| `fm` | — | Estado del modo FM de la T-TWR (evento `fm`; también solo, cuando algo cambia, como mucho 4 veces por segundo, y cada 5 s): `f`, `tx`, `sh` (−1 / 0 / +1), `ofs`, `tm` (0 sin tono, 1 TONE, 2 TSQL, 3 DCS, 4 1750 Hz), `t` (CTCSS en décimas de Hz), `d` (DCS, 23 = 023), `inv`, `w`, `hp`, `n` (memoria), `m` (memoria sí / no), `i`, `step` (Hz), `sq` (nivel en uso), `sqm` (manual), `sqa`, `nr` (0–3), `edit` (0 frecuencia, 1 tono, 2 desplazamiento, 3 silenciador), `busy`, `ptt`, `txs`, `rssi`, `scan` (0, 1 escaneando, 2 parado en señal), `arts` (0 apagado, 1 buscando, 2 en alcance, 3 fuera), `eai` (segundos que quedan), `aprs`, `qsyb`, `tok` (tono admitido por el firmware del módulo), `spk` (altavoz desde el ESP32), `note` |
| (evento `fm`, satélite) | — | mientras el VFO sigue a un satélite (`satrx` o menú FM *Satellite*) el evento `fm` lleva `"sat":{"n":"ISS (ZARYA)","id":25544,"tx":1,"f":145826100,"up":145824000,"dop":1100,"fine":0,"dok":true,"xup":false,"aprs":true,"tone":0,"sent":2,"heard":5,"az":123.4,"el":34.5,"txok":false,"why":"SAT TX off (WebUI)"}`: `f` escucha, `up` subida con Doppler (0 = no se emite en esta banda), `dop` Doppler de la bajada, `xup` la bajada está en otra banda y se escucha la subida, `aprs` digipetidor APRS, `tone` CTCSS, `sent` / `heard` tramas APRS enviadas / oídas, `txok` se puede emitir ahora (PTT, APRS) y si no `why` |
| `fmset` | cualquiera de `f`, `sh`, `ofs`, `tm`, `t`, `d`, `inv`, `w`, `hp` (canal: pasa al VFO) y `sq`, `sqa`, `step` (índice 0–8), `nr`, `aprs`, `qsyb`, `edit` | `ok` y evento `fm`; `err` `out of band or ARTS on`. Solo en el modo FM |
| `fmdial` | `d` (−50 … 50) | como girar el encoder `d` pasos (lo elegido en `edit`) |
| `fmmem` | `op` = `list` · `recall` (`i`) · `store` (`i`, `n` ≤ 8) · `del` (`i`) · `vfo` | memorias (32): `list` responde con el evento `fmmems` (`max`, `mem`: `i`, `n`, `f`, `sh`, `ofs`, `tm`, `t`, `d`, `inv`, `w`, `hp`) |
| `fmscan` / `fmarts` / `fmeai` | `s` = `start` · `stop`; `fmscan` además `mem` (true memorias, false banda) y `res` (0 portadora, 1 tiempo, 2 parar) | escaneo, ARTS, EAI |
| `qsy` | `c` (comentario APRS, p. ej. `146.940MHz T100 -060`) y `name` (nombre de objeto), o `f`, `ofs` (con signo), `tm`, `t`, `d`, `w` | sintoniza la emisora de voz (T-TWR). En FM al momento; desde otro modo guarda el canal en el VFO y reinicia en FM (`restarting`). `err` `no frequency in the text`, `not in the band of this radio` |
| `carrier` | `s` = `start` · `stop` | portadora sin modular de calibración en la frecuencia de emisión en uso (30 s como mucho; T-TWR): `{"e":"ok","c":"carrier","f":Hz,"ppm":…}`. Emite: la app exige indicativo validado |
| `bcast` | `sub` (segundos entre eventos; 0 = solo al cambiar la frecuencia o el nombre RDS) | radio comercial por I2C (`caps.bcast` ≠ `none`): evento `bcast` con `chip`, `on`, `band` (0 FM, 1 MW, 2 SW, 3 LW), `f` (kHz), `vol` (0–15), `mute`, `mono`, `rssi` (dBµV), `snr`, `st` (estéreo), `seek`, `ps` (nombre RDS), `rt` (RadioText), `pi`, `ct` (hora RDS UTC) y `ctOff` (medias horas), `bands` (las del chip), `presets` (10, kHz, 0 = libre) |
| `bcastset` | cualquiera de `on`, `band`, `f` (kHz), `step` (± canales), `seek` (+1 / −1), `vol`, `mute`, `mono`, `op` = `store` · `recall` · `del` con `i` (0–9) | `ok` y evento `bcast`; `err` `no broadcast receiver`, `band not available`, `bad preset`. No emite nada |
| evento `msg` | — | Se emite solo ante cualquier cambio del registro (llega uno, se envía, ack, leído): `{"e":"msg","tot","unread", + el más reciente}` |
| `heard` | `from`, `cnt` (1–12, 6) | `{"e":"heard","tot","pkts","items":[{"c","sym","k","lat","lon","alt","spd","crs","rssi","snr","age","pa","n","dev","path","cmt","sts","wx":{...}}]}`; `sym` = símbolo APRS con el **código primero y la tabla después** (`>/` coche, `&L` iGate LoRa); `age` = segundos desde el último paquete, `pa` = desde la última posición; 0 = la más reciente |
| `cfgget` | `o` = desplazamiento en bytes (0) | `{"e":"cfg","o","n":tamaño total,"b":bytes de este trozo,"d":texto}`: un trozo (hasta 320 bytes, sin cortar caracteres UTF-8) del fichero `/tracker_conf.json`, el mismo que edita la WebUI. La app lo pide con `o` creciente (`o += b`) hasta `o >= n`, lo junta y lo interpreta ella (el tracker no analiza el JSON aquí) |
| `cfgset` | `s`:`begin` (`n` = bytes totales, `reboot` true) → `s`:`c` (`d` = texto, ≤ 180 caracteres por orden) → `s`:`end` (o `abort`) | **Sustituye el fichero entero**: la app lee la configuración con `cfgget`, cambia lo que toque y la sube completa. Un trozo por orden y espera de `ok`. Al `end` el tracker comprueba el tamaño y que llaves y corchetes cuadren, guarda y reinicia (salvo `reboot:false`); al arrancar, `Configuration::readFile()` pone el valor por defecto a lo que falte o esté fuera de rango. Las claves (WiFi, PIN...) viajan tal cual: el enlace va cifrado y emparejado con PIN |

`sensors` incluye ahora todos los sensores: además de los anteriores, `wxSensor`, `gas` (kΩ), `iaq`/`iaqLabel`, `accel` y `gyro` ([x,y,z]), `flat`, `magCal`/`magAlign`/`magOff`, `charging`, `vbus`, `pmuTemp`, `battMa`, `battPct` y `heap`.

## Modos

`m`: 0 APRS LoRa (APRS AFSK 1200 en la T-TWR), 1 APRS FSK, 2 MORSE, 3 WSPR, 4 LR-FHSS, 5 DAPNET, 6 FT8, 7 FT4, 8 FST4W, 9 SAT (satélites 70 cm), 10 M17 (solo la T-TWR), 11 FM de voz (solo la T-TWR). `info.modes` dice cuáles admite la placa.

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

La actualización por internet es cosa de la app: el tracker no tiene cliente HTTPS (se quitaron `fwcheck`, `fwinstall`, `fwstatus` y el evento `fw`, y `caps.wifiUpd`).

## Tramas KISS del móvil

Desde esta versión el tracker guarda hasta 4 tramas recibidas del móvil por BLE (antes una sola: una segunda trama llegada antes de transmitir la primera la pisaba).
