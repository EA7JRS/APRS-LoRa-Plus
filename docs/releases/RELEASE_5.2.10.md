# LoRa APRS Tracker V5.2.10 — Notas de versión

Corrección de una señal WSPR sucia, detectada en las pruebas de campo con un SDR (GPSDO LBE-1420) y WSJT-X.

## Problema (5.2.9)
Con *Fine tone spacing* activado, el firmware reescribía la palabra del PLL cada 1 ms para aproximar la separación de tonos de 1,4648 Hz. Esa modulación a 1 kHz (más la pausa de `delay(1)` cada 20 ms) producía en la señal un **peine de líneas cada 50 Hz** con nivel comparable al de los tonos y líneas a ±500 Hz y múltiplos. WSJT-X decodificaba el mismo mensaje repetido en varias frecuencias (a −13 dB). La portadora de **WSPR Tune** (sin escrituras del PLL) salía limpia. Con la opción desactivada, la misma emisión se decodificó a **+11 dB** con una sola línea y drift 0.

## Cambios
- **`WSPR::Tones::plan()`** sustituye a `tick()`: cada símbolo se emite en **dos tramos** (palabra del PLL inferior durante `1−f` del símbolo y superior durante `f`), con **una sola escritura extra** del PLL a mitad del símbolo. La media por símbolo sigue siendo exacta (error ≤ 0,03 pasos = 0,03 Hz). Si la parte fraccionaria es < 0,03 o > 0,97 no hay segundo tramo.
- **Nueva opción «Screen off while transmitting»** (`wspr.screenOff`, WebUI → WSPR beacon, desactivada por defecto): apaga la pantalla durante la emisión (sin redibujos ni barra de progreso) y la enciende al terminar. El botón sigue abortando.
- **Compensación de deriva** (`wspr.driftTenths`, WebUI → *Drift compensation (0.1 Hz/min)*, −200..200, 0 por defecto): rampa lineal centrada en la mitad de la emisión; el centro calibrado no cambia. Se añade en `Tones::hz(tone, index)`. Positivo = la frecuencia sube durante la emisión (para un drift de −4 Hz/min en WSJT-X: `+40`).
- **Tabla de calibración por potencia** (`wspr.perPower`, `ppCorrHz[7]`, `ppDriftTenths[7]`; WebUI → *Per-power calibration table*): offset y compensación de deriva propios para 0, 3, 7, 10, 13, 17 y 20 dBm. Si está activada sustituye a los dos campos generales; la pantalla WSPR, las emisiones y WSPR Tune usan la fila de la potencia elegida. Sin tabla guardada, las filas parten de los valores generales. `WSPR::powerIndex()` convierte la potencia en índice.
- **Registro de temperaturas por el puerto serie** (`T+..s temp C: bme= imu= pmu= esp=`) cada ~10 s durante la emisión y cada 5 s en WSPR Tune, para correlacionar la deriva con las temperaturas internas. Solo informativo: no corrige nada.
- `tools/test_wspr_tones.cpp`: prueba en PC del generador de tonos.
- `docs/WSPR_CALIBRACION.md`: nuevo apartado 5.1 con lo medido en campo (potencia, calentamiento, estado estable, nivel del SDR, comprobación del espectro). Manuales ES/EN y `WSPR.md` actualizados.

## Corrección: pantalla WSPR tras varias emisiones
- «Sent #n» (y «Aborted») ya no tapan para siempre la cuenta atrás: «Sent #n» se borra a los 10 s y el estado se limpia al volver a activar la baliza.
- Tras emitir, el estado interno de la pantalla se sincroniza con el hardware: antes, si el modo ECO la había apagado, la emisión la encendía pero el modo ECO ya no volvía a apagarla.

## Pruebas y límites
- Test en PC del generador de tonos (palabras vecinas, error de media, separación de 1,4648 Hz entre símbolos, sin segundo tramo con la opción desactivada, rampa de deriva con media cero y pendiente correcta): sin fallos.
- **No compilado con PlatformIO ni probado en hardware**. Falta repetir una emisión con SDR y WSJT-X para confirmar que desaparece el peine con *Fine tone spacing* activado, y recalibrar el offset en estado estable a la potencia de uso.
