# Modo SAT (satélites) y control de rotor

*Copyright (c) 2026 Manuel - EA7JRS. Estado: **fases 1 a 4** (predicción, rotor, recepción con Doppler, emisión con precompensación, GPredict y app), sin probar en placa.*

**Solo en las placas que lo admiten** (con pantalla y no el ESP32 original, p. ej. T-Beam Supreme S3): en las demás no aparece en el menú, ni en la app, ni en la WebUI.

## Qué hace (fase 1)

Se elige en **SELECT MODE → Satellites** (y desde la app por BLE: modo 9). Es un modo de **solo cálculo**: no emite ni procesa paquetes de radio.

- Lista de satélites con enlace de subida y/o bajada entre 420 y 450 MHz (datos de CelesTrak y SatNOGS).
- Para el satélite elegido: **azimut, elevación, distancia, altura y velocidad radial**, próximo paso (AOS, LOS, elevación máxima) y **Doppler** de cada transmisor (lo que oyes en la bajada y lo que hay que emitir en la subida para que llegue en su frecuencia). La frecuencia de subida y de bajada de un transpondedor lineal se muestra en su centro.
- Posición de la estación (*Station position* en la WebUI; la pantalla muestra la fuente arriba a la derecha): **GPS del tracker como referencia** (`GPS`; sin fix, la del móvil `PHN`), **fusión** del GPS del tracker y del móvil (`FUS`: media ponderada por la inversa de la varianza; precisión del tracker ≈ HDOP × 5 m, la del móvil la manda la app) o **solo el móvil**. La posición del móvil la envía la app por BLE con la orden `pos` (ver `BLE_CONTROL.md`) y caduca a los 2 minutos. Si no hay ninguna: última posición conocida (`LAST`) y, si no, la fija de la WebUI (`FIX`).
- Hora: UTC del GPS. Sin hora válida no se calcula nada.

Pantalla **SATELLITE** (pulsación larga → submenú): *Next / Previous satellite*, *Change view* (ahora / próximo paso / frecuencias). El archivo de satélites lo envía la app (ver más abajo).

## Categorías y favoritos

El submenú tiene *List: …* para filtrar la lista; recorre *All, Favorites, This radio, 2m, 70cm, LoRa/TinyGS, GEO/Inmarsat* y se salta las que no tienen satélites. *Favorite: ON/OFF* marca el satélite elegido (hasta 32, `sat.favorites`; también en la WebUI y con la estrella de la app). Fuera de las listas de flechas, con el **encoder de la T-TWR** se pasa al satélite siguiente / anterior de la lista.

- **Favorites**: los marcados.
- **This radio**: con algún transmisor en la banda de la radio de la placa (SX126x: 70 cm; T-TWR: la del SA868, 134–174 o 400–480 MHz).
- **2m**: subida o bajada en 144–148 MHz.

- **70cm**: satélites con subida o bajada en 420–450 MHz.
- **LoRa/TinyGS**: transmisores SatNOGS con modo LoRa o LR-FHSS en cualquier banda (433, 868, 915 MHz, 2,4 GHz...) y los LACUNASAT. Sirve para saber cuándo pasa un satélite TinyGS o de LR-FHSS (el tracker solo *emite* LR-FHSS; la recepción de estos satélites la hace tu estación TinyGS).
- **GEO/Inmarsat**: satélites geoestacionarios (periodo ≈ 1 día sideral). **Solo seguimiento**: no hay pasos, la pantalla da su azimut/elevación fijos (o «below horizon») y sirven para apuntar una antena o un rotor. Las frecuencias que se muestran (banda L: bajada 1525–1559 MHz, subida 1626,5–1660,5 MHz, y los canales STD-C 1537,10 y 1541,45 MHz) son **nominales, para sintonizar un SDR u otro equipo**; el canal concreto depende del satélite y de la región: verifícalo. El tracker no recibe ni emite en banda L.

Las frecuencias se guardan con 64 bits (la banda L y los 2,4 GHz no caben en 32).

## Datos orbitales

El archivo de satélites lo **envía la app Android** (Bluetooth) **o la WebUI** (WiFi: sección *Satellites* > *Satellite data*; el navegador lo descarga de internet, o se elige un archivo, y lo sube a `/api/satdata`). Hay dos archivos en la rama `satdata`:

- `sats.txt`: el de siempre (70 cm, LoRa, GEO; máx. 62 kB), para las placas con la partición de datos de 192 kB y para las apps y firmwares antiguos.
- `sats_all.txt`: **todas las bandas** (2 m / VHF 134–174 MHz, 70 cm / UHF 400–480 MHz, LoRa, GEO), los **grupos de CelesTrak** de cada satélite (letras: `s` estaciones espaciales, `v` los más brillantes, `a` aficionados, `c` CubeSats, `n` SatNOGS, `w` meteorológicos, `x` ciencia, `d` educación, `e` ingeniería; `l` LoRa y `g` geoestacionarios salen de la categoría), los satélites de los grupos de seguimiento (`s`, `v`, `a`, `w`) aunque no tengan transmisores, y el **CTCSS** de las subidas de los repetidores de FM conocidos (ISS, SO-50, AO-91, PO-101, IO-86). La app y la WebUI se quedan con los **grupos elegidos** (`sat.groups`; ninguno = todos) y siempre con los favoritos, primero, hasta lo que cabe en el tracker (`caps.satMax`: 64 000 bytes; en la T-TWR, con 16 MB de flash, ~900 000 bytes y hasta 3000 satélites en el índice, en PSRAM).

La app (pestaña SAT: lo descarga con el internet del móvil, o lo lee de un archivo, y lo sube por BLE con `satdata`); el tracker lo guarda en la memoria flash y ya no lleva cliente HTTP ni descarga nada por sí mismo. Lo genera cada día un workflow de GitHub (`.github/workflows/satdata.yml`, `tools/make_sat_data.py`) y lo publica en la rama `satdata` del repositorio; la dirección de descarga se cambia en la app. Los TLE envejecen: la precisión baja pasados unos días; la pantalla muestra su edad.

Formato (texto, un bloque por satélite): `S|norad|nombre|categoría|grupos`, las dos líneas del TLE y `T|subidaMin|subidaMax|bajadaMin|bajadaMax|modo|baudios|inversor|descripción|CTCSS` por transmisor (Hz; 0 = sin dato; CTCSS en décimas de Hz, opcional; si falta, se busca en la descripción: «67 Hz», «CTCSS 67.0», «PL 74.4»). Categoría U 70 cm, V 2 m / VHF, L LoRa, G geoestacionario. Los campos nuevos (grupos, CTCSS) los ignoran los lectores antiguos. El tracker lo valida y lo lee por bloques de 2 kB (el índice de un archivo de 900 kB se hace en uno o dos segundos al entrar en el modo).

**Hasta que `t-twr-plus` llegue a `main`** el workflow diario solo publica `sats.txt` (lo ejecuta GitHub desde la rama principal); mientras tanto la app y la WebUI usan ese y avisan.

## Radio: recepción con Doppler (fase 2)

En la pantalla de frecuencias (*Change view* → *v:freq*) cada transmisor lleva la clase de recepción (`CW`, `FSK`, `LoRa` o `n/a`). Con **menú → Listen: OFF** el tracker sintoniza la bajada del transmisor mostrado y la **corrige por Doppler** de forma continua (se recalcula cada medio segundo y se retoca la radio solo cuando la deriva supera una fracción del ancho de banda: 400 Hz en CW, ~2/3 de la velocidad en FSK, 1/10 del ancho de banda en LoRa, y como mucho una vez por segundo). La pantalla *RX* (cuarta vista) muestra el modo, la frecuencia recibida, el Doppler aplicado, el ajuste fino, el RSSI y el último texto; los textos largos se pasan de 40 en 40 caracteres cada 3 s. Mientras se escucha, la pantalla puede apagarse y la recepción sigue; el rotor sigue funcionando.

| Modo del transmisor | Qué hace el tracker |
|---|---|
| **CW** | FSK estrecho (4,8 kHz) y detector de envolvente por RSSI (1 kHz) con el mismo decodificador Morse que la escucha CW; el texto se agrupa en líneas del registro. LED verde y *sidetone* como en la escucha CW |
| **FSK / GFSK / GMSK** (300 a 120 000 bd; típico 9600) | Modem FSK sin sincronismo (filtro según la velocidad) y descodificador AX.25 G3RUH por software (descrambler, NRZI, HDLC, CRC y FX.25). Las tramas UI salen en formato TNC2 (`ORIGEN>DESTINO,VIA:info`); las que no son UI, en hexadecimal. Las señales con otro formato (AX.100, CCSDS, GOMspace...) no se decodifican: se ve el RSSI pero no tramas |
| **LoRa** (TinyGS y similares) | Modem LoRa con SF / BW / CR leídos de la descripción del transmisor (por defecto SF10, 125 kHz, CR 4/5); palabra de sincronismo privada (0x12), CRC activado. Menú *LoRa SF/BW* recorre combinaciones habituales. Carga útil en texto (o hexadecimal si es binaria), con SNR |
| AFSK 1200, BPSK, SSB, FM de voz, SSTV, LRPT, banda L... | **No se pueden** con un SX126x: el tracker lo avisa. La pantalla de frecuencias da la frecuencia corregida por Doppler para sintonizarlos con un SDR u otro equipo |
| LR-FHSS | El SX126x solo emite LR-FHSS: no se recibe |

Menú del modo SAT: **Listen ON/OFF**, **Next transmitter** (el siguiente que se pueda recibir; si se está escuchando, cambia en caliente), **Fine +100 Hz / -100 Hz** (error del oscilador del satélite; se suma al Doppler), **LoRa SF/BW** y **Clear RX log**. Al cambiar de satélite o de modo la escucha se detiene.

Limitaciones: solo SX1262 / SX1268 (frecuencia 400-510 MHz en esta fase); la radio es semidúplex y en esta fase no emite; el SX1268 no llega a los 401,9 MHz de algún satélite LoRa («Freq/params refused»). Con antena yagi y preamplificador externo la sensibilidad mejora, pero el tracker no controla ni alimenta el preamplificador. BLE: orden `satrx` y evento `satrx` (ver `BLE_CONTROL.md`), con cada línea recibida, para que la app la guarde o la decodifique. 
## Emisión con precompensación Doppler y secuenciador (fase 3)

**Apagada por defecto.** Se habilita en la WebUI (*Satellites 70 cm → Transmit to the satellite*). Nunca emite sola: cada emisión la pide el usuario (menú **Send message (TX)** o la orden BLE `sattx`). El mensaje (64 caracteres) se escribe en la WebUI, o lo manda la app.

| Subida del transmisor | Qué se emite |
|---|---|
| **CW** | El texto en Morse (velocidad de *CW WPM*); la frecuencia de la portadora se recalcula en cada elemento. Máximo 25 s; el botón la corta |
| **FSK / GMSK** | Trama AX.25 UI `INDICATIVO>APLRT1:texto` en G3RUH (el indicativo es el del perfil de baliza activo). El texto es el campo de información: puede ser APRS (`>estado`, `:INDICATIVO:mensaje`) |
| **LoRa** | El texto como carga útil, con SF / BW / CR de la descripción del transmisor (máx. 10 s en el aire) |

La frecuencia de emisión es la de subida del transmisor con el Doppler del instante (`f·(1 + vr/c)`, con la velocidad radial del satélite) y el ajuste de ppm global del tracker. Un transpondedor lineal se emite en el centro de su banda de subida.

**Seguridades** (todas deben cumplirse; la pantalla *TX* dice cuál falla): permiso de la WebUI; un indicativo real en el perfil de baliza (no `NOCALL`); satélite en órbita baja con hora y posición válidas y por encima de la *TX minimum elevation* (15° por defecto; también se corta una emisión larga si baja de ella); subida entre **430 y 440 MHz** (el SX1268 llega a 410–510 MHz, pero el modo SAT no emite fuera de esa banda de aficionados); intervalo mínimo entre emisiones (30 s por defecto); potencia limitada por la placa (*TX power*, 10 dBm por defecto). No se emite en AFSK, FM, BPSK, SSB ni LR-FHSS. La licencia, las normas de uso del satélite (muchos limitan la potencia, el tiempo o los modos) y la potencia radiada con la antena son responsabilidad del operador. Con yagi y amplificador externo hay que ajustar *TX power* para no pasarse.

**Secuenciador de conmutación** (WebUI): tres salidas (-1 = sin usar) con su nivel activo — *Preamp pin* (activo mientras se escucha), *PA / relay pin* y *PTT pin* (activos al emitir) — y cuatro tiempos: preamplificador cortado y PA activo → *Lead* → PTT → *Pre* → RF → *Post* → PTT y PA sueltos → *Tail* → preamplificador activo. En reposo (y al arrancar el modo SAT) el preamplificador está activo y PA y PTT sueltos. Los GPIO no mueven un relé ni un preamplificador directamente: usa un transistor o un driver. La radio es semidúplex: se para la escucha, se emite y se vuelve a escuchar.


## GPredict y la app (fase 4)

El tracker habla los dos protocolos de red de Hamlib que usa GPredict. Los dos comparten el WiFi de *Firmware update* (como KISS y CAT) y la lista de direcciones permitidas del CAT. En modo SAT solo se abren esos servidores, no KISS. **No hay autenticación: úsalo en una red de confianza.** Responde en `aprs-lora-plus.local` o por IP.

**Rotor — GPredict manda el acimut y la elevación** (WebUI → *Antenna rotor* → *Accept GPredict / rotctl on WiFi*, puerto 4533). En GPredict: *Preferencias → Interfaces → Añadir*: tipo *rotador*, host = IP del tracker, puerto 4533. En la ventana de *Antenna control* elige esa interfaz, el satélite y pulsa *Engage*. El tracker lo reenvía a su rotor (rotctld por TCP, GS-232 por Bluetooth o la app por BLE) aplicando los desfases, el montaje móvil y la compensación de inclinación; mientras GPredict manda órdenes (se vigila 15 s) la fuente del rotor pasa a *GPredict* (3) y vuelve sola a la elegida en el tracker cuando deja de mandar. El rotor tiene que estar activado en la WebUI (*Enable rotor control*) o *Send the rot event* para la app. Comandos admitidos: `P az el`, `p`, `S`, `K`, `R`, `_`, `\get_info`, `\dump_state`, `q`. Se prueba en PC con `tools/rot_net/test_rot_proto.cpp`.

**Radio — GPredict sintoniza la bajada con su Doppler** (WebUI → *Connectivity → CAT over WiFi*, puerto 4532). En GPredict: interfaz *radio*, tipo *RX only* (o *Duplex TRX* si quieres que también le diga la subida), host = IP del tracker, puerto 4532. En modo SAT, `F` (la frecuencia que GPredict ya ha corregido por Doppler) sintoniza el receptor mientras se está escuchando (menú **Listen**), y manda sobre el cálculo propio durante 15 s tras cada orden; `f` devuelve la frecuencia sintonizada (o la nominal de la bajada si no se escucha). `I` / `i` guardan y devuelven la subida de GPredict (informativa: la emisión usa su propia precompensación). `T 1` (PTT) emite el mensaje de la WebUI solo si está marcado *PTT sends a beacon* en el CAT **y** se cumplen todas las seguridades de la emisión. Rango aceptado: 400–510 MHz.

**App por BLE** (`docs/BLE_CONTROL.md`): evento `sat` (satélite elegido, azimut, elevación, distancia, velocidad radial, próximo paso, edad del TLE) cada 2 s en modo SAT; órdenes `satlist` (lista paginada), `satsel` (elegir por posición o NORAD), `sattxlist` (transmisores con su clase de recepción y emisión y la frecuencia con Doppler), `satrx` y `sattx` (fases 2 y 3), y **`satdata`**: la app descarga `sats.txt` de la rama `satdata` por Internet y se lo sube al tracker por BLE, **sin necesitar WiFi en el tracker**.

## Control de rotor

Se configura en la WebUI (*Antenna rotor*). El tracker apunta el rotor al satélite elegido desde `pre-position` segundos antes del AOS hasta el LOS, y opcionalmente lo lleva a una posición de reposo. Solo envía una orden si el objetivo cambia más que el *Step* (0,1–10°) o cada 10 s.

| Transporte | Qué habla | Notas |
|---|---|---|
| **WiFi / TCP** | cliente de `rotctld` de Hamlib (puerto 4533): `P az el` y `p` | el PC o la Raspberry con `rotctld` debe ser alcanzable desde la WiFi de *Firmware update* (p. ej. el hotspot del móvil) |
| **Bluetooth SPP** | Yaesu GS-232 (`Waaa eee`) | solo placas con Bluetooth clásico (ESP32 original) y con el Bluetooth del tracker apagado |
| **BLE → app** | evento `rot` (ver `BLE_CONTROL.md`) | la app Android debe retransmitirlo al rotor; la app aún no lo implementa |

Limitaciones: acimut de 0 a 359° (rotores de 450° sin tratamiento especial), elevación 0–90°, sin corrección de refracción. Ejemplo para probar sin rotor: `rotctld -m 1 -t 4533` (rotor «dummy» de Hamlib) y mirar `rotctl -m 2 -r IP:4533 p`.

### Estación fija o en movimiento

*Rotor mounting* en la WebUI:

- **Fixed station**: el azimut que recibe el rotor está referido al norte.
- **Mobile / vehicle**: el rotor va en un vehículo, así que el azimut que se le manda es **relativo al rumbo del vehículo** (azimut al objetivo − rumbo). El rumbo verdadero sale del curso GPS cuando se va a 5 km/h o más y, parado, del magnetómetro (con la declinación); si no hay ninguno, se mantiene el último curso 30 s y después el rotor espera («No heading»). En movimiento la posición se toma del GPS (o de la fusión con el móvil) y el objetivo se recalcula cada 0,5 s; el GPS no entra en reposo mientras el rotor móvil está activo. Los pasos de satélite se recalculan si la estación se mueve más de 0,5° (≈ 55 km). **Cabeceo y alabeo:** con *Tilt compensation* = inclinómetro externo, el tracker corrige el azimut y la elevación con el cabeceo (+ = morro arriba) y el alabeo (+ = lado derecho abajo) del soporte del rotor, que le manda la app por BLE con la orden `att` (hay que repetirla al menos cada 2 s; si caduca, el rotor sigue sin compensar). La rotación (rumbo, cabeceo, alabeo) está comprobada en PC. Está previsto para un inclinómetro de varios ejes que aún no está integrado; el IMU de la placa no se usa porque su orientación en el soporte es desconocida. No se compensa el retardo del rotor. Para que el rumbo sea fiable parado hace falta magnetómetro calibrado; sin él, el rotor móvil solo apunta bien en marcha.

### Objetivos terrestres (estaciones APRS y repetidores)

El rotor no sirve solo para satélites. En *What the rotor follows* se elige:

- **Satélite** (solo en modo SAT).
- **Estación APRS con GO TO**: el rotor sigue la estación que hayas puesto como destino de la brújula (STATIONS, LAST o GROUP → *GO TO*), con su posición actualizada en vivo cada vez que se oye.
- **Objetivo fijo**: hasta 8 en la WebUI (nombre, latitud, longitud, altitud, frecuencia) o por BLE desde la app (orden `tgt`). Sirve para un repetidor, una baliza, un nodo de la red...
- **Última estación APRS oída** (fuente 4): la estación más reciente con posición recibida por APRS LoRa o APRS FSK, sin tener que hacer *GO TO*. Filtro de indicativos (*callsigns to follow*: `EA7JRS` = cualquier SSID, `EA7JRS-9` = solo esa, `EA7*` = prefijo; vacío = todas) y antigüedad máxima (*Max. age*, 30 min por defecto). Si nadie cumple, el rotor se queda donde estaba.
- **Última baliza CW** (fuente 5): en el modo MORSE, durante *Listen CW (RF)*, el tracker busca en el texto decodificado el indicativo tras `DE` y un locator Maidenhead de 4, 6 u 8 caracteres (`VVV DE ED7YAA IM76HR`) y el rotor apunta al centro de ese cuadro (con 6 caracteres el error es de unos 2–3 km). Funciona en las placas con escucha CW por RF (SX1262 / SX1268). La baliza queda como objetivo hasta que se decodifica otra.

**Estaciones en movimiento** (fuentes *GO TO* y *última estación oída*): con *Moving APRS stations: aim where they are now* (activado por defecto) el rotor no apunta a la última posición recibida sino a la **estimada ahora**, avanzando desde ella con la velocidad y el rumbo que la estación mandó en esa posición, durante el tiempo transcurrido (como mucho *Predict up to*, 300 s por defecto; por debajo de 3 km/h se considera parada). Se recalcula cada segundo, así que el rotor acompaña a un móvil entre baliza y baliza; al llegar una posición nueva se corrige. En la pantalla ROTOR y en la app el nombre lleva `~` cuando la posición es estimada.

En todas las fuentes terrestres el acimut, la elevación y la distancia se calculan desde la **posición GPS del propio tracker** (*Satellites 70 cm → Station position*: GPS del tracker por defecto; el móvil solo entra sin fix o si se elige la fusión).

Fuera del modo SAT aparece la pantalla **ROTOR** (solo con el rotor activado y una fuente terrestre) con el rumbo, la elevación, la distancia y la posición real del rotor si el transporte la devuelve (`rotctld` sí). Rumbo y distancia sobre una Tierra esférica; la elevación incluye la diferencia de altura y la curvatura con refracción estándar (radio efectivo 4/3).

**Repetidores y RepeaterBook.** RepeaterBook Connect solo envía frecuencia, desplazamiento y tono a radios compatibles (con suscripción) y no ofrece ninguna interfaz abierta para terceros; su API exige permiso y prohíbe reutilizar los datos en otros servicios. Por eso el tracker no lleva datos de RepeaterBook: la app Android, con el acceso que tenga el usuario a ese servicio, manda al tracker solo el repetidor elegido (`tgt`). La app aún no lo hace.

**Sin probar con rotor real ni en placa.** El cálculo SGP4, Doppler y pasos sí está comprobado en PC contra las referencias de Vallado y una implementación independiente (`tools/test_sat_core.cpp`).

## T-TWR (SA868): el VFO de FM sigue al satélite

En la LilyGO T-TWR la radio es FM de 2 m o de 70 cm (un solo módulo y una sola banda), así que el modo SAT y el modo FM usan el **VFO de FM**: *Listen* (menú SAT, app o `satrx`) y *Satellite: ON* en la página FM lo ponen a seguir al transmisor elegido del satélite elegido.

- **Bajada** con Doppler (cada 0,5 s; se resintoniza al pasar 250 Hz con OpenRTX o 500 Hz con NiceRF, como mucho una vez por segundo y nunca emitiendo) más el **ajuste fino** (el encoder en la página FM o SAT: 100 Hz por paso; *Fine ±100 Hz*).
- **Subida** precompensada; se actualiza antes de cada emisión. Canal de 25 kHz (margen para el Doppler que queda).
- **CTCSS** de la subida del archivo de datos (TONE); se puede cambiar en la página FM (*Dial: tone*).
- Satélite de **banda cruzada** (SO-50: sube en 2 m y baja en 70 cm): si la bajada no cae en la banda del módulo se escucha la subida (las otras estaciones) y se puede emitir; si la subida no cae en una banda de aficionado del módulo (2 m: 144–146 MHz en la R1, 144–148 en la 2 y la 3; 70 cm: 430–440, 420–450 en la R2), solo se escucha.
- **FM de voz**: con el **PTT** lateral, solo con *Allow transmitting* (WebUI), un indicativo real y el satélite por encima de *TX minimum elevation*. En el modo SAT el PTT solo emite hacia el satélite.
- **APRS por satélite** (digipetidor AFSK 1200: **ISS 145,825 MHz**, o cualquier transmisor AFSK con la misma subida y bajada o «APRS» / «digi» en la descripción): las tramas oídas van al registro del modo SAT y a la **lista de estaciones y mensajes** (también se ven en la app). *Send position (TX)* manda la posición (GPS, última o la del modo SAT) con la ruta `sat.aprsPath` (**ARISS** por defecto; APRSAT para otros) y el comentario de la WebUI (40 caracteres como mucho: el digipetidor está muy concurrido); *Send message (TX)* manda el texto. En el modo FM con *APRS beacon* encendido, las balizas y los mensajes van por el satélite mientras está a la vista (respetando *Minimum seconds between transmissions*) y por la frecuencia APRS el resto del tiempo; en el modo SAT solo por el satélite. Sin digipeater en FM ni en SAT.
- **APT** de los NOAA (137 MHz) se oye con el módulo de VHF, pero no se decodifica.
- GPredict por CAT (`F`) manda la bajada ya corregida (15 s).

Sin probar en la placa: falta comprobar que el firmware de NiceRF acepta frecuencias fuera de la rejilla de 12,5 kHz (si no, el Doppler solo va bien con OpenRTX, en pasos de 62,5 Hz) y los cortes de audio al resintonizar.
