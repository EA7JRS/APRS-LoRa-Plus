# LoRa APRS Tracker V5.2.0 — Notas de versión

**Placa de referencia:** LilyGO T-Beam S3 Supreme (SX1262) · **Base:** V5.1.0 · **Fecha:** 2026-10-26

## Novedades
### Modo WSPR beacon
- Cuarta opción del selector de modo (*WSPR beacon*, solo SX1262/SX1268), con una única pantalla **WSPR**; no se guarda.
- Mensaje WSPR tipo 1 (indicativo del perfil sin SSID, locator de 4 caracteres, potencia), 162 símbolos 4-FSK a 1,4648 baudios, 110,6 s desde 1 s después del minuto par.
- Sincronismo por la hora del GPS (u-blox M10); el locator sale de la posición GPS en cada emisión (baliza «tracker») o es fijo.
- Tonos por cambio de la palabra del PLL del SX1262; separación afinada alternando dos pasos del PLL (opción *Fine tone spacing*).
- Intervalo configurable (2 a 60 min) con desplazamiento de ranura por indicativo, potencia 0–20 dBm, desplazamiento de frecuencia y **Tune carrier** para calibrar con un SDR.
- WebUI: bloque *WSPR beacon* (frecuencia central, desplazamiento, potencia, intervalo, locator fijo, afinado de tonos).
- Pantalla: indicativo, locator, potencia, frecuencia, hora UTC y satélites, cuenta atrás, barra de progreso `TX n/162`, emisiones enviadas. Submenú: *Beacon ON/OFF*, *Tune carrier*, *Change mode*.

## Aviso importante sobre la frecuencia
La frecuencia estándar de WSPR en 70 cm es **dial 432,300 MHz USB (central 432,3015 MHz)**, no 433,600 MHz. Es el valor por defecto. Además, WSPR exige ±100 Hz (±0,23 ppm): el TCXO del SX1262 no lo garantiza, hay que calibrar con *Tune carrier* y un SDR.

## Cambios que afectan a la configuración
Nuevo bloque JSON `wspr`: `freqHz`, `corrHz`, `powerDbm`, `intervalMin`, `locator`, `dither`. `operatingMode` sigue guardando solo LoRa o FSK. La versión mostrada es 5.2.0.

## Estado de verificación
Verificado en PC: el codificador WSPR coincide símbolo a símbolo con la implementación de referencia JTEncode en 910 mensajes; planificación de ranuras (incluido el cambio de día); plan de tonos y media del dither; pantalla y secuencia de emisión en el simulador (162 símbolos, ~110 600 ticks). **No hecho:** compilación con PlatformIO de estos cambios y pruebas en la placa: cambio de palabra PLL durante la portadora, calidad espectral con el dither, deriva de frecuencia, exactitud de la hora y decodificación real en wsprnet.org.

## Limitaciones
Solo mensajes tipo 1 (sin prefijos/sufijos ni locator de 6 caracteres); sin sueño profundo entre emisiones; portadora continua el 92 % del tiempo (potencia baja y filtro paso bajo de 70 cm recomendados); solo bandas de aficionado y con licencia.

Licencia GPL-3.0; se conserva la atribución a CA2RXU.
