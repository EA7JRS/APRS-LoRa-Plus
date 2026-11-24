# APRS LoRa+ tracker 5.4.9

- **Estado propio una vez.** Con el campo *Status* del perfil vacío, el tracker manda una sola vez por versión e
  indicativo el estado «APRS LoRa+ 5.4.9», unos 10 minutos después de arrancar. Las webs como aprs.fi conservan para
  siempre el último estado recibido: en estaciones que venían de otro firmware seguía saliendo el enlace a aquel proyecto.
  Con un *Status* propio se manda ese y no este.
- **Modo de arranque por USB:** `MODE LORA` o `MODE FSK` por el puerto serie (115200) deja ese modo APRS como el de
  arranque y reinicia, sin depender del botón ni de la app. El menú de modos apunta en el registro lo que vio.
- **Registro limpio** en las T-Beam v0.7 / v1.x y la Hydra: ya no sale «gpio_pullup_en: GPIO number error (input-only pad
  has no internal PU)» por el botón. No afectaba al funcionamiento.

Compila en la CI para todas las placas. **Sin probar todavía en las placas.**
