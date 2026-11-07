# Instalador web (Tracker, Rotor y Gateway)

*Copyright (c) 2026 Manuel - EA7JRS.*

Dirección: **https://ea7jrs.github.io/APRS-LoRa-Plus/** (enlaces directos: `#rotor`, `#gateway`). Chrome o Edge, placa por USB (ESP Web Tools). Se elige el dispositivo, se detecta la placa (o se elige a mano) y se instala; la configuración guardada en la placa se conserva.

## Misma versión en todo el proyecto

Tracker, rotor, gateway y app llevan **el mismo número de versión** (ahora 5.3.5) y se publican juntos. Cada lanzador muestra la versión de su dispositivo y el aviso de versión nueva compara con la instalada.

## Cómo se forma el sitio

| Ruta | Qué es | De dónde sale |
|---|---|---|
| `/` (`latest.json`, `manifests/`, `firmware/`, `ota/`) | Tracker | este repositorio, release `vX.Y.Z` (`tools/make_web_assets.py`) |
| `/rotor/` | APRS LoRa+ Rotor | `web-site-rotor.zip` de la release `vX.Y.Z` de `EA7JRS/APRS-LoRa-Plus-Rotor` (su `release.yml`) |
| `/gateway/` | APRS LoRa+ Gateway | `web-site-gateway.zip` de la release `vX.Y.Z` de `EA7JRS/APRS-LoRa-Plus-I-Gateway` (pendiente de su workflow) |

El job `pages` de `build.yml` descarga esos dos `.zip` de las releases con **la misma etiqueta** y los descomprime en `rotor/` y `gateway/`. Los dos repositorios son privados: hay que crear en este repositorio el secreto **`FLASHER_TOKEN`** (token de acceso de grano fino de GitHub, solo lectura de *Contents* de esos dos repositorios). Sin el secreto, o si el otro repositorio aún no tiene esa release, el instalador dice «aún no hay versión publicada» en ese lanzador y el tracker se publica igual.

Orden para publicar una versión: release del rotor y del gateway (`vX.Y.Z`) → release del tracker (`vX.Y.Z`). Si el tracker ya estaba publicado, se vuelve a desplegar con *Actions → Build Release → Run workflow* y la etiqueta.

## Contrato de cada dispositivo

`web-site-<dispositivo>.zip` contiene, con rutas relativas:

- `latest.json`: `{"device":"rotor","version":"5.3.5","boards":{"<placa>":{"version","chip","ota","uf2"?}}}`
- `manifests/<placa>.json`: manifiesto de ESP Web Tools (rutas `../firmware/<placa>/…`)
- `firmware/<placa>/…`: imágenes (`bootloader.bin`, `partitions.bin`, `boot_app0.bin`, `firmware.bin`) o `firmware.uf2` (RP2350: el instalador ofrece la descarga y explica BOOTSEL)
- `ota/<placa>.json`: tamaño y SHA-256 (los usa la app Android)

Para que *Detectar placa* lo reconozca, el firmware contesta por USB a la línea `APRS-INFO?` con
`APRS-LoRa-Plus: device=<tracker|rotor|gateway> board=<placa> version=<X.Y.Z> chip=<chip> ota=<0|1>` (el tracker, por compatibilidad, sin `device=`).
