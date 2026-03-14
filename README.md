# ESP32 Projects

Colección de proyectos prácticos con ESP32, ESP32-C3, ESP32-S3 y variantes. Cada carpeta contiene un proyecto independiente con su propio README, código fuente, binarios precompilados e instrucciones.

## Proyectos

| Proyecto | Chip | Descripción |
|---|---|---|
| [WiFi-Repeater](WiFi-Repeater/) | ESP32-C3-SuperMini | Repetidor WiFi con interfaz web profesional, NAPT, captive portal, OTA, auth y factory reset |
| [Smartbutton-esp32-c5](Smartbutton-esp32-c5/) | ESP32-C5 | Botón IoT dual configurable — HTTP/MQTT, deep sleep, captive portal, LED RGB, OTA |

## Estructura

```
esp32-projects/
├── README.md
├── WiFi-Repeater/              ← Repetidor WiFi con web UI
│   ├── README.md
│   ├── firmware/
│   ├── img/
│   ├── main/
│   └── ...
├── Smartbutton-esp32-c5/       ← Botón IoT dual HTTP/MQTT
│   ├── README.md
│   ├── components/
│   ├── main/
│   └── ...
└── (futuros proyectos)/
```

## Requisitos comunes

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/) (v5.x o superior)
- [esptool.py](https://github.com/espressif/esptool) (para flashear binarios precompilados)
- Cable USB-C
- Placa ESP32 según el proyecto

## Autor

[@soyunomas](https://github.com/soyunomas)
