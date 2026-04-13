# ESP32 Projects

Colección de proyectos prácticos con ESP32, ESP32-C3, ESP32-S3, ESP32-C5 y variantes. Cada carpeta contiene un proyecto independiente con su propio README, código fuente, binarios precompilados e instrucciones.

## Proyectos

| Proyecto | Chip | Descripción |
|---|---|---|
| [WiFi-Repeater](WiFi-Repeater/) | ESP32-C3-SuperMini | Repetidor WiFi con interfaz web profesional, NAPT, captive portal, OTA, auth y factory reset |
| [Smartbutton-esp32-c5](Smartbutton-esp32-c5/) | ESP32-C5 | Botón IoT dual configurable — HTTP/MQTT, deep sleep, captive portal, LED RGB, OTA |
| [Smartbutton-esp32-c5-ws](Smartbutton-esp32-c5-ws/) | ESP32-C5 | Botón IoT dual configurable — HTTP/MQTT/**WebSockets**, deep sleep, captive portal, LED RGB, OTA |
| [Smartbutton-esp32-C3-Supermini](Smartbutton-esp32-C3-Supermini/) | ESP32-C3-SuperMini | Botón IoT dual configurable — HTTP/MQTT, deep sleep, captive portal, LED azul onboard, OTA |
| [Smartbutton-esp32-C3-Supermini-SENSORS](Smartbutton-esp32-C3-Supermini-SENSORS/) | ESP32-C3-SuperMini | Dispositivo IoT genérico con 3 entradas (botones, PIR, radares) — HTTP/MQTT, estabilización, cooldown, portal web y OTA |

## Estructura

```text
esp32-projects/
├── README.md
├── WiFi-Repeater/                          ← Repetidor WiFi con web UI
│   ├── README.md
│   ├── firmware/
│   ├── img/
│   ├── main/
│   └── ...
├── Smartbutton-esp32-c5/                   ← Botón IoT dual HTTP/MQTT (C5)
│   ├── README.md
│   ├── components/
│   ├── main/
│   └── ...
├── Smartbutton-esp32-c5-ws/                ← Botón IoT dual HTTP/MQTT/WS (C5)
│   ├── README.md
│   ├── components/
│   ├── main/
│   └── ...
├── Smartbutton-esp32-C3-Supermini/         ← Botón IoT dual HTTP/MQTT (C3)
│   ├── README.md
│   ├── components/
│   ├── firmware/
│   ├── main/
│   └── ...
├── Smartbutton-esp32-C3-Supermini-SENSORS/ ← Disp. IoT 3 entradas (PIR/Radar/Botones)
│   ├── README.md
│   ├── components/
│   ├── firmware/
│   ├── main/
│   └── ...
└── (futuros proyectos)/ 
```
