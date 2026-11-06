# Changelog

## 5.3.4 — en desarrollo (sin publicar; la versión del código sigue en 5.3.3 hasta que lo decidas)
- **Región IARU automática:** el tracker deduce la región IARU (1, 2, 3) de la posición GPS (límites de la UIT simplificados: líneas A y C y la frontera R1/R3 por tierra; 108 puntos de referencia comprobados en el PC) y ajusta la región, el canal y la frecuencia de todos los perfiles (LoRa, FSK, FT8 / FT4 / FST4W, CW Beacon y Telegrafía). Apagada por defecto; panel *IARU region (auto)* al principio de *Transmission mode* en la WebUI. Modos: al arrancar y bajo petición (WebUI *Detect now* o SYSTEM > *IARU: detect now*) o también en marcha (60 s en la región nueva, sin oscilar en las fronteras). Con confirmación en pantalla (**Apply / Keep current**) o directa, a elegir. Un canal pasa a su equivalente en la región nueva (o al canal por defecto), y una frecuencia *Custom* se respeta si cabe en la banda de la región nueva y si no pasa al canal por defecto. Código del plan en `src/iaru_plan.cpp` (probado en PC: `tools/test_iaru_plan.cpp`; las tablas de canales se comparan con las de la WebUI con `tools/check_iaru_tables.py`; los límites los genera `tools/gen_iaru_region.py`). Sin probar en placa. Manual, apartado 20.
- **CI:** el paso «Merge for web flashing» de las placas ESP32-C3 usaba offsets antiguos (bootloader 0x1000, SPIFFS 0x310000) y fallaba con «Detected overlap» al crecer el firmware, así que el release no adjuntaba los binarios ni desplegaba el instalador web. Ahora usa 0x0 y 0x3D0000, como la tabla de particiones.

## 5.3.3 — 2026-11-05
- **Guía de pines en todas las placas:** la WebUI identifica los pines (libre / ocupado por la placa / con cuidado / solo entrada) en cualquier firmware, no solo en la Supreme. La lista la genera el firmware a partir de los pines que declara cada placa (radio, GPS, pantalla, botones, batería, flash, USB) y se lee en `/api/pins`; el botón de pines sugeridos rellena solo pines libres (el teclado FH-2, en uno ADC1). Los pines por defecto de LEDs, flash y buzzer pasan a «sin asignar» si los ocupa la placa (antes, por ejemplo, el 13 y el 14 chocaban con la radio de las Heltec V3), y el firmware rechaza esos pines y los de solo entrada para LEDs, buzzer, PTT y NMEA en todas las placas. Sin probar en placa.
- **T-Beam S3 Supreme, hardware externo:** esquema y reparto de pines para LED rojo (Tx), LED verde (Rx), flash de alta intensidad, buzzer KY-006 (pasivo), manipulador, teclado FH-2 y salida NMEA al conversor TTL-RS422 (`docs/HARDWARE_SUPREME_EXTRAS.md`). La WebUI identifica cada pin de la cabecera (libre, a evitar u ocupado por la placa), avisa si se repite y rellena los pines sugeridos con un botón. Los pines por defecto de LEDs, flash y buzzer en la Supreme pasan a 38 / 21 / 45 / 39 / 48 (antes chocaban con la radio, el RTC y el IMU). Nuevo en la WebUI: *NMEA output* (pin TX y velocidad de la UART, antes fijos en el firmware; aplica al reiniciar). El manipulador CW y el teclado FH-2 ignoran los pines que ocupa la placa, y la lista de pines reservados incluye USB y flash/PSRAM. Sin probar en placa.
- **CAT / Hamlib por WiFi:** servidor TCP (puerto 4532) con el protocolo *NET rigctl* de Hamlib (`rigctl -m 2`): frecuencia, modo (APRS LoRa = `PKTFMN`, APRS FSK = `PKTFM`), RSSI y potencia; PTT opcional = enviar una baliza APRS; ventana de sintonía configurable (0 = fija). No es una radio de audio. Comparte el enlace WiFi con KISS; interruptor en la WebUI (Connectivity > CAT over WiFi) y en SYSTEM (*WiFi CAT*). Especificación pública en `docs/CAT_HAMLIB.md`; protocolo probado en PC contra Hamlib 4.6.5 (`tools/cat_net`). Sin probar en placa. Manual, apartado 33.
- **LEDs de notificación seleccionables:** el LED verde (Rx) puede avisar de mensajes sin leer (parpadeo, y rápido con una emergencia) y/o dar un destello corto por cada baliza o paquete oído de otra estación; el LED blanco (flash) se enciende en sincronía con la sirena del zumbador durante una alarma de emergencia recibida o tu propia baliza SOS (sin zumbador: ráfagas de 200 ms por segundo, 5 minutos como máximo). **Modo CW** (un interruptor por LED): el rojo (Tx) sigue la portadora (tecla pulsada, también macros y baliza CW), el blanco hace de lámpara Morse (manipulador y entrenador) y el verde (Rx) se enciende con la señal CW recibida. La sección *Notifications* de la WebUI se reorganiza en un cuadro por dispositivo (LED rojo Tx, LED verde Rx, flash blanco, zumbador), cada uno con sus interruptores y su pin; todos apagados por defecto. Sin probar en placa.
- **CAT por USB:** el mismo protocolo del CAT por el puerto serie USB de la placa (115200 baudios), con interruptor propio en la WebUI (Connectivity > CAT; apagado por defecto, aplica al reiniciar). Con él activo el log de ese puerto solo muestra errores para no mezclarse con las respuestas, y `APRS-INFO?` del instalador web sigue funcionando. Hamlib no trae un modelo serie para este protocolo: el backend de `tools/hamlib_backend` añade el modelo «APRS LoRa+ tracker (USB)» (13 en el parche 4.7.2; DTR y RTS apagados para no reiniciar la placa). Probado en PC con `rigctl` 4.7.2 a través de un seudoterminal; sin probar en placa.
- **SYSTEM:** el estado de WiFi ya se ve (antes siempre ponía `off`): `CON` si el KISS por WiFi está unido a tu red, `..` mientras se une, `AP` en el punto de acceso. Con Bluetooth o WiFi activos, **1P** alterna a una vista **LINK** (no es una página más) con los detalles de las conexiones: Bluetooth (tipo, protocolo, nombre, móvil conectado) y WiFi (red, **IP**, puertos KISS, clientes, señal). Nuevo submenú (LP): **Bluetooth ON/OFF**, **BT data KISS/TNC2** (guardan y reinician) y **WiFi KISS ON/OFF** (al momento). Sin probar en placa.
- **KISS por WiFi:** probado con APRSdroid en Android por TCP en **LoRa y FSK**. El nombre de equipo (`aprs-lora-plus`) se fija antes de crear la interfaz WiFi, para que el router y mDNS lo vean en vez de «esp…» (muchas apps de Android no resuelven `.local`: usa la IP de la vista LINK).
- **Menú de 3 clics:** el selector *Power off / WiFi AP* del firmware base pasa a una ventana emergente pequeña y centrada titulada **APRS LoRa+** (en placas con páginas APRS); la lógica no cambia. La pantalla del **punto de acceso WiFi (WEB-CONF)** usa ahora el formato de las páginas (cabecera con batería, SSID, clave, IP, clientes y cuenta atrás de cierre por inactividad, que se refresca cada segundo), y los avisos de apagar / arrancar / cerrar el WiFi AP son ventanas pequeñas. Sin probar en placa.

## 5.3.2 — 2026-11-04
- **KISS por WiFi:** servidor TCP (puerto 8001 por defecto, como Dire Wolf) y UDP opcional para usar la radio LoRa desde APRSdroid (probado en Android), Xastir, YAAC y similares. La placa se une a tu WiFi de casa o al punto de acceso del móvil (la red que ya indicas en Administración > Firmware update) y responde también como `aprs-lora-plus.local`. Interruptor propio en la WebUI (Connectivity > KISS over WiFi), apagado por defecto, con lista opcional de direcciones permitidas. Solo APRS LoRa y APRS FSK, y placas ESP32-S3 / C3. Mantiene la WiFi encendida (más consumo, sin sueño profundo). Sin probar en placa. Ver el manual, apartado 32.
- Aviso de actualización de firmware en ventana emergente pequeña (título, dos líneas y barra de progreso) en lugar de texto desbordado en las OLED de 128x64.
- **Teclado Yaesu FH-2** (escalera de resistencias) en un pin ADC: las teclas 1–5 envían los mensajes CW 1–5 con la página MORSE abierta. Se calibra desde la WebUI (LoRa → CW / Morse → *FH-2 keypad*: pulsar la tecla y *Read*). Ver `docs/CW_MORSE.md`. Sin probar con el teclado real.
- **Vigilancia de grupo** (estilo ARTS de Yaesu) en los modos APRS LoRa y APRS FSK: lista de hasta 5 indicativos; página nueva **GROUP** con IN / OUT, distancia y tiempo; aviso en pantalla y zumbador al entrar o salir de alcance (por tiempo sin oírla y, opcional, por distancia máxima); sondeo opcional con un mensaje «ping» (mínimo 60 s). Apagada por defecto.
- **Página GROUP al estilo del Group Monitor (GM) del FT5DE:** cursor sobre los miembros con rumbo, punto cardinal y distancia; **GO TO** a la brújula; vista **Radar** tipo APL (propia posición en el centro, norte arriba, miembros numerados, IN relleno / OUT hueco, escala automática o de 1 a 500 km); mensaje rápido a un miembro o a todos los que están IN, y *ping* a uno o a todos. Sin probar en placa.
- **Baliza LR-FHSS con locator de 4, 6 u 8 caracteres:** nueva opción *Locator precision* en la WebUI (6 por defecto, como hasta ahora). El mensaje sigue siendo `PB1` (la longitud distingue el formato); el locator fijo se recorta a la precisión elegida. 8 caracteres revelan la posición con precisión de calle. Ver `docs/LRFHSS_BALIZA.md`.
- **Emergencia remota** (estilo EAI de Yaesu): un mensaje APRS `EAI <código>` desde un indicativo autorizado inicia la baliza SOS durante 1–50 minutos (sin cuenta atrás); `EAI STOP <código>` la termina; el botón la cancela. Responde `EAI ON/OFF`. Apagada por defecto. Ver el manual, apartado 31. Sin probar en placa.

## 5.3.1 — 2026-11-03
APRS FSK 9600: nuevo selector **Packet protocol (TX)** por perfil FSK: **AX.25** (como hasta ahora), **FX.25** (corrección de errores Reed-Solomon con 16, 32 o 64 bytes de control; sigue siendo AX.25 para las estaciones que no lo conocen) o **IL2P** (cabecera compacta + Reed-Solomon; solo la oyen estaciones IL2P). La recepción entiende AX.25, FX.25 e IL2P a la vez, sin ajustes. Codificadores y decodificadores escritos desde las especificaciones y verificados en PC contra Dire Wolf (GPL-2.0 o posterior): los 11 formatos FX.25 en los dos sentidos y 12 tramas IL2P idénticas byte a byte. Sin probar en placa. Las tramas que no caben en un bloque de 255 bytes se emiten como AX.25 normal. Ver `docs/FSK_G3RUH.md`.

**Instalación y actualización del firmware (5.3.1):**
- **Instalador web USB** (`web/index.html`, publicado en GitHub Pages): conecta la placa por USB, pulsa «Detectar placa» y la página lee placa y versión instalada, la compara con la última publicada e instala con un clic (Chrome/Edge; ordenador o Android). Conserva la configuración (no escribe la partición de datos). Los manifiestos y las imágenes los genera el CI (`tools/make_web_assets.py`) y **solo se publican con una release estable** (no con pre-releases ni ejecuciones manuales).
- **La placa se identifica**: línea `APRS-LoRa-Plus: board=… version=… chip=… ota=…` al arrancar y como respuesta a `APRS-INFO?` por el puerto serie; `GET /api/info` en la WebUI.
- **Actualización desde la WebUI** (Administración > Firmware update), solo en placas con dos copias de firmware (T-Beam S3 Supreme, T-Beam 1W): subir un `.bin` (se rechaza si es de otra placa) o «Check for updates / Install» por internet usando tu WiFi (comprobación SHA-256 y de tamaño antes de activar).
- **Actualización automática** opcional: en cada arranque la placa mira tu WiFi, busca una versión más nueva y, si lo permites, la instala. No actúa si el AP de configuración está siempre activo.
- Placas sin segunda copia (ESP32 clásicas, ESP32-C3, Heltec S3, T3S3…): solo por el instalador USB. T-Deck: sin esta función (núcleo Arduino antiguo).
- Sin probar en placa. Si una imagen defectuosa se instala, el arranque falla y hay que reinstalar por USB (el bootloader de Arduino no vuelve atrás solo).

## 5.3.0 — 2026-11-03
Nuevos modos **FT8 beacon**, **FT4 beacon** y **FST4W beacon** (120 y 300 s; solo transmisión, SX1262/SX1268, ranura sincronizada por GPS, GFSK generado por software; codificador de ft8_lib, MIT). Tarjeta propia en la WebUI con región IARU y aviso de segmento; enlace de GitHub de APRS LoRa+ visible en About; estudio de cobertura de todos los modos (`docs/ESTUDIO_COBERTURA.pdf`). LR-FHSS se puede activar desde la WebUI (apagado por defecto). D-Star/DPRS queda para 5.4. El código FT/FST4W solo se enlaza en placas SX1262/SX1268. Codificadores verificados en PC; sin probar en placa. NimBLE 2.5.2 y partición de 3,75 MiB en las ESP32 clásicas. Ver `docs/releases/RELEASE_5.3.0.md`.


## 5.2.12 — 2026-11-01
Selector GFSK/GMSK (filtro gaussiano del emisor) por perfil FSK; el bloque del L76K pasa al final de la sección GPS de la WebUI; cabecera «APRS LoRa+ / Tracker versión» y línea «APRS LoRa+ - by Manuel EA7JRS» añadida en About (la información original se conserva; la versión de la cabecera se lee del firmware). Incluye todo lo de 5.2.11 (modo DAPNET, calibración ppm, pantalla CW BEACON, campanas, modo estricto). Ver `docs/releases/RELEASE_5.2.12.md`.

## 5.2.11 — 2026-10-31
WebUI reorganizada: sección GPS (constelaciones, SBAS, AssistNow, PPS…; u-blox M10 y L76K), «Transmission mode» con un grupo por modo, «Beacon User Profile». APRS FSK con región/canal/frecuencia/potencia propios; CW con dos perfiles (Beacon y Telegrafía); WSPR con frecuencia fija; CAD mejorado (backoff exponencial, escucha por RSSI en FSK); baliza LR-FHSS restaurada y desactivada. Corregido el arranque de APRS FSK en LoRa. Soporte específico del Quectel L76K (constelaciones, 1–5 Hz, NMEA reducida, estado de antena). Campanas APRS (melodías tipo Yaesu FTM-400DR) para zumbador pasivo KY-006. Pantalla CW BEACON propia en modo MORSE. Calibración de frecuencia global en ppm para todos los modos. Cada modo emite solo lo suyo (sin paquetes APRS en CW/WSPR/LR-FHSS). Nuevo modo DAPNET (receptor POCSAG, solo recepción) y selector de modo del arranque sin solapes. Ver `docs/releases/RELEASE_5.2.11.md`.

## 5.2.10 — 2026-10-29
WSPR: el afinado de tonos deja de modular el PLL a 1 kHz (causaba un peine de líneas cada 50 Hz en la señal); cada símbolo se emite en dos tramos con un solo cambio. Nuevas opciones «Screen off while transmitting» y «Drift compensation», tabla de calibración por potencia y registro de temperaturas internas por serie. Ver `docs/releases/RELEASE_5.2.10.md`.


## 5.2.9 — 2026-10-27
Cabeceras de copyright de Manuel - EA7JRS en el código propio. Ver `docs/releases/RELEASE_5.2.9.md`.

## 5.2.8 — 2026-10-27
CW: antirrebote de 12 ms en el decodificador y límite superior para adaptar la velocidad. Ver `docs/releases/RELEASE_5.2.8.md`.

## 5.2.7 — 2026-10-27
WSPR: GPS siempre activo en modo WSPR, UART del GPS vaciada durante la emisión, locator con la última posición conocida, restauración limpia del SX1262. Ver `docs/releases/RELEASE_5.2.7.md`.

## 5.2.6 — 2026-10-27
WSPR: el índice de ranura se reinicia a las 00:00 UTC en `msUntilSlot()` (revisión externa). Ver `docs/releases/RELEASE_5.2.6.md`.

## 5.2.5 — 2026-10-26
WSPR: delay(1) cada 20 ms en la emisión (evita el watchdog; aportación del usuario); Supreme: guarda ifndef para BOARD_HAS_PSRAM. Ver `docs/releases/RELEASE_5.2.5.md`.

## 5.2.4 — 2026-10-26
Supreme: PSRAM QSPI activada en el entorno (la pantalla SYSTEM mostraba n/a). Ver `docs/releases/RELEASE_5.2.4.md`.

## 5.2.3 — 2026-10-26
TELEMETRY: matriz de pronóstico, base de nubes, tipo de aire y helada nocturna; línea de gas oculta sin BME680. Ver `docs/releases/RELEASE_5.2.3.md`.

## 5.2.2 — 2026-10-26
Identificación de equipos actualizada con la lista oficial aprs-deviceid (440 tocalls, 26 sufijos Mic-E, 6 prefijos Mic-E clásicos). Ver `docs/releases/RELEASE_5.2.2.md`.

## 5.2.1 — 2026-10-26
Iconos APRS en LAST APRS; velocidad vertical 0 con el tracker parado. Ver `docs/releases/RELEASE_5.2.1.md`.

## 5.2.0 — 2026-10-26
Modo WSPR beacon (pantalla WSPR, GPS, calibración de frecuencia). Ver `docs/releases/RELEASE_5.2.0.md`.

## 5.1.0 — 2026-10-25
Selector de modo, FSK 9600 G3RUH, plan de bandas IARU, modo MORSE (macros, manipulador, escucha RF, trainer). Ver `docs/releases/RELEASE_5.1.0.md`.

## 5.0.0
Interfaz de 11 pantallas APRS con un botón, alarmas y baliza SOS (EA7JRS) sobre CA2RXU 4.0.1 para T-Beam S3 Supreme.
