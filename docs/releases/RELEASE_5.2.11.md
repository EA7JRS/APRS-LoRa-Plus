# LoRa APRS Tracker V5.2.11 — Notas de versión

WebUI reorganizada (GPS, Transmission mode), perfiles propios para APRS FSK y CW, WSPR con frecuencia fija, CAD mejorado, baliza LR-FHSS restaurada (desactivada) y corrección del bucle de reinicios al arrancar. Parte de la 5.2.10 (WSPR).

## WebUI reorganizada
- **Nueva sección GPS** (menú CONFIGURATION): constelaciones (GPS, Galileo, BeiDou, GLONASS, QZSS, SBAS), opciones SBAS, AssistNow Autonomous, modelo dinámico, tasa de actualización, elevación mínima, static hold y salida PPS (frecuencia, anchura, flanco, retardos). Con **u-blox M10** se aplican por UBX CFG-VALSET (claves del manual SPG 5.00); con **L76K** solo constelaciones GPS/BeiDou/GLONASS y tasa 1 o 2 Hz (comandos CASIC `$PCAS`, **sin verificar en hardware**). El tipo de módulo se detecta al arrancar (`/gpsinfo`). Las combinaciones no válidas del M10 se avisan en la página. Los ajustes viven en `Config.gps` (ya no se usa `/gps_gnss.dat`).
- **«LoRa» pasa a «Transmission mode»**, con un grupo por modo: APRS LoRa, APRS FSK, CW Beacon, WSPR Beacon y LR-FHSS Beacon (reservado).
- En Beaconing, «Beacon» pasa a **Beacon User Profile**.
- **APRS FSK**: cada perfil tiene su propia región IARU, canal, frecuencia y potencia (`fskFrequency`, `fskPower`, `fskIaruRegion`); las configuraciones antiguas parten de los valores del perfil LoRa. Corregido un fallo previo: el modem FSK se elegía con un flag del perfil que ya no se activaba, por lo que APRS FSK podía arrancar en LoRa; ahora lo decide el modo elegido al arrancar.
- **CW / MORSE con dos perfiles** de radio (región, canal, frecuencia y potencia): *CW Beacon* (baliza autónoma) y *Telegrafía* (manipulador, mensajes, trainer y escucha). El cambio entre ambos es automático.
- **WSPR: frecuencia fija** 432 301 500 Hz, no modificable (campo de solo lectura; el firmware ignora el valor recibido).
- La potencia cambiada desde pantalla en FSK / CW se guarda en la configuración; la de LoRa sigue en `/loraPower.dat`.

## Corrección: bucle de reinicios al arrancar (assert xTaskGetIdleTaskHandleForCore)
`Configuration::readFile()` y el constructor de `Config` (objeto global, se ejecuta antes de que arranque FreeRTOS) llamaban a `ESP.restart()` tras reescribir una configuración incompleta o inexistente. Ahí `esp_restart()` aborta con `assert failed: xTaskGetIdleTaskHandleForCore` y, si la reescritura no se consolidaba, la placa quedaba en bucle. Ahora la configuración en memoria (con los valores por defecto de lo que faltaba) se guarda y se sigue arrancando, sin reiniciar.

## CAD / escucha antes de transmitir
- Un error del chip ya no cuenta como canal ocupado (solo la detección de señal LoRa).
- **Backoff exponencial binario** real (ventana 4, 8, 16, 32 ranuras) con pausa aleatoria entre intentos.
- **FSK**: detección de portadora por RSSI instantáneo (SX1262/SX1268); umbral `cadFskRssi` (−95 dBm por defecto).
- Espera máxima configurable (`cadWaitSec`, 1–60 s, 10 por defecto); pasado el tiempo se transmite igualmente.

## Baliza LR-FHSS (restaurada, desactivada)
Se restaura el modo LR-FHSS transmisor (SX1262/SX1268, solo emisión; `docs/LRFHSS_BALIZA.md`, `tools/test_lrfhss.cpp`). Queda **desactivado** (`lrfhss.enabled = false`): el modo no aparece en SELECT MODE y el grupo «LR-FHSS Beacon» de la WebUI está reservado, sin controles, hasta que se autorice su uso. Estudiado el repositorio `jumanamirza/LR-FHSS-receiver` (receptor MATLAB, uso solo académico, «All Rights Reserved»): no se integra código; sirve para validar nuestras emisiones con una grabación IQ (SDR) y su `lrfh_sim`.

## Pruebas y límites
- Test en PC de las utilidades LR-FHSS (`tools/test_lrfhss.cpp`): sin fallos. WebUI revisada en un navegador con un servidor simulado, sin errores de JavaScript. Lectura/escritura de la configuración probada en PC con la misma librería JSON.
- **No compilado con PlatformIO ni probado en hardware** (tampoco los comandos CASIC del L76K, el CAD por RSSI en FSK ni el transmisor LR-FHSS). Al arrancar, el monitor serie muestra `=== LoRa APRS Tracker 5.2.11 | build: ... ===`.


## Corrección del bucle de arranque (causa real)
`assert failed: xTaskGetIdleTaskHandleForCore` ocurría antes de `setup()`: el constructor global de `Configuration` montaba/formateaba/escribía SPIFFS, y las operaciones de escritura en flash no pueden hacerse antes de que arranque FreeRTOS. Ahora el constructor está vacío y `Config.init()` se llama al inicio de `setup()`; las variables globales que leían `Config` (`currentBeacon`, `currentLoRaType`, tamaños, eco, Bluetooth) se asignan justo después. Marca de build: `cfg-init-en-setup`.

## Canales IARU por modo (WebUI)
Contrastados con los planes oficiales: IARU R1 UHF-Bandplan (2021), IARU R2 Band plan (2020) e IARU R3-004 rev.2.
- **APRS LoRa**: sin 432,500 MHz (APRS estrecho de 12 kHz, no apto para LoRa de 125 kHz). Canales digitales R1: 433,625–433,775, 434,450–434,575, 438,025–438,175; 434,855 (Polonia) y 439,9125 (UK) son canales nacionales, no del plan IARU.
- **APRS FSK 9600**: incluye 432,500 MHz «New APRS frequency» (R1) más los canales digitales.
- **Telegrafía (CW)**: R1 432,025/432,050 (centro)/432,075/432,100; R2 432,025/432,050/432,100 y 33 cm 902,100/903,100; R3 432,050/432,100 (el plan R3 solo define 431,9–432,24 MHz «EME / señal débil»).
- **CW Beacon**: además de los de telegrafía, segmentos de balizas: R1 432,400–432,490 (432,410/430/450/470); R2 432,300–432,400 (432,320/350/380) y 903,000–903,100 (903,050). El plan R3 no define segmento de balizas. Los puntos dentro de cada segmento son sugerencias: consultar al coordinador nacional de balizas.
- Región 3: la asignación aficionado se limita a 430–440 MHz según el plan R3-004 (440–450 solo en Australia y Filipinas).
- Configuraciones que traían 433,775 MHz copiado al perfil CW se migran a 432,050 MHz.

## Pila de loopTask (guardar configuración)
Tras mover la configuración a `setup()` apareció `Stack canary watchpoint triggered (loopTask)` al guardar: la pila por defecto (8 KB) no alcanza para la serialización JSON anidada dentro de `setup()`. Se amplía a 16 KB con `SET_LOOP_TASK_STACK_SIZE`. Marca de build: `pila-16k`. Si la configuración quedó vacía por los cortes anteriores, se recrea sola con los valores por defecto.

## Perfiles perdidos tras un fichero de configuración dañado
Si `tracker_conf.json` estaba vacío o corrupto (cortes durante el guardado por el fallo de pila), la lectura seguía con un JSON vacío y la WebUI quedaba sin perfiles de usuario ni de transmisión. Ahora: (1) con error de lectura se carga la configuración por defecto completa (3 balizas, 4 perfiles LoRa regionales, AIS) y se guarda; (2) al leer, si faltan perfiles base, se completan hasta 3 balizas + 4 LoRa delante de los perfiles AIS. Probado en PC con fichero vacío y con fichero que solo contenía AIS.

## Guardado de configuración atómico
`writeFile()` escribe primero `/tracker_conf.tmp` y solo si termina bien sustituye `/tracker_conf.json`. Un corte o fallo a mitad de guardado ya no deja el fichero vacío. `versionDate` actualizada a 2026-10-31.

## Lectura de la configuración por la WebUI
`/configuration.json` se envía ahora en streaming desde SPIFFS (antes se montaba en un `String` carácter a caracter, lo que fragmentaba el heap con configuraciones de varios KB).

## Soporte específico del Quectel L76K (CASIC)
Fuentes: Quectel L76K Hardware Design V1.0 y la documentación de Waveshare (órdenes `$PCAS`). Nuevo `include/casic_pcas.h` (funciones puras) y `tools/test_casic.cpp`.
- Constelaciones GPS / BeiDou / GLONASS en cualquier combinación con máscara propia (`gps.cnMask`, por defecto GPS + BeiDou); Galileo, SBAS y QZSS no configurables.
- Tasa 1 / 2 / 4 / 5 Hz; sube sola a 115200 baudios por encima de 1 Hz, con comprobación y vuelta atrás.
- Salida NMEA reducida (`gps.nmeaLean`, por defecto activada).
- Estado de la antena (`GPTXT`) en el log y en la WebUI; satélites `BDGSV` incluidos en el recuento.
- Reconfiguración tras despertar el GPS en modo eco.
- WebUI: bloque «Quectel L76K», casillas no soportadas atenuadas, tasa de 200 ms a 1 s, BeiDou + GLONASS permitido.
- Sin implementar por falta de documentación verificada: modelo dinámico, standby y consulta de versión del L76K.
- Pruebas en PC: órdenes con los ejemplos del fabricante (checksum), PCAS03 de 14 campos, `GPTXT` y `BDGSV` con TinyGPS++ real, WebUI con ambos módulos. Sin probar en placa.

## Campanas APRS para zumbador pasivo (KY-006)
Nuevo `include/bell_rings.h` (melodías y reproductor sin bloqueos, sin dependencias de Arduino) y `tools/test_bell.cpp`. Nueve melodías inspiradas en la tabla *List of APRS Bell Rings* del manual del Yaesu FTM-400DR (ding = agudo, dong = grave): boletín, mensaje para mí, REJ, ACK, baliza recibida, mensaje de otra estación, mensaje y baliza duplicados, y transmisión propia. Se reproducen por `NOTIFICATION_Utils::update()` (llamada en `loop()`), por LEDC y sin `delay()`; la alimentación del zumbador se activa solo mientras suena. Prioridad por orden de tabla. Opción `notification.bellRings` (por defecto activada) en la WebUI, que sustituye a los pitidos clásicos de baliza enviada, mensaje recibido y estación escuchada. Ganchos en `msg_utils.cpp` para ACK, REJ, boletín, mensaje de otra estación y duplicados.
- Las alturas son una aproximación propia: el manual da notación musical, no frecuencias (ajustables en `BELL_TABLE`).
- Pruebas en PC: reproductor (duración, silencios, prioridad, reinicio). Sin probar en placa ni compilado en PlatformIO.

## Cada modo emite solo lo suyo
En MORSE/CW, WSPR y LR-FHSS el SX126x está configurado para ese modo, pero el gesto de emergencia (5 pulsaciones cortas / SOS en morse) arrancaba la baliza SOS APRS LoRa, y mensajes, ACK, digipeater, telemetría o Bluetooth también podían salir como paquete APRS. Ahora: (1) `LoRa_Utils::sendNewPacket()` descarta todo paquete APRS si el modo no es APRS LoRa ni APRS FSK (aviso en el log); (2) el gesto de emergencia y «Send SOS» en esos modos solo muestran «SOS: APRS modes only». El modo se elige en el arranque, así que no cambia con una emergencia en curso. Sin probar en placa.

## Pantalla CW BEACON (modo MORSE)
Nueva página `PAGE_CWB` («CW BEACON») junto a la de MORSE, visible solo en modo MORSE y con icono propio en la barra. Muestra indicativo, WPM, potencia y frecuencia del perfil CW Beacon, intervalo, texto expandido, estado (`Beacon OFF` / `Next TX mm:ss` / `Sent #n` / `Aborted`) y contador. Submenú: Beacon ON/OFF, Send now (nuevo), Change mode, Back. `sendCwText()` devuelve ahora si la emisión terminó; la baliza programada y «Send now» comparten `cwBeaconRun()`. Sin probar en placa ni compilado en PlatformIO.

## Calibración de frecuencia global (ppm)
Nuevo ajuste de corrección en ppm (en 5.2.11 `radioPpm100`, centésimas; desde 5.2.12 `radioPpm1000`, milésimas), ±50 ppm en la WebUI («Frequency calibration»): se suma a la frecuencia programada en el chip en APRS LoRa, APRS FSK, CW (TX y escucha) y LR-FHSS reservado (`LoRa_Utils::ppmCorrectionHz()` y `radioFreqMHz()`). La WebUI calcula los ppm a partir del error medido (Hz ÷ MHz). Nueva opción `wspr.useGlobalPpm` (WSPR usa los ppm globales en lugar de su offset en Hz; la tabla por potencia tiene prioridad). Nueva opción **Tune carrier** en la pantalla CW BEACON (portadora de 30 s en la frecuencia del perfil CW Beacon corregida). Pruebas en PC: guardado y lectura de la configuración, límites y WebUI con el servidor simulado. Sin probar en placa ni compilado en PlatformIO.

## DAPNET: receptor de buscapersonas POCSAG (solo recepción)
Nuevo modo `MODE_DAPNET` (SELECT MODE → DAPNET pager) para la red DAPNET (hampager.de): el SX1262 en FSK 1200 baud (512/2400 opcionales), recepción continua y bits en bruto; decodificador POCSAG en software (`include/pocsag.h`: sincronismo, BCH(31,21) con corrección de 2 bits, polaridad invertida, alfanumérico y numérico) y almacén de 10 mensajes (`dapnet_utils`). Página DAPNET con submenú (Older/Newer message, Clear list, Monitor all, Change mode). Filtro por RIC propios (hasta 8) o modo monitor; los mensajes propios suenan con la melodía «mensaje para mí»; opción de ajustar el reloj con los RIC de hora UTC (Skyper 2504, AlphaPOC 216, Swissphone 200, Birdy 2000). WebUI: grupo «DAPNET Pager» (439,9875 MHz, baudios, desviación, ancho de banda, RIC, juego de caracteres). El modo no emite nada. Selector de modo del arranque rediseñado: ventana de 4 opciones con flechas de desplazamiento y pie fijo (antes, con 5 o más opciones se solapaban con el mensaje y el temporizador).
- Pendiente de confirmar con la documentación de DAPNET: desviación, juego de caracteres por defecto, correspondencia RIC ↔ indicativo y formato de la hora.
- Pruebas en PC (`tools/test_pocsag.cpp`, fails=0): codificación BCH, corrección de 1 y 2 bits, todas las alineaciones, polaridad, mensajes largos en varios lotes, ruido, hora y lista de RIC; y la capa `dapnet_utils` con ASan/UBSan. **Sin probar en el aire ni compilado en PlatformIO.**
