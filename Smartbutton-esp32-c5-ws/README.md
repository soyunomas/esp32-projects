# SmartButton ESP32-C5 ws

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.1-blue)](https://github.com/espressif/esp-idf)
[![MCU](https://img.shields.io/badge/MCU-ESP32--C5-green)](https://www.espressif.com/en/products/socs/esp32-c5)
[![License](https://img.shields.io/badge/license-MIT-orange)](LICENSE)

Botón IoT dual configurable basado en ESP32-C5. Pulsa un botón físico y ejecuta una petición HTTP, publica un mensaje MQTT o envía un payload por WebSocket. Configurable fácilmente desde cualquier dispositivo vía portal web captivo.

## Características

- **2 botones físicos** con acción configurable cada uno (HTTP GET/POST, MQTT o WebSocket).
- **Nombres personalizables** — pon nombre propio a cada botón (ej: "Botón Rojo", "Alarma") desde la web.
- **Soporte MQTT y WebSockets** — comunícate en tiempo real con brokers o servidores WS.
- **Portal web captivo** — al conectarte a su red, se abre automáticamente el panel de configuración.
- **Interfaz Dark moderna** — panel responsive, asíncrono (AJAX) y con estilo oscuro.
- **Escaneo WiFi** desde la web para seleccionar tu red visualmente.
- **HTTPS/WSS nativo** — soporte TLS integrado mediante `esp_crt_bundle` para APIs y websockets seguros.
- **Autenticación** con usuario/contraseña (por defecto `admin`/`admin`).
- **Cooldown y Timeout** — anti-spam de pulsaciones y control de tiempo de espera por cada botón.
- **Evitar caché (HTTP 304)** — opción por botón para agregar un parámetro aleatorio a la URL.
- **Deep Sleep Inteligente** — uso de retención en dominio RTC (`EXT1`) para evitar rebotes y consumos parásitos mientras duerme. Despierta, ejecuta y vuelve a dormir.
- **Modo Cliente Puro** — oculta el AP propio tras conectar a tu red WiFi.
- **AP configurable y seguro** — Por defecto emite con cifrado WPA2. SSID y contraseña personalizables.
- **Feedback visual RTOS** — LED RGB WS2812 y LEDs integrados en botones (GPIO 6 y 8) controlados por tareas en tiempo real.
- **Test síncrono web** — prueba las acciones (HTTP/MQTT/WS) directamente desde el panel web viendo el resultado al instante.
- **Factory reset dinámico** — mantén ambos botones pulsados (tiempo configurable) para borrar la memoria (NVS).
- **Actualizaciones OTA** — sube nuevos firmwares `.bin` directamente desde el panel de control web.
- **Fallback automático** — si falla la conexión WiFi tras X reintentos, levanta el AP propio para evitar que el dispositivo quede inaccesible (Modo Híbrido APSTA).

## Hardware

### MCU
- **ESP32-C5** (RISC-V, WiFi 6 dual-band, BLE 5)
- Flash: **4MB**

### Pinout

| Función | GPIO | Configuración |
|---------|------|---------------|
| Botón 1 (Rojo) | **GPIO 4** | Pull-up interno, active-low, Wakeup EXT1 |
| Botón 2 (Verde) | **GPIO 5** | Pull-up interno, active-low, Wakeup EXT1 |
| LED Botón 1 (Rojo) | **GPIO 6** | Salida digital |
| LED Botón 2 (Verde) | **GPIO 8** | Salida digital |
| LED RGB | **GPIO 27** | Salida digital (Protocolo WS2812 / NeoPixel) |

Los botones son **normalmente abiertos** (NO) conectados entre GPIO y GND. El firmware activa los pull-ups internos de forma segura incluso antes de despertar de Deep Sleep.

## Flashear el binario precompilado

Requiere [esptool](https://docs.espressif.com/projects/esptool/en/latest/esp32/):

```bash
esptool.py --chip esp32c5 -b 460800 \
  --before default-reset --after hard-reset \
  write_flash --flash_mode dio --flash_size 4MB --flash_freq 80m \
  0x2000   firmware/bootloader.bin \
  0x8000   firmware/partition-table.bin \
  0xf000   firmware/ota_data_initial.bin \
  0x20000  firmware/smartbutton_c5.bin
```

## Compilar desde el código fuente

Requiere **ESP-IDF v6.1+**.

```bash
git clone https://github.com/soyunomas/esp32-projects.git
cd esp32-projects/Smartbutton-esp32-c5

idf.py set-target esp32c5
idf.py build

# Flashear y monitorizar (ajusta /dev/ttyUSB0 o COMx)
idf.py -p /dev/ttyUSB0 erase_flash flash monitor
```

## Uso Normal y Primera Configuración

1. **Alimenta** el dispositivo. Al no tener WiFi guardada, el LED RGB parpadeará en Azul y los LEDs de los botones parpadearán 5 veces en rojo/verde.
2. Desde tu móvil/PC, conéctate a la red WiFi **`SmartButton-XXXX`**.
   - Contraseña por defecto del WiFi: **`smartbutton`**
3. El portal de configuración se abrirá solo. Si no lo hace, entra a **`http://192.168.5.1`**.
4. Inicia sesión con usuario **`admin`** y contraseña **`admin`**.
5. Ve a la pestaña **WiFi**, escanea tu red y pon la contraseña.
6. Configura tus botones y guarda. El dispositivo se reiniciará.

> **Importante:** La IP del modo AP es siempre `192.168.5.1`. Si el dispositivo se conecta a tu router, deberás buscar en tu router qué IP dinámica le ha asignado para entrar a la web, o conectar un cable USB y ver el Monitor Serie.

## Feedback Visual de los LEDs

### LED RGB (WS2812 - GPIO 27)
| Estado | Color LED |
|--------|-----------|
| **Modo AP (Configuración)** | 🔵 Azul (Parpadeo lento) |
| **Conectando a WiFi** | 🟡 Amarillo (Parpadeo rápido) |
| **Conectado / Listo** | 🟢 Verde **Fijo** |
| **Procesando Petición** | 🌐 Cyan (Pulso hiperrápido) |
| **Petición Exitosa (HTTP 2xx / MQTT / WS)**| 🟢 Verde (Flash brillante 1 seg) |
| **Error (Timeout / Fallo WiFi o Red)** | 🔴 Rojo (Triple flash rápido) |
| **Aviso Reset Inminente / Ejecutando** | 🔴 Rojo (Parpadeo rápido / Fijo) |

### LEDs Físicos de Botón (Rojo GPIO 6 / Verde GPIO 8)
* **Arrancando sin WiFi:** Parpadean ambos 5 veces.
* **Conectando WiFi:** Parpadean alternando (Rojo ↔ Verde).
* **Al pulsar un botón:** Se enciende fijo el LED del botón pulsado mientras la petición se procesa por HTTP/MQTT/WS y se apaga al finalizar.
* **Test desde Web:** Se enciende el LED correspondiente mientras se prueba la conexión.

## Configuración de los Botones

La interfaz consolida las acciones. Dependiendo del **"Tipo de Acción"**, el campo de destino cambia:

| Tipo de Acción | Destino (Target) | Ejemplo |
|----------------|------------------|---------|
| **Petición HTTP** | URL (Soporta HTTPS) | `https://ntfy.sh/mi-topic/publish?message=Alerta` |
| **Mensaje MQTT** | Topic | `homeassistant/sensor/boton1/state` |
| **WebSocket** | URI (Soporta WSS) | `ws://192.168.1.100:81` |

* **Método y Payload:** Si eliges HTTP POST, MQTT o WebSocket, se habilitará el campo Payload (cuerpo del mensaje en formato texto o JSON).
* **Cooldown:** Tiempo en segundos que el dispositivo ignora las pulsaciones del mismo botón para evitar spam.
* **Timeout:** Tiempo máximo en segundos que el ESP32 espera respuesta del servidor.

## Deep Sleep (Modo bajo consumo)

Si alimentas la placa con batería, activa el **Deep Sleep** en la pestaña de Sistema.
El comportamiento es el siguiente:

1. **Arranque en frío (se pone la pila):** Despierta, conecta al WiFi y se queda encendido `T. Config` (por defecto 180s) para permitirte entrar por web a cambiar ajustes. Pasado ese tiempo, duerme.
2. **Despertar por pulsación:** Al pulsar un botón, la placa despierta del sueño profundo, se conecta a WiFi rápidamente, ejecuta la acción configurada para ese botón, y **se vuelve a dormir inmediatamente**.
3. **Seguridad:** Si la petición falla o el router no responde, el ESP32 no agotará la batería; se dormirá tras alcanzar el `T. Wakeup` (por defecto 30s).

## Configuración Avanzada y Sistema

En la sección **Sistema > Avanzado** puedes modificar el núcleo del dispositivo:

* **Tiempo de Reset:** Segundos manteniendo ambos botones para borrar la memoria (Factory Reset).
* **Reintentos WiFi:** Veces que intentará conectar a tu router antes de rendirse y levantar su propio AP para que puedas arreglarlo.
* **Modo Cliente Puro:** Si está activado, la red `SmartButton-XXXX` desaparece completamente una vez conectado a tu casa. Más seguro.
* **Anti-rebote:** Tiempo en milisegundos para filtrar falsas pulsaciones del hardware.

## Arquitectura del Software

Desarrollado en **C** bajo **ESP-IDF v6.1**. Modular y orientado a eventos:
* `app_core`: Máquina de estados principal (`system_state_t`).
* `app_nvs`: Persistencia estructurada en Flash.
* `app_wifi`: Gestión STA/AP/APSTA con fallback.
* `app_dns`: Servidor DNS UDP para Portal Cautivo.
* `app_http` / `app_mqtt` / `app_ws`: Clientes de red en tareas asíncronas aisladas (`xTaskCreate`).
* `app_buttons` / `app_btn_leds`: Polling GPIO anti-rebotes y control visual local.
* `app_led`: Control WS2812 usando notificaciones RTOS puras (`xTaskNotify`).
* `app_web`: Servidor HTTPD embebido para la REST API (protegida por Basic Auth) y entrega del HTML minificado.
* Firmware con soporte de **Actualizaciones OTA** (Dual Partition).

## Licencia
Distribuido bajo la licencia **MIT**. Libertad total para uso, modificación y distribución.
