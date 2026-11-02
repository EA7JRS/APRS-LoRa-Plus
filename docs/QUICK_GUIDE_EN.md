# LoRa APRS Tracker V5.2.1 — Quick Guide

**Board:** T-Beam S3 Supreme (128×64 OLED) · **Version:** 5.2.1 (base 4.0.1 + APRS screens + FSK + MORSE + WSPR) · **Screens author:** EA7JRS

## 0. At power-on: choose a mode

Welcome → **SELECT MODE**. **1P** = next, **LP** = accept; after 8 s the last APRS mode starts.

| Mode | What it is |
|---|---|
| **APRS LoRa** | The usual Tracker (11 pages) |
| **APRS FSK 9600** | AX.25 G3RUH at 9600 baud (11 pages; not heard by 1200 baud equipment) |
| **MORSE** | A single MORSE screen; never saved and never starts by itself |
| **WSPR beacon** | GPS-timed WSPR beacon (single WSPR screen); never saved |

To change mode: **MAIN → LP → Change mode** (in MORSE: LP → *Change mode*).

## 1. One button

| Gesture | On the normal screen | With a submenu open |
|---|---|---|
| **1P** (short press) | Next page | Next option |
| **LP** (long press) | Opens the page submenu | Runs the highlighted option |
| **2P** (double press) | Classic configuration menu | Closes the submenu |
| **3 or 4 presses** | WiFi AP menu (as always) | — |
| **5 short or ··· −−− ···** | **SOS beacon** (see §4) | — |

In the classic menu, 2P returns to the screens. The icon bar at the bottom appears when you change page and hides after 3 s.

## 2. The 11 pages (APRS modes, in this order)

| Page | What it shows | Submenu (LP) |
|---|---|---|
| **MAIN** | Callsign, countdown to next beacon, UTC time, position, locator, last Rx, unread messages | Send beacon · Beacon mode · Next profile · Send SOS · Display off · Config menu · Change mode |
| **MESSAGES** | Received and sent messages with status (acknowledged, unacknowledged or NO ACK) | Read / reply · Browse list · Mark all read · Delete all · (in a message) Reply · Quick reply · Delete |
| **LAST APRS** | Last station heard: APRS icon, type, distance, bearing, S-meter (`S 8`, `S 9+10`), comment | Next view · Older · Newer · **GO TO** |
| **STATIONS** | List of up to 20 stations with distance and age | Browse · Sort · Filter · Clear list |
| **GPS** | Coordinates (DD, 6 decimals), locator, altitude, satellites, HDOP, heading | GPS on/off · Coords · Units · SBAS · Constellations |
| **COMPASS** | Compass rose and arrow to a station (GO TO) | Target · Next station · Heading · North · Declination · Reset align |
| **ALTIMETER** | Altitude, vertical speed (0 when stopped), QNH and altitude profile | Span · Reset profile · QNH |
| **TELEMETRY** | Temperature, dew point, humidity, pressure + trend, gas, **forecast** | — |
| **CLOCK** | Local time, date, sunrise and sunset | Zone · Format · Sync from GPS |
| **LORA** | Frequency, SF, BW, CR, power, counters, RSSI, S-meter, time on air | Next profile · Power · Reset counters · Heard list |
| **SYSTEM** | Heap, PSRAM, Flash, battery, uptime, Bluetooth and WiFi | — |

## 3. Handy things

- **GO TO:** from LAST APRS, STATIONS (Browse mode) or an alarm: opens COMPASS pointing at that station. Beeps when you get within 50 m.
- **Quick reply** (in message detail): QSL, 73, TNX, WAIT, CALL ME, QSY CQ VHF, QSY CQ UHF, QSY RPT LOCAL. Sent requesting acknowledgement.
- **Beacon mode** (letter at the top right of MAIN): **A** automatic, **S** SmartBeaconing, **M** manual (only what you send with *Send beacon* goes out).
- **LoRa power:** LORA → LP → Power → +1 / −1 dBm. Saved and kept after reboot.
- **S-meter (VHF/UHF):** S9 = −93 dBm, 6 dB per point. Row `S8 -99 SNR 5.0` in LORA.

## 4. Emergencies

**If you receive an emergency** (Mic-E *Emergency*, or a message/comment with SOS, EMERGENCY, MAYDAY, MOB or MAN OVERBOARD): the screen turns on, the siren sounds and the LED blinks fast. Alarm screen with callsign, distance and bearing.

| Press | Effect |
|---|---|
| **1P** | Silences (another 1P closes the screen) |
| **LP** | **GO TO**: opens COMPASS towards the station |
| **2P** | Silences and closes |

**If the emergency is you:** 5 short presses or SOS in Morse (short = under 0.3 s; long = 0.3–0.7 s). 5 s countdown with beeps (**any press cancels it**). Then a Mic-E *Emergency* and a beacon with the text "EMERGENCY SOS" are sent, **every 30 s until cancelled**. Cancel by repeating the gesture or MAIN → LP → *Cancel SOS*. While it lasts, "SOS" blinks in the header and the GPS does not sleep.

## 5. MORSE mode

Single screen with the decoded text. **LP** opens the submenu:

| Option | What it does |
|---|---|
| **1–5** | Sends the CW macro (editable in the WebUI, `{CALL}` = your callsign) |
| **Listen CW (RF)** | Decodes CW received over RF (carrier within ±2 kHz, ≥ 6 dB above noise) |
| **Trainer: listen** | 5 Koch characters sound; you identify them by ear and they are revealed at 1.5 s |
| **Trainer: keying** | Key the shown character with your key; counts hits |
| **CW beacon: ON/OFF** | CW beacon every N min (WebUI): `DE {CALL}/B {LOC} V{VBAT}` |
| **Clear text / Change mode** | Clears the screen / returns to the selector |

- The **button** aborts any send, listen or trainer.
- **Key:** 3.5 mm jack; tip = dit, ring = dah, sleeve = GND. Pins, type (straight, iambic A/B), WPM, sidetone and trainer level in the **WebUI** (LoRa → CW / Morse).
- During a send or listen the Tracker is paused (no beacons or APRS reception).
- Transmitting needs SX1262/SX1268. Sidetone and listen trainer need the buzzer enabled.

## 6. WSPR beacon mode

Single screen: callsign, GPS locator, frequency, UTC time and a **countdown** to the next transmission (110.6 s, starts 1 s after the even minute). **LP**: *Beacon ON/OFF*, **Tune carrier**, *Change mode*. The **button** aborts a transmission and leaves the beacon OFF.

- **WebUI:** centre frequency (70 cm: dial 432.300 MHz USB = `432301500` Hz; 433.600 is **not** WSPR), power (0–20 dBm, default 10), interval, optional fixed locator, offset.
- **Calibrate:** *Tune carrier* → measure with an SDR → `offset = nominal − measured`. ±100 Hz (±0.23 ppm) is required: the TCXO alone is not enough.
- GPS time is required. Use a 70 cm low-pass filter before the antenna. Amateur bands only.

## 7. If something is wrong

| Symptom | What to check |
|---|---|
| "WAITING FOR GPS" | Open sky; in GPS → LP → *GPS on/off*. The position in MAIN comes from the GPS. |
| TELEMETRY says "Sensor: none" | Enable *Telemetry* in the web configuration and connect a BME280/BMP280/BME680. |
| Compass shows no heading | Keep the board flat; when stationary it uses the magnetometer, when moving the GPS course. |
| No alarm sounds | Check in the configuration that the buzzer and message LED are enabled. |
| Screen off | Any press turns it on. |
| WSPR: "NO GPS TIME" | Open sky until GPS time is available. |
| WSPR: nobody decodes me | Calibrate the frequency (offset) and check dial, filter and antenna. |
| MORSE / FSK missing in the selector | Only offered with SX1262/SX1268 (MORSE) and without LLCC68 (FSK). |
| "Trainer needs buzzer" | Enable the buzzer in the configuration. |
| "Set key GPIO (WebUI)" | Define the key pin in the WebUI. |
