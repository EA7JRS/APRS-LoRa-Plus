# APRS LoRa+ 5.4.4

Tracker, app Android, rotor, Display, plotter náutico (con el instrumento de viento) y pasarela NMEA 2000 con el mismo
número de versión. El gateway sigue en espera.

**Tracker**
- **Pantallas a color** (Heltec Wireless Tracker, T-Deck y T-Deck Plus): páginas de 160 × 80 (26 columnas; la pantalla
  entera en la Heltec, ×2 y centradas en la T-Deck) con los colores de la app; **tema claro, oscuro o automático** (por el
  sol en la posición del GPS: WebUI *Display > Colour theme*, SYSTEM > *Theme*, orden `act sys_theme`); **bienvenida** con
  el logo de la app a todo color; **iconos APRS a color** en LAST (con las superposiciones) y en STATIONS.
- **GPS > GNSS** (todas las placas): satélites por constelación y por banda (L1 / L5 / L2, NMEA 4.10+ como el UC6580),
  PPS, PDOP, error y el cielo. El evento BLE `sky` lleva las bandas de cada satélite.
- **SYSTEM > BATTERY**: carga con la curva de una celda Li-ion, estado (también sin PMU, por la tendencia de la tensión),
  tiempo que queda o hasta llenarse; **ahorro automático** por debajo de `battery.saveBelow` (15 %). Evento `sensors` con
  `bst`, `bLeftH`, `bPctH` y `bSave`.
- **Heltec Wireless Tracker:** GNSS UC6580 configurado al encenderlo (todas las constelaciones, L1 / L5), reset del GNSS
  si deja de mandar datos, PPS para la hora de WSPR / FT8, pines de aviso propios (LED de la placa en GPIO 18) y pines
  retenidos en el apagado. `docs/HELTEC_WIRELESS_TRACKER.md`.
- **T-TWR Plus Rev2.1: audio de FM más limpio** por el altavoz (sin los cortes de ~130 Hz por el desajuste de ritmos del
  ADC y el altavoz, banda de voz de 300 a 3000 Hz y limitador suave).
- **Identificador APRS propio** `APZLRT` (en lugar del de CA2RXU); pedido `APLJR?` al registro.
- **Batería en las placas sin PMU:** porcentaje por la curva Li-ion (antes, recta entre 3,0 y 4,2 V). Con la AXP2101 ya no
  se manda como corriente lo que era el porcentaje (`battMa`).
- CI: ELF con símbolos de cada placa como artefacto y acción *Decode addresses* para traducir el `Saved PC` de un reinicio.

**App Android** (versionCode 50404)
- DISP > GPS: satélites del tracker por banda (L1 / L5). Sensors: estado de la batería, tiempo que queda y ahorro.
- Tracker > GNSS: estado del PPS.
- RepeaterBook Connect (con la app de RepeaterBook, sin token) y mensajes de error de su API.

**Display**
- Reloj OpenHamClock en la 7B (entorno `waveshare_lcd7b_hamclock`, en la release y el instalador web).

**Rotor, plotter náutico e instrumento de viento, pasarela NMEA 2000**
- Solo el número de versión.

Nada de esta versión se ha probado todavía en las placas: las pantallas a color, las páginas GNSS / BATTERY y el audio de
FM de la T-TWR solo se han compilado (todas las placas) y probado en el PC.
