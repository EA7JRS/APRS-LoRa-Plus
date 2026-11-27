# APRS LoRa+ tracker 5.5.1

Versión del tracker alineada con la app Android 5.5.1 (el resto del ecosistema sigue en la 5.5.0). Dos cambios.

## SSDV: descarga de la imagen recibida por BLE

- Orden nueva **`ssdvrx`**: el tracker guarda una copia (del tamaño justo) de la **última imagen SSDV recibida completa**
  y la sirve por BLE a trozos, para que la app Android la **vea, guarde y comparta** (WhatsApp, Telegram…).
  - `s=info` → evento `{"e":"ssdvrx","have","len"}` (si hay imagen y su tamaño en bytes).
  - `s=get, off=<desplazamiento>` → evento `{"e":"ssdvrx","off","d" (base64, ≤ 135 B),"last"}`; la app repite con el
    siguiente `off` hasta `last`.
- Añadido `base64Encode` en `ble_control.cpp` y `SSDV_Utils::rxImageLen` / `rxImageRead` (con sus stubs sin `HAS_SSDV`).
  No transmite. Disponible en las placas con modo SSDV (`caps.ssdv`).

El envío de SSDV desde la app (foto / galería / tarjeta QSL) y la recepción por el RTL-SDR del móvil ya funcionaban con
el firmware 5.5.0; esta versión solo añade la descarga a la app de las imágenes que **recibe el tracker**.

## HF: MUF medida por ionosonda

- La app Android baja la ionosonda GIRO más cercana (servicio KC2G) y manda al tracker la **foF2 y la MUF(3000) medidas**
  en la orden **`spacewx`**, con los campos nuevos **`fo`** (foF2 en MHz) y **`mu`** (MUF 3000 en MHz).
- `ALERTS` las guarda (`spaceMuf` / `spaceFof2`, frescas menos de 2 h) y la página **TELEMETRY > HF** usa esa **MUF real**
  en vez de la estimada a partir del flujo solar; así la frecuencia máxima utilizable y el estado de las bandas son más
  fiables cuando hay una ionosonda cerca. El nombre de la ionosonda no se guarda ni se muestra.
