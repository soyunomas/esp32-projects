# Firmware precompilado

[Español](README.md) | [English](README.en.md)

Binarios para un ESP32-C3 SuperMini con flash de 4 MB. La compilación exacta
se describe en [`BUILD-INFO.txt`](BUILD-INFO.txt) y sus hashes están en
[`SHA256SUMS`](SHA256SUMS).

## Archivos y offsets

| Archivo | Offset | Uso |
|---|---:|---|
| `bootloader.bin` | `0x0` | Bootloader |
| `partition-table.bin` | `0x8000` | Tabla de particiones |
| `wifi-ap-scan-probe-c3.bin` | `0x10000` | Aplicación |
| `wifi-ap-scan-probe-c3-complete.bin` | `0x0` | Imagen completa, incluido borrado de NVS |

## Requisito

```bash
python3 -m pip install esptool
```

## Flasheo normal

Conserva el usuario web, la contraseña, los SSID elegidos y las referencias
guardadas:

```bash
PORT=/dev/ttyACM0 ./firmware/flash-prebuilt.sh
```

Si ya está dentro del directorio `firmware/`:

```bash
PORT=/dev/ttyACM0 ./flash-prebuilt.sh
```

## Instalación completa

Graba la imagen completa, borra NVS y hace que el siguiente arranque calibre
de nuevo:

```bash
PORT=/dev/ttyACM0 ./firmware/flash-factory.sh
```

Use `PORT=/dev/ttyUSB0`, `PORT=COM5` u otro nombre cuando corresponda. Puede
cambiar la velocidad con `BAUD=115200`; el valor predeterminado es `460800`.

## Verificación

```bash
cd firmware
sha256sum -c SHA256SUMS
```

## Acceso después del flasheo

- Wi-Fi: `Motion-C3-Setup`
- clave Wi-Fi: `motion-c3-setup`
- página: `http://192.168.4.1`
- usuario web inicial tras instalación completa: `admin`
- contraseña web inicial tras instalación completa: `admin`

El portal cautivo intenta abrir la página automáticamente. La web muestra el
score, el umbral adaptativo, las detecciones y el progreso de confirmación.
También permite buscar redes, elegir hasta ocho SSID y programar una
calibración con una cuenta atrás de 5 a 300 segundos.

El dispositivo observa esas redes sin asociarse a ellas. No necesita sus
claves, no tiene conexión a Internet, no incluye Telegram y se actualiza por
USB, no por OTA.
