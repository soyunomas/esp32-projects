# Prebuilt firmware

[Español](README.md) | [English](README.en.md)

Binaries for an ESP32-C3 SuperMini with 4 MB flash. The exact build is
described in [`BUILD-INFO.txt`](BUILD-INFO.txt), with hashes in
[`SHA256SUMS`](SHA256SUMS).

## Files and offsets

| File | Offset | Purpose |
|---|---:|---|
| `bootloader.bin` | `0x0` | Bootloader |
| `partition-table.bin` | `0x8000` | Partition table |
| `wifi-ap-scan-probe-c3.bin` | `0x10000` | Application |
| `wifi-ap-scan-probe-c3-complete.bin` | `0x0` | Complete image, including NVS erase |

## Requirement

```bash
python3 -m pip install esptool
```

## Normal flashing

This preserves the web account, selected SSIDs, and saved references:

```bash
PORT=/dev/ttyACM0 ./firmware/flash-prebuilt.sh
```

When already inside `firmware/`:

```bash
PORT=/dev/ttyACM0 ./flash-prebuilt.sh
```

## Factory installation

This writes the complete image, erases NVS, and forces a fresh calibration on
the next boot:

```bash
PORT=/dev/ttyACM0 ./firmware/flash-factory.sh
```

Use `PORT=/dev/ttyUSB0`, `PORT=COM5`, or another device name when applicable.
The baud rate can be overridden with `BAUD=115200`; its default is `460800`.

## Verification

```bash
cd firmware
sha256sum -c SHA256SUMS
```

## Access after flashing

- Wi-Fi: `Motion-C3-Setup`
- Wi-Fi password: `motion-c3-setup`
- page: `http://192.168.4.1`
- initial web username after a factory installation: `admin`
- initial web password after a factory installation: `admin`

The captive portal attempts to open the page automatically. The dashboard
shows the score, adaptive threshold, detections, and confirmation progress. It
also provides network search, selection of up to eight SSIDs, and delayed
empty-room calibration with a 5-to-300-second countdown.

The device observes those networks without joining them. It does not need
their passwords, has no Internet connection or Telegram integration, and is
updated over USB rather than OTA.
