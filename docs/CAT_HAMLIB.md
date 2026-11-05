# CAT / Hamlib interface — APRS LoRa+ (public protocol specification)

Copyright (C) 2026 Manuel - EA7JRS. This document is released under the same terms as the firmware (GPL-3.0); anyone may implement a client or a Hamlib backend from it.

Firmware: <https://github.com/EA7JRS/APRS-LoRa-Plus> — reference implementation: `include/cat_proto.h` (protocol, plain C++, unit-tested on a PC with `tools/cat_net/test_cat_proto.cpp` and with the real Hamlib 4.6.5 `rigctl`) and `src/cat_net.cpp` (TCP server on the ESP32).

## 1. What this is (and what it is not)

APRS LoRa+ is an APRS tracker with a LoRa / FSK radio (SX126x / SX127x). It is **not** a voice or audio transceiver: it has no audio path, so programs that need audio (WSJT-X decoding, fldigi modems) cannot use it as a sound-card radio. The CAT interface exists so that programs can:

- read and (inside a window set by the owner) set the radio **frequency**,
- read the **mode** (APRS LoRa or APRS FSK 9600), the **bandwidth** and the last **RSSI**,
- read / set the **RF power**,
- use **PTT as a "send beacon" trigger** (optional, off by default): keying the radio makes the tracker transmit one APRS position beacon, never audio.

Typical users: GPredict-style Doppler/tuning programs, logging programs that follow the frequency, automation scripts (`rigctl`), and the companion Android app. It is the same interface style as a normal rigctld server, so any Hamlib program works without changes.

## 2. Transport

- TCP server, default port **4532** (the rigctld default), configurable in the WebUI (Connectivity > CAT over WiFi).
- The tracker joins the WiFi network set in Administration > Firmware update (home network or phone hotspot), the same link used by KISS over WiFi. It answers to `aprs-lora-plus.local` and publishes the mDNS service `_rigctl._tcp`.
- Up to 2 simultaneous clients. Optional list of allowed client addresses (`192.168.1.` prefix or full addresses). **There is no authentication**: use trusted networks.
- Off by default. Available on ESP32-S3 / ESP32-C3 boards, in APRS LoRa and APRS FSK modes.
- Protocol: the text protocol of Hamlib's *NET rigctl* (model 2, `rigctld`), **non-extended** (one command per line, answers are bare values or `RPRT n`). The server is tolerant of a VFO name after the command.

From a computer:

```
rigctl -m 2 -r aprs-lora-plus.local:4532 f           # frequency
rigctl -m 2 -r aprs-lora-plus.local:4532 m           # mode + passband
rigctl -m 2 -r aprs-lora-plus.local:4532 l STRENGTH  # RSSI
rigctl -m 2 -r aprs-lora-plus.local:4532 L RFPOWER 0.5
```

## 3. Commands

| Command | Short | Answer |
|---|---|---|
| `\chk_vfo` | | `0` |
| `\dump_state` | | Hamlib protocol 1 block (see 4) |
| `\get_powerstat` | | `1` |
| `\get_freq` | `f` | frequency in Hz (current one, including a CAT retune) |
| `\set_freq Hz` | `F` | `RPRT 0`, or `RPRT -9` if outside the tuning window, `RPRT -1` if not a valid number |
| `\get_mode` | `m` | `PKTFM` (APRS FSK 9600) or `PKTFMN` (APRS LoRa), then the bandwidth in Hz |
| `\set_mode mode bw` | `M` | `RPRT 0` if it is the mode already running (`PKTFM`, `FM-D` or `PKTFMN`); `RPRT -9` otherwise: the modem is chosen when the tracker starts and does not change live |
| `\get_ptt` | `t` | `0` |
| `\set_ptt n` | `T` | `0` → `RPRT 0`; `1` → sends one APRS beacon if the owner enabled "PTT sends a beacon" (`RPRT 0`), otherwise `RPRT -11` |
| `\get_level STRENGTH` | `l` | RSSI of the last packet heard, in dB relative to S9 = −93 dBm (Hamlib convention); −127 dBm until a packet is heard |
| `\get_level RFPOWER` | `l` | power as a fraction 0..1 of the chip's power range |
| `\set_level RFPOWER x` | `L` | sets the profile power (saved in flash only if it changes) |
| `\get_vfo` / `\set_vfo` | `v` / `V` | `VFOA` / `RPRT 0` (single VFO) |
| `\get_split_vfo`, `\get_split_freq`, `\get_rit`, `\get_xit` | `s` | no split / RIT / XIT |
| `\get_lock_mode` | | `0` then `RPRT 0` (Hamlib 4.6 asks it before `set_mode`) |
| `\get_info` | `_` | text identifying the device |
| `\get_vfo_info` | | frequency, mode, passband, split, satellite |
| `quit` | `q` | `RPRT 0` and the connection is closed |

Anything else answers `RPRT -4` (not implemented). Extended-protocol prefixes (`+`, `;`, `|`) answer `RPRT -4`.

## 4. `dump_state`

The tracker answers with protocol version 1 and a single frequency range, valid for receive and transmit, with the modes `PKTFM | PKTFMN`. Power in the transmit range is given in mW from the chip limits. The `rig_model=` line is `0`: the model number is assigned by Hamlib, the client uses the one it was started with.

## 5. Modes: proposal for Hamlib

Hamlib has no LoRa mode. The tracker uses the closest existing mode bits:

- APRS FSK 9600 (G3RUH) → `RIG_MODE_PKTFM` (1<<12, `PKTFM`).
- APRS LoRa → `RIG_MODE_PKTFMN` (1<<34, `PKTFMN`) — a placeholder; the intended upstream change is a dedicated `RIG_MODE_LORA`.

## 6. Tuning window and safety

By default the client **cannot change the frequency** (window 0 kHz: `F` is accepted only if it equals the current frequency). The owner can open a window of up to ±500 kHz around the profile frequency (WebUI "Tuning window"); a retune is kept in RAM only, is dropped when the profile is changed and never rewrites the saved configuration. The AIS nautical profile is fixed.

PTT never keys continuous audio or carrier: it only requests one normal APRS position beacon, through the same path as the keyboard "Send beacon". The tracker transmits on the owner's licence and callsign: the owner is responsible for the frequency, power and use. CAT has no password.

## 7. Testing

```
g++ -std=c++17 -Wall -Wextra -Werror -Iinclude tools/cat_net/test_cat_proto.cpp -o test_cat
./test_cat                       # 38 unit checks
./test_cat --serve 4532          # test server with a mock radio
rigctl -m 2 -r 127.0.0.1:4532 f  # real Hamlib client against it (CAT_TRACE=1 prints the traffic)
```

## 8. Hamlib backend

A native Hamlib backend (`aprslorap.c`, LGPL-2.1+, proposed model `RIG_MODEL_APRSLORAP`, family `dummy`) lives in `tools/hamlib_backend/` with a patch for Hamlib 4.7.2 and the submission steps; it is meant to be submitted upstream by the author. Until it is accepted, use model 2 (*NET rigctl*) as shown above.
