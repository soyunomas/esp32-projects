# Prebuilt firmware

[Main README](../README.md) | [Instrucciones en español](../README-ES.md)

These binaries target an **ESP32-C3 SuperMini with 4 MB flash** and were built
from the source in this repository with ESP-IDF commit
`12f36a021f511cd4de41d3fffff146c5336ac1e7`
(`v6.1-dev-2938-g12f36a021f`).

## Files

| File | Offset | Purpose |
|---|---:|---|
| `bootloader.bin` | `0x0` | ESP32-C3 second-stage bootloader |
| `partition-table.bin` | `0x8000` | 4 MB project partition table |
| `wifi-motion-rssi-c3.bin` | `0x10000` | Main application |
| `wifi-motion-rssi-c3-complete.bin` | `0x0` | Combined factory image |
| `SHA256SUMS` | — | Integrity hashes |

## Install esptool

```bash
python3 -m pip install --user esptool
```

On Windows, use `py -m pip install esptool`.

## Update firmware while preserving settings

The recommended script writes only the bootloader, partition table, and
application. It does **not** write the NVS partition at `0x9000`, so saved Wi-Fi,
administrator, detector, and Telegram settings remain intact.

```bash
PORT=/dev/ttyACM0 ./firmware/flash-prebuilt.sh
```

Typical ports:

- Linux: `/dev/ttyACM0` or `/dev/ttyUSB0`
- macOS: `/dev/cu.usbmodem...`
- Windows: `COM5` or another COM number shown by Device Manager

Equivalent manual command:

```bash
python3 -m esptool --chip esp32c3 --port /dev/ttyACM0 --baud 460800 \
  --before default-reset --after hard-reset write-flash \
  --flash-mode dio --flash-freq 80m --flash-size 4MB \
  0x0 firmware/bootloader.bin \
  0x8000 firmware/partition-table.bin \
  0x10000 firmware/wifi-motion-rssi-c3.bin
```

## Clean factory installation

The combined image contains `0xFF` across the NVS address range. Flashing it at
`0x0` therefore **clears saved Wi-Fi, administrator, detector, and Telegram
configuration**.

```bash
PORT=/dev/ttyACM0 ./firmware/flash-factory.sh
```

Windows PowerShell equivalent:

```powershell
py -m esptool --chip esp32c3 --port COM5 --baud 460800 `
  --before default-reset --after hard-reset write-flash `
  --flash-mode dio --flash-freq 80m --flash-size 4MB `
  0x0 firmware/wifi-motion-rssi-c3-complete.bin
```

After a clean installation, connect to `WiFi-Motion-XXXXXX` with password
`motion-setup`, open `http://192.168.4.1`, and sign in with `admin` / `admin`.

## Verify downloads

From the repository root:

```bash
cd firmware
sha256sum -c SHA256SUMS
```

## If flashing cannot connect

1. Confirm the USB cable supports data.
2. Close serial monitors using the same port.
3. Hold BOOT, tap RESET, release RESET, then release BOOT.
4. Run the flash command again.
5. If necessary, reduce `BAUD`, for example `BAUD=115200`.

