# LoRa APRS Tracker V5.2.1 — Guía rápida

**Placa:** T-Beam S3 Supreme (pantalla OLED 128×64) · **Versión:** 5.2.1 (base 4.0.1 + pantallas APRS + FSK + MORSE + WSPR) · **Autor de las pantallas:** EA7JRS

## 0. Al encender: elige modo

Bienvenida → **SELECT MODE**. **1P** = siguiente, **LP** = aceptar; a los 8 s arranca el último modo APRS.

| Modo | Qué es |
|---|---|
| **APRS LoRa** | El Tracker de siempre (11 páginas) |
| **APRS FSK 9600** | AX.25 G3RUH a 9600 baudios (11 páginas; no lo oyen equipos 1200 baudios) |
| **MORSE** | Una sola pantalla MORSE; nunca se guarda ni arranca solo |
| **WSPR beacon** | Baliza WSPR por GPS (pantalla única WSPR); nunca se guarda |

Para cambiar de modo: **MAIN → LP → Change mode** (en MORSE: LP → *Change mode*).

## 1. Un solo botón

| Gesto | En la pantalla normal | Con un submenú abierto |
|---|---|---|
| **1P** (pulsación corta) | Siguiente página | Siguiente opción |
| **LP** (pulsación larga) | Abre el submenú de la página | Ejecuta la opción marcada |
| **2P** (doble pulsación) | Menú clásico de configuración | Cierra el submenú |
| **3 o 4 pulsaciones** | Menú WiFi AP (como siempre) | — |
| **5 cortas o ··· −−− ···** | **Baliza SOS** (ver §4) | — |

Dentro del menú clásico, 2P vuelve a las pantallas. La barra de iconos de abajo aparece al cambiar de página y se oculta a los 3 s.

## 2. Las 11 páginas (modos APRS) (en este orden)

| Página | Qué muestra | Submenú (LP) |
|---|---|---|
| **MAIN** | Indicativo, cuenta atrás de la próxima baliza, hora UTC, posición, locator, último Rx, mensajes sin leer | Send beacon · Beacon mode · Next profile · Send SOS · Display off · Config menu |
| **MESSAGES** | Mensajes recibidos y enviados con su estado (confirmado, sin confirmar o NO ACK) | Read / reply · Browse list · Mark all read · Delete all · (en un mensaje) Reply · Quick reply · Delete |
| **LAST APRS** | Última estación oída: icono APRS, tipo, distancia, rumbo, S-meter (`S 8`, `S 9+10`), comentario | Next view · Older · Newer · **GO TO** |
| **STATIONS** | Lista de hasta 20 estaciones con distancia y antigüedad | Browse · Sort · Filter · Clear list |
| **GPS** | Coordenadas (DD con 6 decimales), locator, altitud, satélites, HDOP, rumbo | GPS on/off · Coords · Units · SBAS · Constellations |
| **COMPASS** | Rosa de los vientos y flecha hacia una estación (GO TO) | Target · Next station · Heading · North · Declination · Reset align |
| **ALTIMETER** | Altitud, velocidad vertical (0 si estás parado), QNH y perfil de altitud | Span · Reset profile · QNH |
| **TELEMETRY** | Temperatura, punto de rocío, humedad, presión + tendencia, gas, **pronóstico** | — |
| **CLOCK** | Hora local, fecha, orto y ocaso, luna (fase, salida y puesta) | Zone · Format · Sync from GPS |
| **LORA** | Frecuencia, SF, BW, CR, potencia, contadores, RSSI, S-meter, tiempo en el aire | Next profile · Power · Reset counters · Heard list |
| **SYSTEM** | Heap, PSRAM, Flash, batería, tiempo encendido, Bluetooth y WiFi | — |

## 3. Cosas útiles

- **GO TO:** desde LAST APRS, STATIONS (modo Browse) o una alarma: abre COMPASS apuntando a esa estación. Pita al llegar a menos de 50 m.
- **Respuesta rápida** (en el detalle de un mensaje): QSL, 73, TNX, WAIT, CALL ME, QSY CQ VHF, QSY CQ UHF, QSY RPT LOCAL. Se envían pidiendo confirmación.
- **Modo de baliza** (letra arriba a la derecha en MAIN): **A** automática, **S** SmartBeaconing, **M** manual (solo sale lo que mandes con *Send beacon*).
- **Potencia de LoRa:** LORA → LP → Power → +1 / −1 dBm. Se guarda y se mantiene al reiniciar.
- **S-meter (VHF/UHF):** S9 = −93 dBm, 6 dB por punto. Fila `S8 -99 SNR 5.0` en LORA.
- **APRS LoRa+ MAPRS transponder** (LilyGO T3 v1.6.1, 433 MHz): transponder AIS de clase B por APRS LoRa. Pon indicativo y datos del barco en la WebUI (*Beacons*) o en el plotter (Ajustes > Embarcación); el plotter y la app lo ven como AIS (`!AIVDO` el propio) por BLE o Bluetooth clásico (*Protocol: NMEA*). Ver el manual, apartado 38.

## 4. Emergencias

**Si recibes una emergencia** (Mic-E *Emergency*, o un mensaje/comentario con SOS, EMERGENCY, MAYDAY, MOB o MAN OVERBOARD): se enciende la pantalla, suena la sirena y el LED parpadea rápido. Pantalla de alarma con indicativo, distancia y rumbo.

| Pulsación | Efecto |
|---|---|
| **1P** | Silencia (otra 1P cierra la pantalla) |
| **LP** | **GO TO**: abre COMPASS hacia la estación |
| **2P** | Silencia y cierra |

**Si la emergencia eres tú:** 5 pulsaciones cortas o SOS en morse (corta = menos de 0,3 s; larga = 0,3–0,7 s). Cuenta atrás de 5 s con pitidos (**cualquier pulsación la cancela**). Después se envían un Mic-E *Emergency* y una baliza con el texto «EMERGENCY SOS», **cada 30 s hasta cancelar**. Se cancela repitiendo el gesto o con MAIN → LP → *Cancel SOS*. Mientras dure, «SOS» parpadea en la cabecera y el GPS no se duerme.

## 5. Modo MORSE

Pantalla única con el texto decodificado. **LP** abre el submenú:

| Opción | Qué hace |
|---|---|
| **1–5** | Envía la macro CW (editable en la WebUI, `{CALL}` = tu indicativo) |
| **Listen CW (RF)** | Decodifica CW recibido por RF (portadora a ±2 kHz, ≥ 6 dB sobre el ruido) |
| **Trainer: listen** | Suenan 5 caracteres Koch; los identificas de oído y a 1,5 s se revelan |
| **Trainer: keying** | Teclea con tu manipulador el carácter que se muestra; cuenta aciertos |
| **CW beacon: ON/OFF** | Baliza CW cada N min (WebUI): `DE {CALL}/B {LOC} V{VBAT}` |
| **Clear text / Change mode** | Borra la pantalla / vuelve al selector |

- El **botón** aborta cualquier envío, escucha o trainer.
- **Manipulador:** jack de 3,5 mm; punta = dit, anillo = dah, malla = GND. Pines, tipo (vertical, iambic A/B), WPM, sidetone y nivel del trainer en la **WebUI** (LoRa → CW / Morse).
- Mientras dura un envío o una escucha el Tracker está en pausa (sin balizas ni recepción APRS).
- Necesita SX1262/SX1268 para emitir. Sidetone y trainer de escucha, zumbador activo.

## 6. Modo WSPR beacon

Pantalla única: indicativo, locator del GPS, frecuencia, hora UTC y **cuenta atrás** a la próxima emisión (110,6 s, empieza 1 s después del minuto par). **LP**: *Beacon ON/OFF*, **Tune carrier**, *Change mode*. El **botón** aborta la emisión y deja la baliza en OFF.

- **WebUI:** frecuencia central (70 cm: dial 432,300 MHz USB = `432301500` Hz; 433,600 **no** es WSPR), potencia (0–20 dBm, por defecto 10), intervalo, locator fijo opcional, desplazamiento.
- **Calibra:** *Tune carrier* → mide con un SDR → `desplazamiento = nominal − medido`. Se necesita ±100 Hz (±0,23 ppm): el TCXO por sí solo no llega.
- Hace falta hora GPS. Filtro paso bajo de 70 cm antes de la antena. Solo bandas de aficionado.

## 7. Si algo no va

| Síntoma | Qué mirar |
|---|---|
| «WAITING FOR GPS» | Cielo abierto; en GPS → LP → *GPS on/off*. La posición en MAIN sale del GPS. |
| TELEMETRY dice «Sensor: none» | Activa *Telemetry* en la web de configuración y conecta un BME280/BMP280/BME680. |
| La brújula no marca rumbo | Placa plana; sin movimiento usa el magnetómetro, en marcha el rumbo GPS. |
| No suena ninguna alarma | Revisa en la configuración que el buzzer y el LED de mensaje estén activados. |
| Pantalla apagada | Cualquier pulsación la enciende. |
| WSPR: «NO GPS TIME» | Cielo abierto hasta tener hora GPS. |
| WSPR: nadie me decodifica | Calibra la frecuencia (desplazamiento) y comprueba dial, filtro y antena. |
| No aparece MORSE / FSK en el selector | Solo se ofrecen con SX1262/SX1268 (MORSE) y sin LLCC68 (FSK). |
| «Trainer needs buzzer» | Activa el zumbador en la configuración. |
| «Set key GPIO (WebUI)» | Define el pin del manipulador en la WebUI. |
