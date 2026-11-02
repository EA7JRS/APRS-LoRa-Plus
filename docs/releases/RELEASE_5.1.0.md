# LoRa APRS Tracker V5.1.0 — Notas de versión

**Placa de referencia:** LilyGO T-Beam S3 Supreme (SX1262) · **Base:** V5.0.0 (pantallas APRS, EA7JRS) sobre CA2RXU V4.0.1 · **Fecha:** 2026-10-25

## Novedades

### Selector de modo al arrancar
Tras la bienvenida, **SELECT MODE**: *APRS LoRa*, *APRS FSK 9600* o *MORSE / CW*. 1P = siguiente, LP = aceptar, 8 s = último modo APRS. MORSE no se guarda. *Change mode* (MAIN o submenú MORSE) reinicia y vuelve a preguntar.

### FSK 9600 AX.25 (G3RUH)
- Tramas AX.25 UI con CRC-16, HDLC, NRZI y scrambler G3RUH; 9,6 kbps, GFSK BT 0,5, desviación 3 kHz.
- Compatible con packet 9600 baudios; **no** con APRS analógico 1200 baudios.
- Parámetros por perfil en la WebUI (velocidad, desviación, ancho de banda Rx).

### Plan de bandas IARU (WebUI)
Región 1/2/3 y canal del plan de bandas por perfil; aviso si la frecuencia sale de la asignación de aficionados.

### Modo MORSE / CW
- Pantalla única MORSE con texto decodificado; sin barra de iconos.
- 5 macros editables (WebUI) con `{CALL}`, WPM 5–40, emitidas como portadora manipulada (A1A).
- Manipulador vertical o iambic A/B en jack de 3,5 mm con pines configurables (punta = dit, anillo = dah), sidetone de 700 Hz y *Swap paddles*.
- **Baliza CW autónoma:** intervalo y texto con `{CALL}`, `{LOC}`, `{VBAT}` en la WebUI; activable en el submenú. Sin sueño profundo entre balizas.
- **Listen CW (RF):** decodificador de envolvente sobre RSSI instantáneo.
- **Trainer** (método Koch): *listen* (5 caracteres, revelado a los 1,5 s) y *keying* (marcador ok/total); nivel 2–41 y Farnsworth en la WebUI.

### Otros
Versión 5.1.0 en la pantalla de bienvenida · documentación nueva: `MODOS.md`, `FSK_G3RUH.md`, `CW_MORSE.md` · manual y guía rápida actualizados.

## Cambios que afectan a la configuración
- Nuevos campos JSON por perfil: `fsk`, `fskBitRate`, `fskFreqDev`, `fskRxBandwidth`, `iaruRegion`. Nuevo bloque `cw` y `operatingMode` (solo 0 = LoRa o 1 = FSK; MORSE no se escribe).
- Los ajustes existentes siguen siendo válidos.
- El selector de modulación por perfil de la WebUI desaparece: la modulación la fija el modo elegido al arrancar.

## Estado de verificación
Probado en PC: AX.25/G3RUH, temporización y decodificación Morse, keyer, detector de envolvente y trainer; pantallas en simulador 128×64.
**Compilado y probado por el usuario en la T-Beam S3 Supreme (EA7JRS): compila y funciona.** Corregidos tras esa compilación dos tamaños de búfer en `pages_utils.cpp` (texto del trainer y cuenta atrás SOS). Sin comprobar a fondo en hardware: recepción FSK (experimental), temporización del CW, pines del jack, sidetone, selector de modo. Los perfiles AIS y el iGate no se han recompilado.

## Limitaciones conocidas
- FSK: tramas ≤ ~150 bytes, preámbulo ≤ ~50 ms, sin CAD.
- CW: decodificación RF solo de portadora manipulada; la escucha y el envío pausan el resto del Tracker.
- Para emitir CW hace falta SX1262/SX1268; FSK no existe en LLCC68.
- Las normas nacionales prevalecen sobre el aviso de plan de bandas.

Licencia GPL-3.0; se conserva la atribución a CA2RXU.
