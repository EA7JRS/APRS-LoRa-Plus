# Pantalla MORSE (CW)

Página **MORSE** (solo existe en el modo MORSE, ver `MODOS.md`), justo después de MESSAGES. La pantalla muestra el **texto decodificado** de lo que manipulas y de las macros enviadas (las enviadas llevan `>` delante), la letra en curso en puntos y rayas y el tipo/velocidad del manipulador (KEY, IA, IB + WPM).

## Macros (submenú)
Pulsación larga abre el submenú: **1–5** envían el mensaje CW correspondiente, **Clear text** borra la pantalla, **Back** cierra. Se envía una portadora continua del SX1262/SX1268 manipulada todo/nada (A1A) en la frecuencia y potencia del perfil LoRa activo; con un receptor en CW/SSB se oye el tono. El botón aborta. El resto del Tracker queda en pausa mientras dura el mensaje.

## Manipulador en jack de 3,5 mm
Define los pines en la WebUI (LoRa → CW / Morse): punta = *Key / dit GPIO*, anillo = *Dah GPIO*, malla = GND. Los pines son entradas con pull‑up (contacto a masa); usa GPIO libres de tu placa (−1 = sin manipulador).
- **Straight key** (vertical): solo punta. Decodificador adaptativo (el WPM configurado debe estar cerca del tuyo).
- **Iambic A / B**: punta = punto, anillo = raya, con memoria de paletas; *Swap paddles* para zurdos. El keyer usa el WPM configurado.
- *Key the RF carrier*: el manipulador emite la portadora RF en directo. *Sidetone*: tono de 700 Hz en el zumbador (necesita el zumbador activado).
- Con la página MORSE abierta, al pulsar el manipulador comienza una sesión de manipulación que termina a los 2,5 s sin actividad (o con el botón); durante ella el bucle principal queda en pausa y la pantalla solo se redibuja con el manipulador en reposo.

## Escucha CW por RF (decodificación de señales recibidas)
Submenú → **Listen CW (RF)**. El chip pasa a FSK de banda estrecha (4,8 kHz) en la frecuencia del perfil activo y se lee el **RSSI instantáneo** a ~1 kHz. Un detector de envolvente con umbral adaptativo (suelo de ruido, pico y histéresis) convierte el nivel en pulsos, y el decodificador adaptativo los pasa a texto en la pantalla MORSE (cabecera `RX <wpm>`, y abajo el nivel en dBm y la relación señal/ruido). El botón sale y restaura el modem del perfil.
- Pensado para portadora manipulada (A1A) dentro de ±2 kHz de la frecuencia del perfil. No es un demodulador de audio: no sustituye a un receptor de CW con filtro de audio.
- Hace falta al menos ~6 dB entre portadora y ruido; con señales débiles o QSB fuerte decodifica mal.
- Mientras escucha, el Tracker queda en pausa (sin recepción LoRa APRS ni balizas), como en la manipulación.

## Baliza CW autónoma
En la WebUI: *CW beacon every (min)* (0 = desactivada, máx. 240) y *CW beacon text* con `{CALL}` (indicativo sin SSID), `{LOC}` (locator de 6 caracteres, vacío si no hay fix GPS) y `{VBAT}` (voltios de batería con 1 decimal, vacío si no hay batería). Ejemplo por defecto: `DE {CALL}/B {LOC} V{VBAT}` → `DE EA7JRS/B IM77AJ V4.1`. En modo MORSE, si el intervalo es > 0 la baliza se activa sola y emite a los 10 s del arranque y después cada intervalo, en la frecuencia y potencia del perfil activo, a la velocidad WPM general. El submenú tiene **CW beacon: ON/OFF** para activarla o pararla a mano (sin intervalo configurado usa 10 min). El botón aborta la emisión en curso; la siguiente sale en su hora. Mientras emite, el Tracker está en pausa. No hay sueño profundo entre balizas: la placa permanece despierta en modo MORSE (el consumo es el del Tracker en reposo).

## Trainer (aprendizaje, método Koch)
Dos modos en el submenú. No emiten RF: usan el zumbador y el manipulador. En la WebUI se ajustan el **nivel Koch** (2–41, orden LCWO: K M R S U A P T L O W I . N J E F 0 Y , V G 5 / Q 9 Z H 3 8 B ? 4 2 7 C 1 D 6 X) y la **velocidad de espacios Farnsworth** (0 = sin Farnsworth); la velocidad de los caracteres es el WPM general.
- **Trainer: listen** (necesita el zumbador activado): suena un grupo de 5 caracteres aleatorios del nivel, la pantalla muestra `?????`, a los 1,5 s se revela el grupo y a los 3 s empieza el siguiente. Una pulsación del botón sale y muestra cuántos grupos has hecho.
- **Trainer: keying** (necesita el GPIO del manipulador): se muestra un carácter grande y su código; lo tecleas, el decodificador lo lee y se indica OK o lo que tecleaste. Arriba a la derecha, aciertos/intentos. Una pulsación del botón sale y muestra la puntuación.

## Límites
- Solo SX1262/SX1268 para emitir (la T‑Beam S3 Supreme lo es). Sin SX126x se puede decodificar y ver el sidetone, pero no emitir.
- Temporización PARIS, keyer y decodificador comprobados en PC; sin probar con un manipulador real.
- Solo existe en el modo MORSE, que tiene esta única pantalla (sin barra de iconos).

## Pantalla CW BEACON (5.2.11)
En modo MORSE hay una segunda pantalla dedicada a la baliza (indicativo, WPM, potencia, frecuencia e intervalo, texto expandido, estado/cuenta atrás y contador de enviadas). Submenú: Beacon ON/OFF, Send now, Change mode, Back.
