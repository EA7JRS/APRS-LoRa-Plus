# Modos de funcionamiento

Tras el mensaje de bienvenida aparece **SELECT MODE**: *APRS LoRa*, *APRS FSK 9600*, *MORSE / CW*, *WSPR beacon*, *LR-FHSS beacon* (si está activada) *DAPNET pager* y *Satellites 70cm* (ver `SAT_MODE.md`); con más de 4 opciones la lista se desplaza (flechas ▲▼). Pulsación corta = siguiente, pulsación larga = aceptar; a los 8 s sin pulsar se arranca el último modo APRS usado (LoRa o FSK; MORSE y WSPR nunca se guardan ni arrancan solos). Una opción solo aparece si la radio la soporta (FSK no en LLCC68; MORSE y WSPR solo con SX1262/SX1268).

| Modo | Modulación | Páginas | Balizas |
|---|---|---|---|
| APRS LoRa | LoRa (como siempre) | 11 | automáticas |
| APRS FSK 9600 | FSK G3RUH con tramas AX.25 (el perfil AIS sigue en LoRa) | 11 | automáticas |
| WSPR beacon | portadora con 4 tonos (4-FSK) a 1,46 baudios | 1: solo la pantalla **WSPR** (sin barra de iconos) | solo manuales; baliza WSPR automática (ver `WSPR.md`) |
| MORSE / CW | LoRa en reposo; CW por portadora | 1: solo la pantalla **MORSE** (sin barra de iconos) | solo manuales |
| DAPNET pager | FSK 1200 baud (POCSAG), solo recepción | 1: solo la pantalla **DAPNET** | ninguna: no emite |

- En modo MORSE la única pantalla es **MORSE** (decodificación); macros, escucha CW por RF, manipulador, trainer y *Change mode* están en su submenú (pulsación larga). Los modos APRS no la muestran.
- En modo WSPR la única pantalla es **WSPR** (cuenta atrás, progreso de la emisión, hora y locator); ver `WSPR.md`.
- En la WebUI desaparece el selector de modulación por perfil: SF, CR y BW valen para LoRa y los valores FSK (velocidad, desviación, ancho de banda de Rx) para el modo APRS FSK.
- Para cambiar de modo sin reiniciar a mano: MAIN → pulsación larga → **Change mode** (reinicia y vuelve a preguntar).
- Si el botón no existe en la placa, se arranca el último modo guardado.

## DAPNET (receptor POCSAG)

Solo recepción de la red DAPNET (hampager.de): 439,9875 MHz, 1200 baud. No emite nada. Detalle, ajustes y limitaciones en el capítulo 26 del manual (`MANUAL_USUARIO.md`).
