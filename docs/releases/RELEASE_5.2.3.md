# LoRa APRS Tracker V5.2.3 — Notas de versión

## TELEMETRY
- **Sin BME680 se oculta la línea de gas/IAQ**; el resto de filas suben y dejan sitio a las nuevas.
- Matriz de decisión del texto adjunto (presión, cambio en 3 h, T−Td): *Severe storm*, *Sudden change*, *Fair, anticyc.*, *Passing showers*. Si no encaja, se usan las reglas anteriores.
- **Base de nubes** (Hennig, (T−Td)·125 m) y tipo de aire (saturado / cúmulos / moderado / seco).
- **Helada nocturna:** `Tmin = T18 − 0,55·(T18 − Td18) − 2`; usa la muestra de las 18:00 locales (necesita hora válida y el tracker encendido a esa hora) y avisa `FROST risk` si es ≤ 0 °C durante 16 h.
- Aviso `Fog/rain likely 80%` con presión en descenso y base de nubes < 600 m.
- **No incluido:** el índice Z de Zambretti. El texto da las fórmulas pero no la tabla de condiciones, y asignarlas sería inventarlas. Si aportas la tabla se añade.
- Punto de rocío: ya se calculaba con Magnus (constantes 17,62/243,12; el texto usa 17,625/243,04, diferencia < 0,05 °C).

## Verificación
Lógica probada en PC con casos de ejemplo. Sin compilar con PlatformIO ni probar en placa (los sensores BME no están en el simulador).
