# Modo LR-FHSS (baliza de propagación)

*LoRa APRS Tracker V5.2.11 — Manuel - EA7JRS*

Modo de transmisión **propio, independiente del WSPR** y **desactivado por defecto**: no depende de la baliza WSPR ni se emite a la vez. Es una baliza de propagación por **LR-FHSS** (*Long Range – Frequency Hopping Spread Spectrum*) en una frecuencia propia de 70 cm. **Solo transmite**: la recepción y decodificación quedan para un firmware dedicado a placas con módulos capaces de recibirla (monitorización de propagación).

> Esta guía resume lo consultado en las fuentes oficiales citadas al final. **No es asesoramiento legal.** Antes de activarla, comprueba las condiciones de tu autorización con la Jefatura Provincial de Inspección de Telecomunicaciones o con URE.

## 1. Qué hace

- **Estado actual (5.3.0):** el modo se activa desde la WebUI, tarjeta «LR-FHSS Beacon», con el interruptor *Enable the LR-FHSS beacon* (apagado por defecto). Al activarlo aparece **LR-FHSS beacon** en SELECT MODE al arrancar (pulsación larga para aceptarlo, como MORSE y WSPR; no se guarda como modo predeterminado). Necesita hora GPS válida. Su submenú tiene su propio *Beacon ON/OFF*. El titular responde de la autorización que exijan las normas nacionales.
- Envía el texto ASCII `PB1 <indicativo> <locator> <dBm>`, p. ej. `PB1 EA7JRS IM67NG 14` (20 bytes, ≈ 1,9 s en el aire con CR 1/3 según la estimación del firmware). El locator puede ser de **4, 6 u 8 caracteres** (ver abajo); el formato sigue siendo `PB1` porque la longitud los distingue.
- **Ranura:** segundo **114** de un minuto par, cada 2–60 min (por defecto 10), con un desplazamiento que depende del indicativo. Al ser un modo aparte no se solapa con ninguna emisión WSPR.
- **Placas:** solo SX1262/SX1268 (T-Beam S3 Supreme). El SX1278 (T-Beam 1.2) no tiene LR-FHSS.
- La pantalla del modo LR-FHSS muestra indicativo, locator, potencia, frecuencia, ancho, hora UTC, cuenta atrás (`Next TX mm:ss`), intervalo y balizas enviadas, o el aviso correspondiente. El modo WSPR no tiene nada de LR-FHSS.

## 2. Configuración (WebUI → LR-FHSS beacon)

| Campo | Significado |
|---|---|
| Enable the LR-FHSS beacon | **Desactivada por defecto** |
| Centre frequency (Hz) | Por defecto `434000000` (IARU R1: centro de experimentos digitales) |
| Occupied bandwidth | 39,06 kHz … 1574,2 kHz. Por defecto **39,06 kHz** (dentro de los 125 kHz que el plan IARU R1 fija para datos tipo LoRa). Los demás dan aviso si superan 125 kHz |
| Coding rate | 5/6, 2/3, 1/2 o 1/3 (por defecto 1/3, el más robusto) |
| Power (dBm) | −9 a 22; es la que se anuncia en el mensaje |
| Transmit | Una baliza cada 2, 4, 6, 8, 10, 12, 20, 30 o 60 min |
| Fixed locator | 4, 6 u 8 caracteres; vacío = el de la posición GPS. Si es más largo que la precisión elegida se recorta; si es más corto se emite tal cual |
| Locator precision | 4, 6 (por defecto) u 8 caracteres |
| Narrow hopping grid | Rejilla de saltos de 3,9 kHz (activada) o 25,4 kHz |

**Precisión del locator** (a unos 37° N; tiempo en el aire estimado con CR 1/3):

| Caracteres | Casilla | Mensaje de ejemplo | Aire |
|---|---|---|---|
| 4 (`IM67`) | ≈ 177 × 111 km | `PB1 EA7JRS IM67 14` (18 B) | ≈ 1,8 s |
| 6 (`IM67NG`) | ≈ 7,4 × 4,6 km | `PB1 EA7JRS IM67NG 14` (20 B) | ≈ 1,9 s |
| 8 (`IM67NG45`) | ≈ 740 × 460 m | `PB1 EA7JRS IM67NG45 14` (22 B) | ≈ 2,0 s |

Con 4 caracteres el error de la distancia calculada puede pasar de 100 km, demasiado para estudiar propagación en 70 cm. 6 es el equilibrio recomendado. **8 caracteres revelan tu posición con precisión de calle**: úsalos solo si es lo que quieres emitir. Cada byte añade unos 50 ms con CR 1/3 (casi nada con CR 5/6). Un decodificador futuro debe aceptar las tres longitudes.

## 3. Protecciones de frecuencia del firmware

La frecuencia se comprueba antes de cada emisión, con el ancho ocupado más 2 kHz de margen por lado. **No se emite** si:
- queda fuera de **430–440 MHz**;
- invade **432,000–432,500 MHz** (señales débiles, WSPR y balizas exclusivas);
- invade **435–438 MHz** (servicio de satélites).

Se emite pero se avisa (`LRF: BW>125kHz!`) si el ancho supera **125 kHz**. Estos límites son una red de seguridad del firmware, no sustituyen a tu autorización ni al plan de bandas vigente.

## 4. Normativa consultada

**IARU Región 1, VHF Handbook v10.02 (marzo 2024), banda de 70 cm:**
- 432,100–432,400: 2700 Hz, todos los modos; 432,400–432,490: balizas exclusivas (500 Hz). El LR-FHSS no cabe en estos segmentos.
- 433,625–433,775: canales de comunicaciones digitales; 434,000: *centro de experimentos digitales* (con mención a LoRa).
- Nota (p): **125 kHz de ancho máximo para datos tipo LoRa**. Nota (m): los experimentos digitales de banda ancha deben mantenerse dentro del segmento correspondiente.
- 436–438 MHz: servicio de satélites y datos; no se permite infraestructura fija (nodos, pasarelas, *hot-spots*).
- El manual no menciona expresamente el espectro ensanchado.

**Orden IET/1311/2013 (Reglamento de radioaficionados):**
- Anexo I, 3.1: 430–440 MHz, 300 W p.c.e.; se admiten «otros tipos de modulaciones digitales», con anchos de emisión **acordes al plan de bandas de la IARU Región 1**. No menciona el espectro ensanchado.
- Art. 17.2: las emisiones con características distintas a las del reglamento requieren **autorización especial de la SETSI** (memoria técnica, nominativa, hasta 18 meses iniciales, solicitud con un mes de antelación).
- Art. 29: identificación con el distintivo al comienzo y al final de cada emisión (el mensaje lo incluye). Arts. 24–25: las radiobalizas desatendidas tienen reglas propias (ED, telemando, identificación cada ≤ 3 min, ≤ 50 W en VHF/UHF fuera de casco urbano).
- Anexo I, 3.4: en 433,050–434,790 MHz (ICM) el aficionado debe aceptar la interferencia de esas aplicaciones.

**Conclusión práctica:** con el ancho por defecto (39,06 kHz, ≤ 125 kHz) y 434,000 MHz, la emisión queda dentro de lo que el plan IARU R1 prevé para datos tipo LoRa. Con 137 kHz o más supera esa nota y puede requerir autorización especial.

## 5. Limitaciones

- **Solo transmisión.** Los chips SX1261/SX1262 modulan LR-FHSS pero no lo demodulan; hoy solo lo demodulan pasarelas LoRaWAN con concentradores SX1302/SX1303, y no hay receptor aficionado en 70 cm. Para recibirlo se prevé un firmware dedicado a placas con módulos capaces de decodificarlo.
- **Sin compilar ni probar en hardware** (5.2.11). Primera comprobación recomendada: un SDR con carga artificial, buscando los saltos de ≈ 0,1 s dentro de ≈ 39 kHz alrededor de la frecuencia elegida.
- Es un modo distinto del WSPR: para emitir WSPR hay que arrancar en modo WSPR y para LR-FHSS, en modo LR-FHSS (cambio de modo = reinicio).

## Fuentes

- [IARU Region 1 VHF Handbook v10.02 (marzo 2024)](https://www.iaru-r1.org/wp-content/uploads/2024/11/VHF_Handbook_V10_02.pdf)
- [Orden IET/1311/2013 (BOE-A-2013-7624)](https://boe.es/boe/dias/2013/07/12/pdfs/BOE-A-2013-7624.pdf)
- [Semtech: LR-FHSS en LoRaWAN](https://blog.semtech.com/lorawan-protocol-expands-network-capacity-with-new-long-range-frequency-hopping-spread-spectrum-technology)
