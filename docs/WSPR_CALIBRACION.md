# Calibración de frecuencia para WSPR

*LoRa APRS Tracker V5.2.10 — Manuel - EA7JRS*

WSPR decodifica una señal si su frecuencia está dentro de **±100 Hz** del centro (a 432 MHz, ±0,23 ppm) y la deriva es pequeña. El oscilador del SX1262 (TCXO) suele tener un error de varios ppm, así que **hay que medirlo y corregirlo una vez**. Esta guía explica cómo.

## 1. Cómo corrige el firmware

| Ajuste (WebUI → WSPR beacon) | Significado |
|---|---|
| `Centre frequency (Hz)` (`wspr.freqHz`) | Centro nominal. 70 cm: **432 301 500 Hz** (dial 432,300 MHz USB; el centro cae a 1500 Hz de audio). |
| `Frequency offset (Hz)` (`wspr.corrHz`) | Corrección. La frecuencia emitida es **centro + offset**. |

- Paso del sintetizador del SX1262: 32 MHz / 2²⁵ = **0,954 Hz**. No se puede ajustar mejor que ±0,5 Hz; es más que suficiente.
- La portadora de prueba (**WSPR Tune**, submenú de la pantalla WSPR) emite exactamente en `centro + offset`.

**Fórmula (con el dial del receptor en 432,300 MHz USB y `a` = tono de audio medido, en Hz):**

```
offset_nuevo = offset_actual + (1500 − a)
```

Ejemplo real: con offset 0 se midió a = 1657,5 Hz → offset_nuevo = 0 + (1500 − 1657,5) = **−157,5 Hz**.

## 2. Qué necesitas

- Un receptor SDR (p. ej. SDRplay RSPdx-R2 con SDRconnect) y **una referencia de frecuencia fiable**. Sin ella, la medida incluye el error del propio SDR.
- **Mejor opción: un GPSDO** (p. ej. Leo Bodnar LBE-1420) como reloj externo del SDR. Comprueba en el manual del SDR el conector, la frecuencia (24 MHz en el RSPdx-R2) y el nivel admitidos.
- El tracker con fix GPS (para WSPR) y una antena o una carga **sin acoplar la salida directamente al SDR**: usa unos metros de aire o más de 40 dB de atenuación.
- Un software para leer la frecuencia del tono (la cascada de SDRconnect con cursor, Audacity, o enviar la grabación WAV para analizarla).
- Opcional: WSJT-X en modo WSPR para la comprobación final.

## 3. Preparar el SDR

1. Conecta el GPSDO al SDR como referencia externa y espera a que indique **GPS locked**. Deja que se estabilice unos minutos.
2. En el software del SDR activa la referencia externa y pon la **corrección de ppm en 0**.
3. Sintoniza **432,300000 MHz, modo USB**, ancho de banda de unos 3 kHz.
4. Ganancia baja y sin AGC si el nivel es muy alto: no debe saturar.

### Sin GPSDO
Calibra primero el SDR contra una señal de frecuencia conocida y estable (una portadora piloto de televisión digital o DAB cercana, o un patrón disponible) y ajusta la corrección de ppm del SDR. La precisión final será la de esa referencia. Si no puedes, la calibración servirá solo con ese SDR y, para otros receptores, puede quedar a cientos de hercios.

## 4. Medir el error del tracker

1. En la WebUI deja **`Frequency offset` en 0** y guarda. (Medir con el offset en 0 evita arrastrar correcciones antiguas.)
2. Enciende el tracker y **espera 10 minutos** con la caja cerrada y en su posición de uso: el TCXO cambia unos hercios al calentarse.
3. Lanza **WSPR Tune** en el tracker.
4. Graba **60 s** de audio del SDR y anota el tono estable `a` (Hz). Ignora los primeros ~20 s si aún sube.
5. Calcula `offset_nuevo = 0 + (1500 − a)` y redondea al entero.

Repite la medida **tres veces** (otro arranque o unos minutos más tarde) y usa la media. Si difieren más de 2 Hz entre sí, el tracker no se ha estabilizado: espera más.

## 5. Aplicar y verificar

1. Escribe el valor en **`Frequency offset (Hz)`** y guarda. Comprueba que la pantalla WSPR muestra `432.3015MHz ±xxxHz`.
2. Con el tracker caliente, repite Tune y graba otros 60 s. Resultado esperado: tono a **1500 ± 2 Hz**. Si no, corrige con la misma fórmula: `offset_nuevo = offset_actual + (1500 − a)`.
3. **Deriva:** la variación del tono dentro de una emisión debe ser **≤ 1 Hz cada 2 minutos**. WSPR tolera hasta unos 4 Hz/min, pero conviene que sea mucho menor.
4. **Ciclo WSPR completo:** graba los 110 s de una emisión (empieza 1 s después del minuto par; usa la cuenta atrás de la pantalla WSPR). En la cascada deben verse cuatro tonos separados 1,46 Hz que ocupan unos 6 Hz, sin lóbulos laterales.
5. **Decodificación (WSJT-X, modo WSPR, dial 432,300 MHz, reloj del PC sincronizado):** debe salir tu indicativo, locator y potencia. Revisa:
   - **DT** entre −0,5 y +0,5 s: confirma la sincronía de la hora GPS.
   - **Drift** próximo a 0 Hz/min.
   - **Frecuencia** del spot dentro de 1 Hz de la esperada.

## 5.1 Potencia, temperatura y estado estable (medido en campo)

- **La frecuencia depende de la potencia y del calentamiento.** En una unidad T-Beam Supreme con SX1262 se midió, con el mismo offset, un centro unos 3 Hz más alto al pasar de 7 a 13 dBm, y una deriva de 2–3 Hz durante los primeros ~100 s de la primera emisión. **Calibra con la potencia con la que vayas a operar**, y mide el centro en estado estable: haz 3 o 4 emisiones seguidas y usa las últimas.
- **No cambies a la vez el offset y la potencia**: no podrás separar los efectos. Tras un cambio de potencia espera unos 10 minutos antes de medir.
- **Pantalla apagada** (*Screen off while transmitting*): elimina una fuente de calor y de ruido de alimentación junto al TCXO. Actívala antes de calibrar si la vas a usar en servicio.
- **Nivel del SDR:** a 20 dBm a pocos metros un SDR sin atenuar se satura. Baja la ganancia hasta que no marque sobrecarga y aleja el tracker. Con 7 o 13 dBm a unos metros basta con System Gain de 40–50 dB.
- **Compensación de deriva.** Si WSJT-X muestra un drift constante en todos los ciclos (p. ej. −4 Hz/min), pon en *Drift compensation (0,1 Hz/min)* el valor `−10 × drift` (drift −4 → `40`). Es una rampa centrada en la mitad de la emisión: el centro calibrado no cambia. Repite 3 ciclos y comprueba que el drift tiende a 0; si empeora, el signo del WSJT-X es el contrario y hay que invertir el valor. La deriva depende de la potencia y del intervalo entre emisiones: recalíbrala si los cambias.
- **Tabla por potencia.** Activa *Per-power calibration table* y calibra cada potencia que vayas a usar: elige la potencia, calienta el tracker, mide el centro y el drift en 3–4 ciclos y escribe offset y deriva en su fila. Las filas que no uses pueden quedar como están. Al cambiar de potencia en la WebUI, la pantalla WSPR y Tune usan automáticamente la fila correspondiente. Mientras la tabla esté desactivada se usan los dos campos generales.
- **Registro de temperaturas.** Con el puerto serie abierto (115200), el firmware escribe cada ~10 s durante la emisión (y cada 5 s en Tune) una línea `T+..s temp C: bme=.. imu=.. pmu=.. esp=..`. Sirve para correlacionar la deriva medida con la temperatura de cada sensor (los sensores no están junto al TCXO; es un estudio, no una corrección automática).
- **Comprueba el espectro.** En WSJT-X debe salir **una sola línea** por emisión. Si aparece el mismo mensaje repetido a intervalos regulares de frecuencia (p. ej. cada 50 Hz), hay espurios en la señal; con la 5.2.9 la causa era el dither a 1 kHz (corregido en la 5.2.10).

## 6. Criterios de aceptación

| Prueba | Criterio |
|---|---|
| Tono de Tune tras calentar | 1500 ± 2 Hz |
| Repetibilidad entre arranques | ≤ 2 Hz |
| Deriva en 110 s | ≤ 1 Hz cada 2 min |
| Separación de tonos | 1,46 Hz (±0,1 Hz) |
| Decodificación WSJT-X | indicativo, locator y potencia correctos |
| DT | −0,5 … +0,5 s |

## 7. Mantenimiento

- El error de un TCXO cambia con la **temperatura y el envejecimiento**. Repite la medida si el tracker va a trabajar en un entorno muy distinto (frío o calor extremos) y, como mínimo, una vez al año.
- El offset es **por unidad**: cada tracker necesita el suyo.
- Anota los resultados:

| Fecha | Temp. ambiente | Offset usado | Tono medido (Hz) | Deriva (Hz/2 min) | Notas |
|---|---|---|---|---|---|
|  |  |  |  |  |  |

## 8. Seguridad y normativa

- 70 cm es banda de aficionado: emite con tu indicativo y respeta el plan de bandas y la potencia permitida.
- El SX1262 genera **armónicos**: para antena real usa un filtro paso bajo.
- No conectes la salida del tracker al SDR sin atenuación suficiente.
- Para las pruebas de calibración usa carga o antena corta y baja potencia.

## 9. Limitaciones conocidas

- La emisión WSPR completa y su decodificación **no se han verificado todavía en el aire** con este firmware; la portadora de Tune sí se ha medido.
- La hora la da el GPS (precisión estimada de ±0,3 s, por verificar con el DT de WSJT-X).

## 7. Corrección global en ppm (5.2.11)
Desde 5.2.11 existe una corrección del oscilador **global en ppm** (WebUI → Transmission mode → Frequency calibration) que sirve para todos los modos (APRS LoRa/FSK, CW) en emisión y recepción. Con la portadora de **CW BEACON → Tune carrier** o de WSPR Tune se mide igual que aquí; la herramienta «Apply to ppm» convierte el error en ppm. WSPR sigue usando su offset en hercios salvo que actives «Use the global frequency correction (ppm)»; para WSPR a ±2 Hz conviene el offset en hercios (la resolución de 0,001 ppm son unos 0,4 Hz a 432 MHz, redondeados a hercios enteros en WSPR).
