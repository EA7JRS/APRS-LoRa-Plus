# APRS LoRa+ 5.4.7

El tracker, la app Android, el rotor, la Display, el plotter náutico (con el instrumento de viento), la pasarela NMEA 2000
y la pasarela OBD-II llevan el mismo número de versión. El gateway sigue en espera.

**Tracker**
- **SUN y MOON con datos de este instante** (OLED y TFT). SUN: azimut con el rumbo, altura aparente subiendo o bajando,
  cuánto falta para la puesta o la salida, declinación, ecuación del tiempo y la sombra; el arco del día es más grande
  en la OLED. MOON: fase, parte iluminada, edad, azimut, altura y distancia, con el disco con sus mares y cráteres.
- **Seguir al sol o a la luna con el rotor** desde el submenú de SUN o MOON (*Rotor to Sun / Rotor to Moon*), con el
  icono de la antena en la cabecera.
- **SPACE WX:** número de manchas solares (SSN) y viento solar en tiempo real (velocidad, Bz, Bt). En las pantallas a
  color, **código de colores de NOAA**: G / R / S y rayos X en recuadros de amarillo a rojo oscuro, y las barras de Kp
  en verde por debajo de G1.
- **TELEMETRY > HF** (vista nueva): MUF, FOT y LUF estimadas y el estado de las bandas de 160 a 10 m, con las fórmulas
  de sat-aprs. La absorción por rayos X solo cuenta de día, como el modelo D-RAP de SWPC.
- **TELEMETRY:** el pronóstico usa también el tiempo de internet que manda la app. **STATIONS** y **CLOCK** bien
  colocados en la OLED.
- **Terremotos de ShakeNet** (Raspberry Shake) entre los avisos oficiales.
- **Identificador APRS oficial `APLJRT`** (aprs-deviceid, 2026-11-16; antes `APZLRT`). El evento BLE `rf` lleva el
  modem, la velocidad, la frecuencia y una huella de la trama para el iGate de la app.

**App Android** (versionCode 50407)
- **iGate según aprsc** (IGATE-HINTS): `qAO`, trama sin tocar, sin filtro de duplicados, terceros desenvueltos, nada
  guardado con el enlace caído; metadatos de recepción `#is2-meta`. Identificador oficial `APLJRA`.
- **Tiempo espacial:** SSN, viento solar en tiempo real, estimación de HF y el código de colores de NOAA; terremotos de
  ShakeNet.
- **Navegación:** corriente y rumbo a gobernar, situación por dos demoras, latitud por la meridiana y por la Polar,
  corrección total y tablilla de desvíos.
- **Receptor de la app:** NAVTEX (también al plotter por NRX), LSD / DSC del canal 70 y de MF / HF, radiobalizas de
  406 MHz, lista VOR para el rotor y la calibración del RTL-SDR, panadapter del RTL-SDR.
- **Radiotelefonía** (en el núcleo, con los mismos textos que el plotter): MAYDAY, MAYDAY RELAY, PAN PAN y SÉCURITÉ.
- **Android Auto:** estado de los mensajes y estaciones favoritas en las conversaciones; emergencias de carretera
  (V-27).
- **Rotor:** caja en Raspberry Pi con motores paso a paso y GPS propio.

**Plotter náutico**
- **Carta:** luces de la lista de la tarjeta con su color y ritmo; corriente y rumbo a gobernar; situación por demoras.
- **Astro:** latitud por la meridiana y por la Polar; corrección total y tablilla de desvíos.
- **NAVTEX** (por NRX) y **LSD / DSC** (sentencia DSC) con sus ventanas de socorro.
- **Radio:** MAYDAY, MAYDAY RELAY, PAN PAN y SÉCURITÉ para leer por el canal 16.
- Avisos de AEMET por su polígono; terremotos de ShakeNet; colores de NOAA en el núcleo común.

**Display**
- Space weather con el código de colores de NOAA; el núcleo común del plotter al día.

**Rotor**
- **Raspberry Pi** con el Waveshare Stepper Motor HAT y el Uputronics GPS/RTC HAT; modelo de Pi detectado (3B / 3B+ / 5).
- Placa nueva: **Waveshare ESP32-S3-Touch-LCD-7** (800 × 480). Emparejar la Raspberry Pi con una pantalla LCD-7 / 7B y
  con la app.

**Pasarela NMEA 2000 y pasarela OBD-II**
- Solo el número de versión.

Nada de esta versión se ha probado todavía en las placas, en el móvil ni contra los servidores reales: todo se ha
compilado en la CI y lo que se puede se ha probado en el PC.
