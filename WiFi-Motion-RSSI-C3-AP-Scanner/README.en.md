# WiFi Motion RSSI C3 AP Scanner

[Español](README.md) | [English](README.en.md)

Experimental motion-compatible change detector for an ESP32-C3 SuperMini. It
observes the RSSI of nearby access points without joining them, creates its own
local Wi-Fi network, and displays the result on a web page.

It does not require the observed network passwords, a camera, a microphone, or
an external sensor.

## Status

Functional prototype compiled, tested, and flashed on an ESP32-C3 SuperMini:

- asynchronous passive Wi-Fi scanning;
- automatic or manual selection of up to eight SSIDs;
- internal tracking by BSSID;
- initial calibration and delayed recalibration from the web UI;
- multi-reference detector with an adaptive threshold;
- temporal chart, coverage, and confirmation progress;
- captive portal and permanent local access point;
- configuration and reference persistence in NVS;
- JSON Lines telemetry over USB.

This is an experimental propagation-change detector, not a certified static
presence sensor. A stationary person may produce no measurable change, while
other environmental changes may affect RSSI.

## How it works

During calibration, the device scans every channel, identifies stable BSSIDs,
and stores their median RSSI and MAD. It then scans only the required channels
and combines the normalized deviations of the visible references.

The threshold is calculated from a fixed window of 32 quiet scores using the
median and MAD. It never falls below the compiled minimum (`2.50`), freezes
during a possible trigger and during `MOTION`; the default profile requires
one high scan. Insufficient coverage is reported as
`DEGRADED` or `NO_DATA`, not as motion.

## Quick start with the included firmware

Ready-to-flash binaries are stored in [`firmware/`](firmware/). They require an
ESP32-C3 with 4 MB flash and `esptool`:

```bash
python3 -m pip install esptool
```

Normal flashing preserves saved settings and references:

```bash
PORT=/dev/ttyACM0 ./firmware/flash-prebuilt.sh
```

Factory installation erases NVS and forces a fresh calibration:

```bash
PORT=/dev/ttyACM0 ./firmware/flash-factory.sh
```

Replace `/dev/ttyACM0` when the serial port has a different name, such as
`/dev/ttyUSB0` on Linux or `COM5` on Windows. Individual files, offsets, and
hash verification are documented in
[`firmware/README.en.md`](firmware/README.en.md).

## Connecting to the dashboard

The device keeps this network available while detecting:

- Wi-Fi: `Motion-C3-Setup`
- Wi-Fi password: `motion-c3-setup`
- fallback page: `http://192.168.4.1`
- initial web username: `admin`
- initial web password: `admin`

The captive portal attempts to open the login form automatically. If it does
not, open `http://192.168.4.1`. The web username and password can be changed
under Configuration.

The dashboard provides:

- a large calibrating, idle, motion, or insufficient-coverage state;
- a two-minute chart with score, live adaptive threshold, and detections;
- coverage and observed-reference counts;
- confirmation progress when two or three readings are selected;
- a table with the latest 128 detections and JSON/CSV downloads;
- network search with **+ Add** and **Remove** controls.

English is the default interface. The **English** and **Español** links switch
language without changing the detector configuration.

Because the device cannot obtain Internet time, the **Date and time** section
offers the phone or computer's local time after each boot. Confirm it to date
the detections. The clock and history live in RAM: they remain available while
the ESP32 is powered and are cleared by a restart. Setting the clock after an
event also dates earlier detections from the same session.

Configuration exposes only simple profiles for sensitivity, confirmation
(1, 2, or 3 readings), detection speed, motion-state duration (2, 4, or 8
seconds), and calibration (15, 25, or 40 scans). Defaults are one confirmation
reading and 25 calibration scans. **Save configuration** preserves existing
references, **Save and recalibrate** replaces them, and **Restore detection
settings** preserves credentials, selected networks, and references.

Automatic mode selects the most stable BSSIDs. Manual mode allows up to eight
SSIDs to be added from the search results. The ESP32 never joins those networks
and never asks for their passwords.

## Calibrating an empty environment

Under **Empty-room calibration**:

1. choose a delay from 5 to 300 seconds;
2. press **Leave and calibrate**;
3. leave the monitored area during the countdown;
4. stay away while calibration progresses (25 scans by default);
5. return after the dashboard reports **No motion**.

When calibration starts, previous references are deleted, all channels are
scanned again, and a new baseline is created. The previous detector remains
active during the countdown.

The 15-scan fast profile finishes sooner but may select less stable references;
the normal profile uses 25 scans and the precise profile uses 40. This choice
applies to initial and portal-requested calibration.

## Building and testing

The exact ESP-IDF revision is recorded in
[`firmware/BUILD-INFO.txt`](firmware/BUILD-INFO.txt).

```bash
source /path/to/esp-idf/export.sh
./tools/build.sh
```

The script:

1. builds and runs the host tests;
2. builds the ESP32-C3 firmware;
3. generates the application and complete flash images;
4. refreshes `firmware/BUILD-INFO.txt` and `firmware/SHA256SUMS`.

The project can also be flashed directly through ESP-IDF:

```bash
idf.py -p /dev/ttyACM0 flash
```

## Capture and diagnostics

```bash
python3 tools/capture_jsonl.py \
  --port /dev/ttyACM0 \
  --output captures/session-01.jsonl \
  --duration 300
```

A short BOOT press toggles the experimental event marker. Holding BOOT for
three seconds requests the portal again, although this release normally keeps
the local network active. The GPIO8 onboard LED is off in `IDLE`, on in
`MOTION`, and blinks during warm-up or insufficient coverage.

USB output uses the `wifi_ap_scan/v1` schema and includes `boot`, `ap`, `scan`,
`calibration`, `reference`, `detector`, `motion_event`, and explicit error
records.

## Technical documentation

The detailed study documents are currently written in Spanish:

- [Feasibility study](ESTUDIO_VIABILIDAD.md)
- [Phased plan](PLAN_POR_FASES.md)
- [Capture protocol](PROTOCOLO_CAPTURA.md)
- [Phase 1 results](RESULTADOS_FASE1.md)

## Not included

- It does not join the reference access points.
- It does not need or store their Wi-Fi passwords.
- It does not send Telegram alerts because it operates without Internet
  connectivity.
- It has no OTA updater; install updates over USB from `firmware/`.
- It cannot guarantee detection of a completely stationary person.
