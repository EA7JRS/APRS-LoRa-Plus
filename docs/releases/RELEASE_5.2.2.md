# LoRa APRS Tracker V5.2.2 — Notas de versión

## Identificación de equipos (LAST APRS / TRAINER)
- Lista de tocalls actualizada desde **aprs-deviceid** (https://aprsorg.github.io/aprs-deviceid-web/): 440 tocalls, 26 sufijos Mic-E y 6 prefijos Mic-E clásicos. Antes había unas 30 entradas escritas a mano.
- Comodines del estándar: `?` cualquier carácter, `n` un dígito, `*` el resto. Gana el patrón con más caracteres exactos (APK005 → *Kenwood TH-D75*; APK0AB → *Kenwood TH-D7*).
- **Corrección Mic-E:** el sufijo de 2 caracteres va al **final** del comentario (antes se buscaba al inicio). Se reconoce también el `_` suelto (VX-8, espacio final recortado). El formato clásico usa `>`/`]` al inicio y `=`, `^`, `&` al final. El código del equipo se oculta del comentario mostrado.
- Se mantienen los nombres propios del proyecto: APLRT → *LoRa Tracker*, APLRG → *LoRa iGate*.
- Nombres abreviados a 17 caracteres (pantalla). Tocall desconocido: se muestra tal cual.
- Regenerar la lista: `python3 tools/gen_aprs_devices.py tools/tocalls.yaml include/aprs_devices.h`.

## Atribución
Lista de identificación de equipos APRS mantenida por Hessu, OH7LZB, para aprs.fi, licencia CC BY-SA 2.0 (http://creativecommons.org/licenses/by-sa/2.0/). Los datos derivados (`include/aprs_devices.h`) mantienen esa licencia.

## Verificación
Pruebas en PC de la función (APLRT1, APAT81, APK005, APAND1, sufijos Mic-E `_3`, `_)`, `(8`, `_`, `>…=`, `]`). No compilado con PlatformIO; ocupa ~17 KB de flash adicionales.
