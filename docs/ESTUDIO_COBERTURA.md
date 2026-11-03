# Estudio de cobertura y plan de bandas IARU — APRS LoRa+

*APRS LoRa+ v5.3.0 (FST4W-300, añadido después: ≈4 dB más sensible que el -120 por la duración de símbolo 2,6 veces mayor, estimación no medida) — Manuel - EA7JRS — noviembre de 2026*

> **Naturaleza del documento.** Es un **estudio teórico** (presupuesto de enlace y modelos de propagación estándar), no una medida de campo. Todas las cifras de alcance dependen de los supuestos de la sección 4 y deben validarse con pruebas reales. No se ha compilado ni modificado el firmware para elaborarlo.

## 1. Resumen

1. **LoRa APRS (SF12, 20 dBm)** es el modo con mejor relación alcance/infraestructura: unos **5 km en ciudad, 10 km en suburbano, más de 20 km en campo abierto** y el horizonte radio (≈28 km a 2 m/30 m) sobre terreno llano o mar. SF9 con CR 4/5 y BW 125 kHz alcanza aproximadamente el 60 % (detalle en la sección 5).
2. **APRS FSK 9k6** tiene un umbral unos **29 dB peor** que LoRa SF12: alcanza menos de 1 km en ciudad y ≈5 km en campo abierto. Su ventaja es la velocidad (0,06 s por paquete frente a 2,6 s).
3. **LR-FHSS** (baliza experimental, 10 dBm por defecto) tiene el mismo umbral que LoRa SF12 (−137 dBm, mejor sensibilidad publicada por Semtech) y, a igual potencia, el mismo alcance: 2,7 km en ciudad, 5,2 en suburbano y 18 en campo abierto a 10 dBm. La diferencia es que **solo lo demodulan pasarelas con SX1302/SX1303**: no hay receptor de aficionado en 70 cm, de modo que hoy su cobertura útil depende de montar uno.
4. Los **modos de señal débil** (WSPR, FST4W, FT8, FT4) tienen umbrales entre **18 y 33 dB mejores** que LoRa SF12 (FT4 el más cercano, FST4W-120 el más lejano), pero **solo los recibe una estación con SSB/SDR y WSJT-X**, no la red de iGates. Con un tracker a 20 dBm y una yagi de 12 dBi en el receptor, el límite pasa a ser el horizonte radio, no la potencia.
5. **FST4W-120 solo gana ≈1,8 dB a WSPR** con el mismo ancho de banda (5,9 Hz): su interés es la compatibilidad con la base de usuarios de WSJT-X 3.x, no el alcance.
6. **FT8 y FT4** quedan 10 y 13,5 dB por encima de WSPR en umbral (es decir, son menos sensibles), a cambio de ciclos de 12,6 s y 5 s: mucha más información por unidad de tiempo, menos alcance.
7. **DAPNET (POCSAG) y D-Star** son modos de **recepción** en el tracker: su cobertura la fija la **PIRE del transmisor**, no el tracker. Un hotspot Pi-Star de 10–50 mW da del orden de **0,5–1 km en ciudad** y ≈3–6 km en campo abierto.
8. **Plan de banda:** hay tres puntos de las frecuencias por defecto que conviene revisar (sección 3.3): LoRa de 125 kHz frente al límite de 25 kHz del plan R3, el perfil 434,855 MHz dentro de las salidas de repetidor de R1, y la posición de WSPR en R2.

## 2. Modos considerados

| Modo | Estado | Función del tracker | Ancho ocupado | Chips |
|---|---|---|---|---|
| APRS LoRa | existente | TX + RX | 125 kHz | SX126x, SX127x, LLCC68 |
| APRS FSK (G3RUH 9600 bd) | existente | TX + RX | ≈12 kHz (±3 kHz de desviación) | SX126x, SX127x (no LLCC68) |
| CW / Morse | existente | TX | ≈100–200 Hz (según velocidad) | SX1262/68 |
| WSPR | existente | TX | 5,9 Hz | SX1262/68 |
| DAPNET (POCSAG) | existente | solo RX | ±4,5 kHz | SX1262/68 |
| LR-FHSS | existente, desactivado por defecto | TX | 39 kHz | SX1262/68 |
| FT8 | nuevo, estudiado | TX | 50 Hz | SX1262/68 |
| FT4 | nuevo, estudiado | TX | 83 Hz | SX1262/68 |
| FST4W-120 | nuevo en 5.3.0 | TX | 5,9 Hz | SX1262/68 |
| D-Star / DPRS | nuevo, estudiado | RX primero, TX después | ≈6,25 kHz (GMSK 4800 bps) | SX126x (por validar) |

No se consideran DMR, Fusion, P25, NXDN ni M17: usan 4-FSK y el chip solo demodula 2 niveles. LR-FHSS se calcula con la sensibilidad publicada por Semtech; su cobertura útil depende de una pasarela con SX1302/SX1303, que no existe en la red de aficionados (el chip del tracker modula LR-FHSS pero no lo demodula).

## 3. Plan de bandas IARU en 70 cm

### 3.1 Segmentos relevantes por región

**Región 1 (Europa, África, Oriente Medio)** — *IARU R1 VHF Handbook v10.03, febrero de 2026, págs. 41–44*:

| Segmento (MHz) | Ancho máx. | Uso |
|---|---|---|
| 432,000–432,400 | 2700 Hz | todos los modos; centros de actividad: 432,050 telegrafía, 432,200 SSB, 432,350 enlace de microondas, 432,370 meteor scatter |
| 432,400–432,490 | 500 Hz | telegrafía y MGM, **balizas en exclusiva** |
| 432,491–432,493 | 500 Hz | MGM experimental |
| 432,500–432,975 | sin límite | **«nueva frecuencia APRS» (432,500)**; entrada de repetidores de R1 |
| 433,400–433,575 | sin límite | FM y voz digital |
| 433,600–434,000 | sin límite | todos los modos (433,625–433,775: canales de comunicaciones digitales, notas g, h, i) |
| 434,000 | – | centro de experimentos digitales; nota (p): datos hasta 125 kHz «como LoRa» |
| 434,600–434,975 | sin límite | **salida de repetidores de R1**, canales de 12,5 kHz |
| 438,025–438,175 y 438,200–438,525 | – | canales digitales y de repetidores digitales |
| 439,800–439,975 | – | enlaces de comunicaciones digitales; 439,9875 POCSAG en uso nacional (nota q) |

**Región 2 (Américas)** — plan IARU R2 (`hamwaves.com/bandplans`): 432,000–432,025 CW/EME; 432,025–432,100 CW y modos digitales (DM), señal débil, 500 Hz; 432,100–432,300 CW/SSB, señal débil, 2700 Hz (llamada 432,100); 432,300–432,400 balizas CW (500 Hz); 432,400–432,420 balizas y balizas digitales (ACDS, 2700 Hz); 432,420–433,000 CW/SSB/DM; 433,000–433,100 DM/IVG; 433,100–435,000 y 438–450 «opción local». El plan no define segmento APRS en 70 cm.

**Región 3 (Asia-Pacífico)** — *IARU R3-004 (versión publicada en enero de 2025)*: 430–431,9 todos los modos (25 kHz máx.); 431,9–432,24 EME y señal débil (2700 Hz); 432,24–435 todos los modos (**25 kHz máx.**); 435–438 satélites; 438–440 todos los modos. No define balizas ni canales digitales o APRS.

> La extracción de texto de la v10.03 de R1 tiene tablas desordenadas (p. ej. «433.600–433.5875»). Las filas anteriores se han contrastado con el plan R1 de 2020. **Antes de fijar un canal, comprobar las páginas 41–44 del PDF original y el plan nacional (URE en España).**

### 3.2 Frecuencias que usa hoy el proyecto

| Perfil | Frecuencia | R1 | R2 | R3 |
|---|---|---|---|---|
| LoRa 1 (APRS) | 433,775 MHz, 125 kHz | dentro de 433,600–434,000 «todos los modos»; ocupa 433,7125–433,8375, es decir, **más ancho que el segmento de canales digitales** 433,625–433,775 | opción local | **supera los 25 kHz máx.** del segmento 432,24–435 |
| LoRa 2 | 434,855 MHz | **cae en 434,600–434,975, salida de repetidores de R1** (uso nacional, p. ej. Polonia) | opción local | supera 25 kHz |
| LoRa 3 | 439,9125 MHz | dentro de 439,800–439,975 (enlaces digitales); uso nacional (Reino Unido) | opción local | supera 25 kHz |
| APRS FSK 9k6 | 432,500 MHz (≈12 kHz) | **frecuencia APRS de R1** | opción local | cabe en 25 kHz |
| CW | 432,050 MHz | centro de telegrafía | CW señal débil (500 Hz) | EME/señal débil (2700 Hz) |
| CW baliza | 432,41–432,47 MHz | baliza en exclusiva | R2 usa 432,300–432,400 | sin segmento |
| WSPR | dial 432,300 / RF 432,3015 | 432,1–432,4, 2700 Hz | **inicio del segmento de balizas CW (500 Hz)** | 432,24–435, correcto |
| DAPNET (RX) | 439,9875 MHz | uso nacional (nota q) | – | – |
| LR-FHSS | 434,000 MHz, 39 kHz de ancho ocupado (saltos de ≈0,5 kHz) | centro de experimentos digitales, nota (p): ≤125 kHz | opción local | **39 kHz supera los 25 kHz máx.** del plan R3 (cada salto ocupa ≈0,5 kHz; la interpretación del límite es dudosa) |

### 3.3 Puntos a revisar

1. **LoRa de 125 kHz en R3**: el plan R3-004 fija 25 kHz como máximo en 432,24–435 MHz. Con la región 3 seleccionada, la WebUI debería avisar de que el perfil LoRa supera ese límite (hoy solo comprueba que la frecuencia esté dentro de la banda de aficionado).
2. **434,855 MHz en R1** coincide con la salida de repetidores. Es una práctica nacional documentada, pero el aviso debería aparecer cuando se elija ese canal.
3. **WSPR en R2**: el dial 432,300 es el valor habitual de WSJT-X, pero el plan R2 reserva 432,300–432,400 a balizas CW de 500 Hz. La señal WSPR (6 Hz) cabe en ese ancho; es una cuestión de convenio, no de ancho.
4. **LR-FHSS**: el firmware ya se niega a emitir fuera de 430–440 MHz, en 432,000–432,500 MHz y en 435–438 MHz, y avisa si el ancho supera 125 kHz (`docs/LRFHSS_BALIZA.md`). En España, además, la Orden IET/1311/2013 (art. 17.2) puede exigir autorización especial de la SETSI para emisiones de características distintas a las reglamentarias: por eso el modo está desactivado por defecto.
5. **Balizas automáticas**: un tracker que emite WSPR, FST4W o CW de forma desatendida se comporta como una baliza. En R1 las balizas tienen el segmento 432,400–432,490 en exclusiva y se coordinan con el responsable nacional de balizas.

### 3.4 Propuesta de frecuencias para los modos nuevos

Los planes IARU definen **segmentos**, no canales para FT8, FT4 ni FST4W. WSJT-X solo propone 432,174 MHz para FT8. Propuesta, pendiente de que el usuario la confirme:

| Modo | R1 | R2 | R3 |
|---|---|---|---|
| FT8 | 432,174 (dentro de 432,100–432,400) | 432,174 (432,100–432,300, señal débil) | 432,174 (431,9–432,24, señal débil) |
| FT4 | dentro de 432,100–432,400, **sin canal propuesto** (evitar centros de actividad 432,200/432,350/432,370) | dentro de 432,100–432,300 | dentro de 431,9–432,24 |
| FST4W-120 (baliza) | 432,400–432,490 (balizas MGM), coordinar con el responsable nacional | 432,400–432,420 (balizas digitales) | 432,24–435 |
| LR-FHSS (baliza) | 434,000 (centro de experimentos digitales, ≤125 kHz, nota p) | opción local | 432,24–435 (revisar el límite de 25 kHz) |
| D-Star / DPRS | el del repetidor o hotspot: 433,400–433,575 (voz digital), 438,200–438,525 (repetidores digitales) | opción local | según el plan nacional |

La WebUI ya tiene región IARU por perfil y listas de canales por modo (`IARU_CHANNELS*` en `script.js`); los modos nuevos reutilizarían ese mecanismo y añadirían un **aviso de segmento** (frecuencia fuera del segmento de señal débil o de balizas de la región elegida).

## 4. Método y supuestos

**Presupuesto de enlace.** Pérdida de trayecto admisible = P_tx + G_tx − margen + G_rx − L_rx − sensibilidad.

| Parámetro | Valor | Origen |
|---|---|---|
| Potencia del tracker | 10 y 20 dBm (el chip llega a +22) | configuración del proyecto |
| Ganancia de antena del tracker | −2 dBi (látigo, cuerpo, desadaptación) | **supuesto** |
| Margen de desvanecimiento | 10 dB | **supuesto** |
| Antena de la estación receptora | omni 3 dBi (iGate); yagi 12 dBi (estación de señal débil) | **supuesto** |
| Pérdida de cable receptor | 1 dB | **supuesto** |
| Frecuencia de cálculo | 432,5 MHz | – |

**Tiempo en el aire LoRa.** Fórmula estándar de Semtech con cabecera explícita, CRC, preámbulo de 8 símbolos y optimización de baja velocidad (DE) en SF11/SF12; con ella, 20 B en SF12/125 kHz dan 1,32 s, igual que los 1318 ms de la nota AN1200.64 de Semtech.

**Sensibilidad** = −174 dBm/Hz + 10·log(B) + figura de ruido del sistema + SNR umbral. La figura de ruido combina el ruido externo del escenario (2, 5 u 8 dB, **supuestos** basados en la mediana de ruido artificial de ITU-R P.372 a 432 MHz, ≈4 dB en ciudad, más un margen por interferencia local y el solape con ISM/LPD de 433 MHz) con la del receptor (6 dB en SX126x, 2 dB en un SDR con preamplificador).

| Modo | SNR umbral | Origen |
|---|---|---|
| LoRa SF12 / SF9 (BW 125 kHz, CR 4/5) | −20 / −12,5 dB | tabla del fabricante de LoRa (CR 4/5); el efecto de otros CR no se modela |
| APRS FSK 9k6 (39 kHz de ruido) | +14 dB | **supuesto** (G3RUH sin FEC) |
| LR-FHSS CR 1/3 (488 Hz por fragmento) | +4,1 dB | calibrado para dar −137 dBm sin ruido externo, la mejor sensibilidad publicada por Semtech (AN1200.64, tabla 4); **supuesto** que se mantiene con 39 kHz de ancho ocupado, un caso que esa nota no cubre (usa 137 y 336 kHz) |
| POCSAG 1200 (14,6 kHz) | +10 dB | **supuesto** |
| D-Star GMSK 4800 (12,5 kHz) | +12 dB | **supuesto** |
| CW al oído, filtro de 200 Hz | −5 dB en 2500 Hz | **supuesto** (6 dB sobre el ruido en 200 Hz) |
| WSPR / FST4W-120 / FT8 / FT4 | −31 / −32,8 / −21 / −17,5 dB en 2500 Hz | **tabla 7 de la guía de WSJT-X 3.3.0** |

**Propagación.** Hata (urbano, suburbano, zona abierta) con la estación a 30 m y el tracker a 1,5 m; dos rayos sobre terreno llano o mar (tracker a 2 m); espacio libre desde una cima a 600 m sobre la zona. Hata solo es válido hasta 20 km y con antenas base de 30 m o más: **no se extrapola**; el alcance se marca «≥20». En llano y cima el tope es el **horizonte radio** (k = 4/3): 28 km y 123 km. Más allá solo hay difracción o propagación anómala, que este estudio no modela; en su lugar se da el **margen sobrante** en el tope.

## 5. Resultados

Los alcances son en km. «≥N» significa que la potencia ya no limita el enlace: manda el modelo o el horizonte. Las tablas completas, incluidas las de 10 dBm y las de margen, están en el anexo A.

#### Sensibilidad efectiva (dBm)

| Modo | Ancho ruido | SNR umbral (dB) | Ruido ext. 2 dB | 5 dB | 8 dB |
|---|---|---|---|---|---|
| LoRa SF12 CR4/5 BW125 | 125 kHz | -20.0 | -136.4 | -135.1 | -133.4 |
| LoRa SF9 CR4/5 BW125 | 125 kHz | -12.5 | -128.9 | -127.6 | -125.9 |
| APRS FSK 9k6 | 39 kHz | +14.0 | -107.5 | -106.2 | -104.4 |
| LR-FHSS CR1/3 (39 kHz OCW) | 488.28 Hz | +4.1 | -136.4 | -135.1 | -133.3 |
| DAPNET POCSAG 1200 (RX) | 14.6 kHz | +10.0 | -115.8 | -114.5 | -112.7 |
| D-Star GMSK 4800 | 12.5 kHz | +12.0 | -114.4 | -113.1 | -111.4 |
| CW (oido, filtro 200 Hz) | 2.5 kHz | -5.0 | -141.7 | -139.3 | -136.6 |
| WSPR | 2.5 kHz | -31.0 | -167.7 | -165.3 | -162.6 |
| FST4W-120 | 2.5 kHz | -32.8 | -169.5 | -167.1 | -164.4 |
| FT8 | 2.5 kHz | -21.0 | -157.7 | -155.3 | -152.6 |
| FT4 | 2.5 kHz | -17.5 | -154.2 | -151.8 | -149.1 |
#### Alcance (km), tracker a 20 dBm, estación omni 3 dBi (iGate, digipeater)

| Modo | Urbano | Suburbano | Campo abierto | Llano/mar | Cima |
|---|---|---|---|---|---|
| LoRa SF12 CR4/5 BW125 | 5.2 | 10 | ≥20 | ≥28 | ≥123 |
| LoRa SF9 CR4/5 BW125 | 3.2 | 6.1 | ≥20 | 23 | ≥123 |
| LR-FHSS CR1/3 (39 kHz OCW) | 5.2 | 10 | ≥20 | ≥28 | ≥123 |
| APRS FSK 9k6 | 0.8 | 1.5 | 5.2 | 6.7 | 41 |
#### Alcance (km), tracker a 10 dBm, estación omni 3 dBi (iGate, digipeater)

| Modo | Urbano | Suburbano | Campo abierto | Llano/mar | Cima |
|---|---|---|---|---|---|
| LoRa SF12 CR4/5 BW125 | 2.7 | 5.2 | 18 | 20 | ≥123 |
| LoRa SF9 CR4/5 BW125 | 1.7 | 3.2 | 11 | 13 | ≥123 |
| LR-FHSS CR1/3 (39 kHz OCW) | 2.7 | 5.2 | 18 | 20 | ≥123 |
| APRS FSK 9k6 | 0.4 | 0.8 | 2.7 | 3.8 | 13 |
#### Alcance (km), tracker a 20 dBm, receptor débil-señal con antena omni 3 dBi

| Modo | Urbano | Suburbano | Campo abierto | Llano/mar | Cima |
|---|---|---|---|---|---|
| CW (oido, filtro 200 Hz) | 6.5 | 13 | ≥20 | ≥28 | ≥123 |
| WSPR | ≥20 | ≥20 | ≥20 | ≥28 | ≥123 |
| FST4W-120 | ≥20 | ≥20 | ≥20 | ≥28 | ≥123 |
| FT8 | 18 | ≥20 | ≥20 | ≥28 | ≥123 |
| FT4 | 15 | ≥20 | ≥20 | ≥28 | ≥123 |
#### Alcance (km), tracker a 20 dBm, receptor débil-señal con yagi 12 dBi

| Modo | Urbano | Suburbano | Campo abierto | Llano/mar | Cima |
|---|---|---|---|---|---|
| CW (oido, filtro 200 Hz) | 12 | ≥20 | ≥20 | ≥28 | ≥123 |
| WSPR | ≥20 | ≥20 | ≥20 | ≥28 | ≥123 |
| FST4W-120 | ≥20 | ≥20 | ≥20 | ≥28 | ≥123 |
| FT8 | ≥20 | ≥20 | ≥20 | ≥28 | ≥123 |
| FT4 | ≥20 | ≥20 | ≥20 | ≥28 | ≥123 |
#### Margen sobrante (dB) en el tope del escenario, tracker a 20 dBm, RX omni 3 dBi

| Modo | Urbano | Suburbano | Campo abierto | Llano/mar | Cima |
|---|---|---|---|---|---|
| CW (oido, filtro 200 Hz) | -17 | -6 | +14 | +9 | +25 |
| WSPR | +9 | +20 | +40 | +35 | +51 |
| FST4W-120 | +11 | +21 | +41 | +37 | +52 |
| FT8 | -1 | +10 | +30 | +25 | +41 |
| FT4 | -5 | +6 | +26 | +22 | +37 |

### LoRa SF9 · CR 4/5 · BW 125 kHz en detalle

Es el perfil rápido del proyecto: el perfil náutico (AIS) es siempre SF9 con CR 4/5, y el perfil 2 (434,855 MHz) usa SF9 con CR 4/7. Frente a SF12:

- **Sensibilidad:** −128,9 dBm con ruido externo de 2 dB (−127,6 y −125,9 con 5 y 8 dB), es decir, **7,5 dB peor** que SF12. El alcance queda en el **61 %** del de SF12 (3,2 km frente a 5,2 km en ciudad a 20 dBm).
- **Velocidad:** el símbolo dura 4,1 ms frente a 32,8 ms; la tasa nominal es de 1758 bps frente a 293 bps, **seis veces más rápido**.
- **Ocupación del canal:** un paquete de 60 B dura 0,37 s. Enviado una vez por minuto ocupa el 0,62 % del tiempo; cada 30 s, el 1,2 %.

Alcance (km) con una estación omni de 3 dBi, tracker con antena de −2 dBi y margen de 10 dB, según la potencia configurada:

| Potencia | Urbano | Suburbano | Campo abierto | Llano/mar | Cima |
|---|---|---|---|---|---|
| 10 dBm | 1.7 | 3.2 | 11 | 13 | ≥123 |
| 14 dBm | 2.2 | 4.1 | 14 | 16 | ≥123 |
| 17 dBm | 2.6 | 5.0 | 17 | 19 | ≥123 |
| 20 dBm | 3.2 | 6.1 | ≥20 | 23 | ≥123 |
| 22 dBm | 3.6 | 7.0 | ≥20 | 26 | ≥123 |

Tiempo en el aire (cabecera explícita, CRC, preámbulo de 8 símbolos):

| Carga útil | SF9, CR 4/5 | SF9, CR 4/7 | SF12, CR 4/5 |
|---|---|---|---|
| 20 B | 0.19 s | 0.23 s | 1.32 s |
| 30 B | 0.23 s | 0.28 s | 1.65 s |
| 40 B | 0.29 s | 0.37 s | 1.97 s |
| 60 B | 0.37 s | 0.48 s | 2.63 s |
| 80 B | 0.45 s | 0.60 s | 3.28 s |
| 120 B | 0.64 s | 0.86 s | 4.60 s |

Lectura:
- **Mar y costa.** Con el tracker a 2–3 m y la estación a 30 m el horizonte radio es de 28–30 km. SF9 llega a **13 km con 10 dBm y a 23 km con 20 dBm**, es decir, cubre la mayor parte del trayecto sin necesitar SF12.
- **Tráfico denso o vehículo en movimiento.** Paquetes de 0,2–0,4 s dejan más margen para que varios trackers compartan el canal que los 1,6–2,6 s de SF12. Es el perfil adecuado cuando hay iGates cercanos y el alcance extra de SF12 no hace falta.
- **Zona abierta o montaña.** Con 20 dBm el modelo ya no limita en campo abierto (≥20 km) y en cima llega al horizonte (≥123 km): subir a SF12 aporta poco y cuesta seis veces más tiempo en el aire.
- **CR 4/5 frente a CR 4/7.** CR 4/7 añade unos 0,1 s a un paquete de 60 B y ofrece más redundancia frente a ráfagas de errores; el estudio no modela esa mejora, de modo que las cifras de alcance son las de CR 4/5.

### Enlace descendente: DAPNET y D-Star

En estos modos el tracker solo recibe: la cobertura depende de la PIRE del transmisor (1 W ≈ 30 dBm, 10 W ≈ 40 dBm).

#### Cobertura de recepción del tracker: DAPNET POCSAG 1200 (RX), según PIRE del transmisor (antena RX -2 dBi)

| PIRE | Urbano | Suburbano | Campo abierto | Llano/mar | Cima |
|---|---|---|---|---|---|
| 10 dBm (0.01 W) | 0.6 | 1.1 | 3.8 | 5.1 | 24 |
| 17 dBm (0.0501 W) | 0.9 | 1.8 | 6.0 | 7.6 | 54 |
| 30 dBm (1 W) | 2.1 | 4.1 | 14 | 16 | ≥123 |
| 40 dBm (10 W) | 4.1 | 7.9 | ≥20 | ≥28 | ≥123 |
| 50 dBm (100 W) | 7.9 | 15 | ≥20 | ≥28 | ≥123 |
#### Cobertura de recepción del tracker: D-Star GMSK 4800, según PIRE del transmisor (antena RX -2 dBi)

| PIRE | Urbano | Suburbano | Campo abierto | Llano/mar | Cima |
|---|---|---|---|---|---|
| 10 dBm (0.01 W) | 0.5 | 1.0 | 3.5 | 4.7 | 21 |
| 17 dBm (0.0501 W) | 0.8 | 1.6 | 5.5 | 7.1 | 46 |
| 30 dBm (1 W) | 2.0 | 3.8 | 13 | 15 | ≥123 |
| 40 dBm (10 W) | 3.8 | 7.2 | ≥20 | 27 | ≥123 |
| 50 dBm (100 W) | 7.2 | 14 | ≥20 | ≥28 | ≥123 |
#### Tracker → hotspot Pi-Star (antena del hotspot 0 dBi, GMSK 4800)

| Potencia TX | Urbano | Suburbano | Campo abierto | Llano/mar | Cima |
|---|---|---|---|---|---|
| 10 dBm | 0.5 | 1.0 | 3.5 | 4.7 | 21 |
| 20 dBm | 1.0 | 2.0 | 6.7 | 8.4 | 65 |

## 6. Lectura de los resultados

- **Cobertura APRS con iGates.** LoRa SF12 a 20 dBm es la opción de red: 5/10/>20 km (urbano/suburbano/abierto). Bajar a SF9 reduce el alcance a ≈60 % y acorta el paquete de 2,6 s a 0,37 s, lo que importa si hay mucho tráfico en el canal.
- **LR-FHSS.** Semtech indica un presupuesto de enlace similar al de SF12/125 kHz y una sensibilidad de pasarela 2 dB mejor que LoRa SF10/125 kHz; el estudio da la misma sensibilidad que LoRa SF12. Su ventaja no es el alcance sino la **robustez frente a colisiones e interferencia** (saltos de frecuencia con redundancia). A 10 dBm (valor por defecto) alcanza lo mismo que LoRa SF12 a 10 dBm: 2,7 / 5,2 / 18 km. Como la emisión es breve (≈2 s cada 10 min) y no hay receptor de aficionado, el modo es útil para **experimentar** con una pasarela propia, no para dar cobertura APRS.
- **FSK 9k6 frente a LoRa.** La diferencia de umbral es de unos 29 dB, es decir, un alcance 5–7 veces menor. Solo compensa con un digipeater cercano o cuando el tiempo en el aire pesa más que el alcance.
- **Señal débil.** Con 20 dBm y receptor omni en campo abierto sobran 30–40 dB (WSPR/FT8): el enlace aguanta más de lo que permite el horizonte. En ciudad, el margen es ajustado (FT8 −1 dB, FT4 −5 dB a 20 km): ahí hacen falta una yagi o una estación elevada.
- **WSPR frente a FST4W-120.** 1,8 dB de diferencia equivalen a un alcance un 13 % mayor con exponente de pérdida 3,5. Por sí solo no justifica el esfuerzo de portar el codificador; sí lo justifica querer baliza compatible con WSJT-X 3.x y mensajes de 50 bits.
- **FT8/FT4 como baliza.** El mensaje `CQ EA7JRS IM76` es el que más usuarios decodifican, pero a 70 cm hay mucha menos actividad que en HF. La utilidad es probar enlaces de tropo y con estaciones de señal débil.
- **D-Star y DPRS.** El límite es el repetidor o el hotspot. Para pruebas con un Pi-Star de baja potencia, la ventana útil es de cientos de metros a pocos kilómetros.
- **Sensibilidad del resultado a los supuestos.** Con la pendiente de Hata a 30 m (≈35 dB por década), ±3 dB cambian el alcance un ±22 %; ±10 dB lo multiplican o dividen por 2. El supuesto más influyente es el ruido externo del escenario.

## 7. Tiempo en el aire y tasa útil

| Modo | Duración por emisión | Carga |
|---|---|---|
| LoRa SF12, 125 kHz, CR 4/5 | 2,63 s (60 B), 1,65 s (30 B) | paquete APRS |
| LoRa SF9, 125 kHz, CR 4/5 | 0,37 s (60 B), 0,23 s (30 B) | paquete APRS |
| LoRa SF9, 125 kHz, CR 4/7 (perfil 2) | 0,48 s (60 B) | paquete APRS |
| LoRa SF7, 125 kHz, CR 4/5 | 0,11 s (60 B) | paquete APRS |
| LR-FHSS, CR 1/3 | ≈1,9 s (20 B según Semtech) a ≈2,2 s (mensaje `PB1 …` del firmware) | baliza de propagación |
| APRS FSK 9600 bd | ≈0,06 s (60 B + cabeceras) | paquete AX.25 |
| CW, 15 wpm | ≈19 s | `DE EA7JRS/B IM76 V4.12` |
| WSPR | 110,6 s | 50 bits |
| FST4W-120 | 109,3 s | 50 bits |
| FT8 | 12,64 s | 77 bits |
| FT4 | 5,04 s | 77 bits |
| D-Star (cabecera + 21 tramas) | ≈0,57 s | cabecera + DPRS (estimación) |

Por mensaje, los modos de señal débil ocupan el canal entre 2 veces (FT4) y 40 veces (WSPR, FST4W) más tiempo que un paquete LoRa SF12. Factor de uso con los intervalos propuestos: WSPR o FST4W-120 cada 10 min, 18 %; FT8 cada 4 ciclos (60 s), 21 %; LoRa SF12 una vez por minuto, 4,4 %; LR-FHSS cada 10 min, 0,4 %.

## 8. Límites del estudio y validación

- Los modelos de propagación son medias estadísticas; el terreno real (relieve, vegetación, edificios) puede cambiar el resultado en ±15 dB.
- La **PIRE real del tracker** no está medida. La potencia configurada es la del chip (+20 dBm), no la radiada. Medir con el SDR y el GPSDO (la salida a través de un atenuador) da el nivel conducido.
- Las sensibilidades de FSK, POCSAG y D-Star son estimaciones; el umbral real depende del demodulador y del filtro del chip. Se medirán con un generador o con un segundo tracker a atenuación conocida.
- **LR-FHSS**: las cifras de Semtech son de 868 MHz y canales de 137 y 336 kHz; el ancho de 39 kHz del proyecto no está caracterizado. Sin receptor propio no se puede verificar nada; la primera prueba propuesta en `docs/LRFHSS_BALIZA.md` (SDR con carga artificial) comprueba solo la emisión.
- La densidad de receptores (iGates LoRa, estaciones WSPR/FT8 en 70 cm, repetidores D-Star y transmisores DAPNET) **no se ha estudiado**: es tan determinante para la cobertura útil como el presupuesto de enlace.
- Pruebas propuestas: (1) alcance LoRa SF12/SF9 frente a un iGate conocido con registro GPS; (2) WSPR/FT8 con un receptor en una colina conocida; (3) hotspot Pi-Star con registro de distancia y RSSI.

## 9. Fuentes

- IARU Región 1, *VHF Handbook v10.03* (febrero de 2026): <https://www.iaru-r1.org/wp-content/uploads/2026/02/VHF_Handbook_V10_03_final.pdf>
- IARU Región 1, plan de banda UHF (2020): <https://www.irts.ie/dnloads/IARU-R1-Bandplan-UHF-2020.pdf>
- IARU Región 2, plan de bandas: <https://hamwaves.com/bandplans/doc/iaru.region2.pdf>
- IARU Región 3, R3-004 (publicado en enero de 2025): <https://iaru-r3.org/wp-content/uploads/2025/01/R3-004-Band-Plans-IARU-Region-3.pdf>
- ARRL, plan de bandas de EE. UU.: <https://www.arrl.org/band-plan>
- Semtech, *AN1200.64 LR-FHSS system performance v1.2*: <https://www.mouser.lt/pdfDocs/AN1200-64_LR-FHSS_system_performance_V1_2.pdf>
- Documentación del proyecto: `docs/LRFHSS_BALIZA.md`
- WSJT-X 3.3.0-beta5, guía de usuario (tabla 7, «Parameters of Slow Modes») y código de FST4/FT8
- UIT, Reglamento de Radiocomunicaciones, atribuciones 430–440 MHz (página IARU R1)
- Modelo de Hata (Okumura-Hata) y ruido artificial según ITU-R P.372: fórmulas estándar, **no consultadas en línea en este estudio**

## Anexo A. Tablas completas

#### Sensibilidad efectiva (dBm)

| Modo | Ancho ruido | SNR umbral (dB) | Ruido ext. 2 dB | 5 dB | 8 dB |
|---|---|---|---|---|---|
| LoRa SF12 CR4/5 BW125 | 125 kHz | -20.0 | -136.4 | -135.1 | -133.4 |
| LoRa SF9 CR4/5 BW125 | 125 kHz | -12.5 | -128.9 | -127.6 | -125.9 |
| APRS FSK 9k6 | 39 kHz | +14.0 | -107.5 | -106.2 | -104.4 |
| LR-FHSS CR1/3 (39 kHz OCW) | 488.28 Hz | +4.1 | -136.4 | -135.1 | -133.3 |
| DAPNET POCSAG 1200 (RX) | 14.6 kHz | +10.0 | -115.8 | -114.5 | -112.7 |
| D-Star GMSK 4800 | 12.5 kHz | +12.0 | -114.4 | -113.1 | -111.4 |
| CW (oido, filtro 200 Hz) | 2.5 kHz | -5.0 | -141.7 | -139.3 | -136.6 |
| WSPR | 2.5 kHz | -31.0 | -167.7 | -165.3 | -162.6 |
| FST4W-120 | 2.5 kHz | -32.8 | -169.5 | -167.1 | -164.4 |
| FT8 | 2.5 kHz | -21.0 | -157.7 | -155.3 | -152.6 |
| FT4 | 2.5 kHz | -17.5 | -154.2 | -151.8 | -149.1 |

#### Alcance (km), tracker a 10 dBm, estación omni 3 dBi (iGate, digipeater)

| Modo | Urbano | Suburbano | Campo abierto | Llano/mar | Cima |
|---|---|---|---|---|---|
| LoRa SF12 CR4/5 BW125 | 2.7 | 5.2 | 18 | 20 | ≥123 |
| LoRa SF9 CR4/5 BW125 | 1.7 | 3.2 | 11 | 13 | ≥123 |
| LR-FHSS CR1/3 (39 kHz OCW) | 2.7 | 5.2 | 18 | 20 | ≥123 |
| APRS FSK 9k6 | 0.4 | 0.8 | 2.7 | 3.8 | 13 |

#### Alcance (km), tracker a 10 dBm, receptor débil-señal con antena omni 3 dBi

| Modo | Urbano | Suburbano | Campo abierto | Llano/mar | Cima |
|---|---|---|---|---|---|
| CW (oido, filtro 200 Hz) | 3.4 | 6.8 | ≥20 | 27 | ≥123 |
| WSPR | 18 | ≥20 | ≥20 | ≥28 | ≥123 |
| FST4W-120 | ≥20 | ≥20 | ≥20 | ≥28 | ≥123 |
| FT8 | 9.6 | 19 | ≥20 | ≥28 | ≥123 |
| FT4 | 7.6 | 15 | ≥20 | ≥28 | ≥123 |

#### Alcance (km), tracker a 10 dBm, receptor débil-señal con yagi 12 dBi

| Modo | Urbano | Suburbano | Campo abierto | Llano/mar | Cima |
|---|---|---|---|---|---|
| CW (oido, filtro 200 Hz) | 6.0 | 12 | ≥20 | ≥28 | ≥123 |
| WSPR | ≥20 | ≥20 | ≥20 | ≥28 | ≥123 |
| FST4W-120 | ≥20 | ≥20 | ≥20 | ≥28 | ≥123 |
| FT8 | 17 | ≥20 | ≥20 | ≥28 | ≥123 |
| FT4 | 14 | ≥20 | ≥20 | ≥28 | ≥123 |

#### Alcance (km), tracker a 20 dBm, estación omni 3 dBi (iGate, digipeater)

| Modo | Urbano | Suburbano | Campo abierto | Llano/mar | Cima |
|---|---|---|---|---|---|
| LoRa SF12 CR4/5 BW125 | 5.2 | 10 | ≥20 | ≥28 | ≥123 |
| LoRa SF9 CR4/5 BW125 | 3.2 | 6.1 | ≥20 | 23 | ≥123 |
| LR-FHSS CR1/3 (39 kHz OCW) | 5.2 | 10 | ≥20 | ≥28 | ≥123 |
| APRS FSK 9k6 | 0.8 | 1.5 | 5.2 | 6.7 | 41 |

#### Alcance (km), tracker a 20 dBm, receptor débil-señal con antena omni 3 dBi

| Modo | Urbano | Suburbano | Campo abierto | Llano/mar | Cima |
|---|---|---|---|---|---|
| CW (oido, filtro 200 Hz) | 6.5 | 13 | ≥20 | ≥28 | ≥123 |
| WSPR | ≥20 | ≥20 | ≥20 | ≥28 | ≥123 |
| FST4W-120 | ≥20 | ≥20 | ≥20 | ≥28 | ≥123 |
| FT8 | 18 | ≥20 | ≥20 | ≥28 | ≥123 |
| FT4 | 15 | ≥20 | ≥20 | ≥28 | ≥123 |

#### Alcance (km), tracker a 20 dBm, receptor débil-señal con yagi 12 dBi

| Modo | Urbano | Suburbano | Campo abierto | Llano/mar | Cima |
|---|---|---|---|---|---|
| CW (oido, filtro 200 Hz) | 12 | ≥20 | ≥20 | ≥28 | ≥123 |
| WSPR | ≥20 | ≥20 | ≥20 | ≥28 | ≥123 |
| FST4W-120 | ≥20 | ≥20 | ≥20 | ≥28 | ≥123 |
| FT8 | ≥20 | ≥20 | ≥20 | ≥28 | ≥123 |
| FT4 | ≥20 | ≥20 | ≥20 | ≥28 | ≥123 |

#### Margen sobrante (dB) en el tope del escenario, tracker a 10 dBm, RX omni 3 dBi

| Modo | Urbano | Suburbano | Campo abierto | Llano/mar | Cima |
|---|---|---|---|---|---|
| CW (oido, filtro 200 Hz) | -27 | -16 | +4 | -1 | +15 |
| WSPR | -1 | +10 | +30 | +25 | +41 |
| FST4W-120 | +1 | +11 | +31 | +27 | +42 |
| FT8 | -11 | -0 | +20 | +15 | +31 |
| FT4 | -15 | -4 | +16 | +12 | +27 |

#### Margen sobrante (dB) en el tope del escenario, tracker a 10 dBm, RX yagi 12 dBi

| Modo | Urbano | Suburbano | Campo abierto | Llano/mar | Cima |
|---|---|---|---|---|---|
| CW (oido, filtro 200 Hz) | -18 | -7 | +13 | +8 | +24 |
| WSPR | +8 | +19 | +39 | +34 | +50 |
| FST4W-120 | +10 | +20 | +40 | +36 | +51 |
| FT8 | -2 | +9 | +29 | +24 | +40 |
| FT4 | -6 | +5 | +25 | +21 | +36 |

#### Margen sobrante (dB) en el tope del escenario, tracker a 20 dBm, RX omni 3 dBi

| Modo | Urbano | Suburbano | Campo abierto | Llano/mar | Cima |
|---|---|---|---|---|---|
| CW (oido, filtro 200 Hz) | -17 | -6 | +14 | +9 | +25 |
| WSPR | +9 | +20 | +40 | +35 | +51 |
| FST4W-120 | +11 | +21 | +41 | +37 | +52 |
| FT8 | -1 | +10 | +30 | +25 | +41 |
| FT4 | -5 | +6 | +26 | +22 | +37 |

#### Margen sobrante (dB) en el tope del escenario, tracker a 20 dBm, RX yagi 12 dBi

| Modo | Urbano | Suburbano | Campo abierto | Llano/mar | Cima |
|---|---|---|---|---|---|
| CW (oido, filtro 200 Hz) | -8 | +3 | +23 | +18 | +34 |
| WSPR | +18 | +29 | +49 | +44 | +60 |
| FST4W-120 | +20 | +30 | +50 | +46 | +61 |
| FT8 | +8 | +19 | +39 | +34 | +50 |
| FT4 | +4 | +15 | +35 | +31 | +46 |

#### Cobertura de recepción del tracker: DAPNET POCSAG 1200 (RX), según PIRE del transmisor (antena RX -2 dBi)

| PIRE | Urbano | Suburbano | Campo abierto | Llano/mar | Cima |
|---|---|---|---|---|---|
| 10 dBm (0.01 W) | 0.6 | 1.1 | 3.8 | 5.1 | 24 |
| 17 dBm (0.0501 W) | 0.9 | 1.8 | 6.0 | 7.6 | 54 |
| 30 dBm (1 W) | 2.1 | 4.1 | 14 | 16 | ≥123 |
| 40 dBm (10 W) | 4.1 | 7.9 | ≥20 | ≥28 | ≥123 |
| 50 dBm (100 W) | 7.9 | 15 | ≥20 | ≥28 | ≥123 |

#### Cobertura de recepción del tracker: D-Star GMSK 4800, según PIRE del transmisor (antena RX -2 dBi)

| PIRE | Urbano | Suburbano | Campo abierto | Llano/mar | Cima |
|---|---|---|---|---|---|
| 10 dBm (0.01 W) | 0.5 | 1.0 | 3.5 | 4.7 | 21 |
| 17 dBm (0.0501 W) | 0.8 | 1.6 | 5.5 | 7.1 | 46 |
| 30 dBm (1 W) | 2.0 | 3.8 | 13 | 15 | ≥123 |
| 40 dBm (10 W) | 3.8 | 7.2 | ≥20 | 27 | ≥123 |
| 50 dBm (100 W) | 7.2 | 14 | ≥20 | ≥28 | ≥123 |

#### Tracker → hotspot Pi-Star (antena del hotspot 0 dBi, GMSK 4800)

| Potencia TX | Urbano | Suburbano | Campo abierto | Llano/mar | Cima |
|---|---|---|---|---|---|
| 10 dBm | 0.5 | 1.0 | 3.5 | 4.7 | 21 |
| 20 dBm | 1.0 | 2.0 | 6.7 | 8.4 | 65 |


---
*© 2026 Manuel - EA7JRS — APRS LoRa+ (GPL-3.0). Base: LoRa APRS Tracker de CA2RXU.*
