# AGENTS.md - ESP32-C3 WiFi Repeater

## Expert Profile
Soy un experto senior en sistemas embebidos con especialización en:
- **ESP-IDF** (Espressif IoT Development Framework) - APIs nativas, lwIP, NAPT, WiFi STA+AP
- **ESP32-C3-SuperMini** - RISC-V, limitaciones de memoria, pinout, deep sleep
- **Interfaces Web embebidas** - HTML/CSS/JS minificado, SPA responsive, WebSocket real-time
- **UX/UI profesional** - Diseño moderno dark-theme, mobile-first, accesible
- **Networking** - NAT/NAPT, DHCP, DNS relay, TCP/IP stack tuning

## Project Description
Repetidor WiFi basado en ESP32-C3-SuperMini. El dispositivo se conecta a una red WiFi
existente (modo STA) y crea un punto de acceso propio (modo AP) para extender la cobertura.
Toda la configuración se realiza mediante una interfaz web profesional servida desde el
propio dispositivo.

## Directory Structure

```
ESP32-C3-Repeater/
├── AGENTS.md                  # Este archivo - instrucciones del agente
├── PROGRESS.md                # Historial de cambios y TODO
├── README.md                  # Documentación del proyecto
├── CMakeLists.txt             # Build system raíz ESP-IDF
├── sdkconfig.defaults         # Configuración por defecto del SDK
├── partitions.csv             # Tabla de particiones personalizada
├── main/
│   ├── CMakeLists.txt         # Build del componente principal
│   ├── main.c                 # Entry point - inicialización
│   ├── wifi_manager.c         # Gestión WiFi STA+AP, NAPT
│   ├── wifi_manager.h         # Header WiFi manager
│   ├── web_server.c           # Servidor HTTP + API REST
│   ├── web_server.h           # Header web server
│   ├── config_storage.c       # Persistencia NVS
│   ├── config_storage.h       # Header config storage
│   ├── dns_server.c           # Captive portal DNS
│   ├── dns_server.h           # Header DNS server
│   └── embedded_files/        # Archivos web embebidos
│       ├── index.html         # SPA principal
│       ├── styles.css         # Estilos profesionales
│       └── app.js             # Lógica frontend
└── components/                # Componentes ESP-IDF adicionales
```

## Build Commands
```bash
# Configurar entorno
source /home/yo/esp/esp-idf/export.sh

# Compilar
idf.py build

# Flash + Monitor
idf.py -p /dev/ttyACM0 flash monitor

# Limpiar
idf.py fullclean

# Configurar menuconfig
idf.py menuconfig
```

## Conventions
- Código C siguiendo estilo ESP-IDF (snake_case, prefijos esp_/wifi_/web_)
- Logs con ESP_LOGI/ESP_LOGW/ESP_LOGE usando TAG por módulo
- Manejo de errores con ESP_ERROR_CHECK y esp_err_t
- NVS para persistencia de configuración
- Web UI: dark theme, responsive, vanilla JS (sin frameworks externos)
- HTML/CSS/JS embebidos en flash via EMBED_FILES

## Hardware Target
- **Chip**: ESP32-C3 (RISC-V single core 160MHz)
- **Board**: SuperMini (4MB flash, antena PCB)
- **RAM**: 400KB SRAM
- **WiFi**: 802.11 b/g/n, 2.4GHz

## Key Features
1. WiFi Repeater (STA+AP simultáneo con NAPT)
2. Web UI profesional para configuración (dark theme, responsive)
3. Escaneo de redes WiFi disponibles
4. Captive portal para configuración inicial
5. Persistencia de configuración en NVS
6. Estadísticas en tiempo real (clientes, RSSI, tráfico)
7. OTA updates via web (dual partitions ota_0/ota_1 + rollback automático)
8. Autenticación web (HTTP Basic Auth, credenciales únicas por MAC, persistidas en NVS)
9. Test de conectividad (ICMP ping + resolución DNS)
10. Factory reset desde la interfaz web (borra NVS + reinicia con defaults)

## Web Authentication
- **Usuario por defecto**: `admin`
- **Password por defecto**: `admin`
- Cambiable desde System → Web Credentials en la UI
- Todos los endpoints `/api/*` requieren HTTP Basic Auth
- Archivos estáticos (`/`, `/styles.css`, `/app.js`) sin autenticación

## OTA Updates
- Tabla de particiones: `ota_0` + `ota_1` (2x ~1.94MB en flash 4MB)
- Upload binario raw via `POST /api/ota` (streaming por chunks, no multipart)
- Rollback automático: `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`
- Primera vez tras cambio de particiones requiere flash serial
