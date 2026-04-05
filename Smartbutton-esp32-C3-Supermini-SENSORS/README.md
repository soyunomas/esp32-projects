# SmartButton-SENSORS

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.1-blue)](https://github.com/espressif/esp-idf)
[![MCU](https://img.shields.io/badge/MCU-ESP32--C3-green)](https://www.espressif.com/en/products/socs/esp32-c3)
[![License](https://img.shields.io/badge/license-MIT-orange)](LICENSE)

Dispositivo IoT genérico basado en ESP32-C3 SuperMini con **3 entradas configurables** — botones mecánicos, sensores PIR, radares de microondas o cualquier dispositivo con salida digital. Cada entrada dispara una petición HTTP o publica un mensaje MQTT. Configurable desde cualquier dispositivo vía portal web captivo.

## Características

- **3 entradas configurables** — asigna GPIO, tipo (botón activo LOW / sensor activo HIGH) y acción por cada entrada.
- **Tipos de entrada soportados** — botones mecánicos, sensores PIR (HC-SR505), radares (XYC-WB-DC) o cualquier sensor con salida digital.
- **Acciones HTTP y MQTT** por entrada — HTTP GET/POST o publicación MQTT.
- **Habilitación por entrada** — activa o desactiva cada entrada sin borrar su configuración. El botón de reset siempre funciona.
- **Tiempo de estabilización** — retardo configurable tras encendido (0–120 seg) para que sensores PIR/radar se estabilicen antes de responder.
- **Cooldown anti-ráfaga** — tiempo de silencio tras cada activación (0.5 seg a 10 min) para evitar disparos repetitivos, especialmente útil con radares.
- **Factory Reset por botón** — mantén pulsado el botón designado como reset (tiempo configurable 3–60 seg).
- **Soporte MQTT** — publica mensajes a un broker MQTT al activarse una entrada.
- **Portal web captivo** — al conectarte a su red, se abre automáticamente.
- **Interfaz Dark moderna** — panel responsive con estilo oscuro.
- **Escaneo WiFi** desde la web para seleccionar tu red visualmente.
- **HTTPS nativo** — soporte TLS integrado mediante `esp_crt_bundle` para llamar APIs seguras.
- **Autenticación** con usuario/contraseña (por defecto `admin`/`admin`).
- **Evitar caché (HTTP 304)** — opción por entrada para agregar un parámetro aleatorio a la URL.
- **Modo Cliente Puro** — oculta el AP propio tras conectar a tu red WiFi para mayor seguridad.
- **AP configurable** — SSID y contraseña del punto de acceso personalizables (abierto o WPA2).
- **Feedback visual RTOS** — LED azul onboard con notificaciones FreeRTOS en tiempo real.
- **Botón de test** para probar las acciones HTTP y MQTT desde el propio panel web.
- **Actualizaciones OTA** — sube nuevos firmwares `.bin` directamente desde el panel de control.
- **Fallback automático** — si falla la conexión WiFi (reintentos configurables), vuelve a crear su propio AP.

## Hardware

### MCU

- **ESP32-C3** (RISC-V, WiFi 4 2.4 GHz, BLE 5)
- Flash: **4MB**

### GPIOs disponibles

Los pines se configuran desde la interfaz web. GPIOs disponibles:

| GPIO | Notas |
|------|-------|
| **0, 1, 2, 3, 4, 5** | Uso general |
| **6, 7** | Uso general (también I²C) |
| **10** | Uso general |
| **8** | ⚠️ LED azul onboard (no usar como entrada) |
| **9** | ⚠️ Botón BOOT (no recomendado) |
| **20, 21** | ⚠️ UART (no disponibles) |

### Ejemplo de conexión: 2 sensores + 1 botón

```text
ESP32-C3 SuperMini
┌──────────┐
│ GPIO 2   ├──── PIR HC-SR505 (OUT)     [Sensor, Activo HIGH]
│ GPIO 3   ├──── Radar XYC-WB-DC (OUT)  [Sensor, Activo HIGH]
│ GPIO 4   ├──── Botón ──── GND         [Botón, Activo LOW, Reset]
│          │
│ GPIO 8   ├──── LED Azul (Onboard)
└──────────┘
```

- **Botones** — normalmente abiertos (NO), conectan GPIO a GND. El firmware activa el pull-up interno.
- **Sensores** — alimentar Vcc/GND por separado. La salida OUT va directamente al GPIO. El firmware activa pull-down interno.

## Flashear el binario precompilado (sin compilar)

Si solo quieres flashear directamente sin instalar ESP-IDF, necesitas **esptool**:

```bash
pip install esptool
```

Conectar la placa por USB e identificar el puerto (`/dev/ttyUSB0`, `/dev/ttyACM0`, `COMx`...), luego:

```bash
esptool.py --chip esp32c3 -b 460800 \
  --before default-reset --after hard-reset \
  write_flash --flash_mode dio --flash_size 4MB --flash_freq 80m \
  0x0000   firmware/bootloader.bin \
  0x8000   firmware/partition-table.bin \
  0xf000   firmware/ota_data_initial.bin \
  0x10000  firmware/smartbutton_c3.bin
```

> ⚠️ Sustituye el puerto si es necesario añadiendo `-p /dev/ttyACM0` (Linux) o `-p COM3` (Windows).

> 💡 Si no detecta la placa, mantén pulsado el botón **BOOT** mientras conectas el USB.

---

## Compilar desde el código fuente

### Requisitos

- [ESP-IDF v6.1+](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/get-started/) configurado y activo.
- Placa ESP32-C3 (Flash: 4MB).

### Clonar y compilar

```bash
git clone https://github.com/soyunomas/esp32-projects.git
cd esp32-projects/Smartbutton-esp32-C3-Supermini-SENSORS

# Configurar entorno ESP-IDF (si no lo tienes en tu .bashrc/.zshrc)
. $HOME/esp/esp-idf/export.sh

# Configurar target (solo la primera vez)
idf.py set-target esp32c3

# Compilar
idf.py build

# Flashear primera vez (borra NVS anterior para asegurar limpieza)
# Cambia /dev/ttyACM0 por tu puerto correspondiente (COMx en Windows)
idf.py -p /dev/ttyACM0 erase_flash flash monitor

# Flashear futuras actualizaciones por cable (conserva configuración WiFi/Botones)
idf.py -p /dev/ttyACM0 flash monitor
```

Si al compilar da error de directorio de build incompatible, limpia y reconfigura:

```bash
idf.py fullclean
idf.py set-target esp32c3
idf.py build
```

## Uso Normal

### Primera configuración

1. **Alimenta** el dispositivo.
2. Desde tu móvil/PC, busca la red WiFi **`SmartButton-XXXX`**.
3. El portal de configuración se abre automáticamente (o navega a `http://192.168.4.1`).
4. Inicia sesión con **admin** / **admin**.
5. Ve a **WiFi**, escanea tu red, selecciónala y pon la contraseña.
6. En **Integraciones**, configura MQTT si lo necesitas.
7. En **Entradas**, configura cada entrada: GPIO, tipo (botón/sensor), estabilización, acción HTTP/MQTT y cooldown.
8. Guarda. El dispositivo se reiniciará y se conectará a tu red.

### Configuración de Entradas

Desde la pestaña **Entradas** puedes configurar hasta 3 entradas independientes:

| Parámetro | Descripción |
|-----------|-------------|
| **Habilitada** | Activa/desactiva la entrada sin borrar su configuración |
| **GPIO Pin** | Pin físico al que está conectado el dispositivo |
| **Tipo de Entrada** | `Botón (Activo LOW)` — pull-up, se activa al conectar a GND |
| | `Sensor (Activo HIGH)` — pull-down, se activa al recibir 3.3V |
| **Estabilización** | Tiempo de espera tras encendido antes de empezar a leer (0–120 seg). Útil para PIR (~30s) o para salir del campo del sensor tras configurar |
| **Botón de Reset** | Designa esta entrada como botón de factory reset (solo botones, no sensores) |
| **Tipo de Acción** | `Petición HTTP` o `Mensaje MQTT` |
| **URL / Topic** | Dirección HTTP (soporta HTTPS) o topic MQTT |
| **Método** | `GET` o `POST` (solo HTTP) |
| **Payload** | Cuerpo de la petición POST o mensaje MQTT (~1 KB) |
| **Timeout** | Tiempo máximo de espera de respuesta (1–30 seg) |
| **Cooldown** | Tiempo de silencio tras activación (0.5 seg – 10 min). **Para radares**, usar valores altos para evitar disparos en ráfaga |
| **Evitar caché** | Agrega parámetro aleatorio a la URL para evitar HTTP 304 |

### Comportamiento del LED Azul

| Estado | Patrón de parpadeo (LED Azul) |
|--------|-------------------------------|
| **Modo AP (Configuración)** | Parpadeo lento (1s) |
| **Conectando a WiFi** | Parpadeo rápido (200ms) |
| **Conectado / Listo** | **Fijo** |
| **Procesando Petición** | Pulso hiperrápido (150ms) |
| **Petición Exitosa (HTTP 2xx / MQTT OK)**| Flash brillante largo de 1 segundo |
| **Error (Timeout / Fallo)** | Triple flash rápido |
| **Aviso de Reset Inminente** | Parpadeo muy rápido (100ms) |
| **Reset en curso** | **Fijo** |

### Modo Cliente Puro

Activable desde **Sistema > Modo Cliente Puro**. Oculta el punto de acceso propio (`SmartButton-XXXX`) una vez conectado exitosamente a tu red WiFi. Útil para mayor seguridad en entornos de producción. Si la conexión falla tras los reintentos configurados (por defecto 5), el AP se reactiva automáticamente como fallback.

### Factory Reset (Restaurar de fábrica)

Mantén pulsado el botón designado como **reset** durante el tiempo configurado (por defecto 8 segundos, ajustable en Sistema > Avanzado).

- **Fase 1 (inicio):** El LED parpadea lento advirtiendo de la pulsación prolongada. Si sueltas, se cancela.
- **Fase 2 (últimos ~3 seg):** El LED parpadea rápidamente advirtiendo del borrado inminente.
- **Fase 3 (fin del tiempo):** El LED se queda fijo, la placa borra toda la NVS y reinicia en Modo AP de fábrica.

También se puede realizar Factory Reset desde el panel web en **Sistema > Factory Reset**.

### GET vs POST: ¿cuándo usar cada uno?

- **GET** — Toda la información va en la URL. No envía body/payload. Ideal para webhooks simples, APIs que reciben parámetros por query string, o servicios que solo necesitan que "toques" una URL.
- **POST** — Envía la URL + un cuerpo (payload) con datos adicionales (se envía como `Content-Type: application/json`). Necesario cuando el servicio espera recibir datos estructurados.

### Ejemplos prácticos

#### ntfy.sh — Notificación push (GET)

| Campo | Valor |
|-------|-------|
| **Método** | `GET` |
| **URL** | `https://ntfy.sh/mi-topic/publish?message=Movimiento+detectado&title=Alerta&priority=high&tags=rotating_light` |

#### ntfy.sh — Notificación push (POST)

| Campo | Valor |
|-------|-------|
| **Método** | `POST` |
| **URL** | `https://ntfy.sh/mi-topic` |
| **Payload** | `{"topic":"mi-topic","title":"Alerta","message":"Movimiento detectado por radar","priority":4,"tags":["rotating_light"]}` |

#### Telegram — Enviar mensaje vía Bot API (GET)

| Campo | Valor |
|-------|-------|
| **Método** | `GET` |
| **URL** | `https://api.telegram.org/bot<TU_TOKEN>/sendMessage?chat_id=<CHAT_ID>&text=Movimiento+detectado` |

#### Home Assistant — Llamar un webhook (POST)

| Campo | Valor |
|-------|-------|
| **Método** | `POST` |
| **URL** | `https://tu-ha.duckdns.org/api/webhook/sensor-movimiento` |
| **Payload** | `{"action":"toggle","entity":"light.salon"}` |

#### IFTTT — Disparar un evento (GET)

| Campo | Valor |
|-------|-------|
| **Método** | `GET` |
| **URL** | `https://maker.ifttt.com/trigger/movimiento/with/key/<TU_KEY>?value1=radar` |

> **Nota:** El campo Payload solo se envía con método POST. En GET se ignora. La URL admite hasta 510 caracteres y el Payload hasta ~1 KB.

## Configuración MQTT

Desde **Integraciones > MQTT Broker** puedes configurar la conexión al broker:

| Parámetro | Descripción |
|-----------|-------------|
| **Habilitar MQTT** | Activa o desactiva el soporte MQTT global |
| **Servidor** | Host o IP del broker MQTT |
| **Puerto** | Puerto del broker (por defecto 1883) |
| **Client ID** | Identificador del cliente (auto-generado si se deja vacío) |
| **Usuario / Contraseña** | Credenciales opcionales para el broker |

## Configuración del AP

Desde **Sistema > AP Local** puedes personalizar el punto de acceso:

| Parámetro | Descripción |
|-----------|-------------|
| **SSID** | Nombre personalizado de la red AP. En blanco genera `SmartButton-XXXX` automáticamente |
| **Contraseña WPA2** | Mínimo 8 caracteres para activar cifrado WPA2. En blanco = red abierta |
| **Modo Cliente Puro** | Oculta el AP tras conectar exitosamente a tu red WiFi |

## Configuración Avanzada

Desde **Sistema > Avanzado** se pueden ajustar parámetros internos del firmware:

| Parámetro | Descripción | Rango | Default |
|-----------|-------------|-------|---------|
| **Reintentos WiFi** | Intentos de reconexión antes de activar el fallback AP | 1 – 20 | 5 |
| **Canal AP WiFi** | Canal del punto de acceso propio (para evitar interferencias) | 1 – 13 | 1 |
| **Anti-rebote** | Tiempo mínimo de pulsación para considerar un botón válido (ms) | 50 – 500 | 200 |
| **T. Reset** | Tiempo que hay que mantener pulsado el botón de reset (seg) | 3 – 60 | 8 |

## Arquitectura del Software

Este proyecto está diseñado en **C** con **ESP-IDF** y divide sus responsabilidades en componentes débilmente acoplados:

- `app_core`: Máquina de estados global, gestión de Event Groups.
- `app_nvs`: Capa de persistencia en Flash (NVS) para WiFi, credenciales, entradas, MQTT y ajustes de sistema.
- `app_wifi`: Modos STA, AP y APSTA con fallback automático tras reintentos configurables.
- `app_web`: Servidor HTTP, interfaz de usuario embebida (`html_ui.h`) y endpoints REST con autenticación Basic Auth.
- `app_http`: Cliente HTTP/HTTPS asíncrono con TLS via `esp_crt_bundle`, ejecutado en tarea independiente.
- `app_mqtt`: Cliente MQTT oneshot — conecta, publica y desconecta por cada acción de entrada.
- `app_buttons`: Polling GPIO dinámico con soporte para botones (activo LOW) y sensores (activo HIGH, detección de flanco), estabilización, cooldown anti-ráfaga y factory reset por botón designado.
- `app_led`: Feedback visual en tiempo real usando el LED azul onboard (GPIO8) y notificaciones RTOS (`xTaskNotify`).
- `app_dns`: Servidor DNS ultraligero para secuestro web (Captive Portal).

### Endpoints del API REST

La interfaz embebida interactúa con la placa mediante las siguientes rutas HTTP (protegidas con Basic Auth):

| Endpoint | Método | Uso |
|----------|--------|-----|
| `/` | `GET` | Carga el portal (HTML/CSS/JS embebido) |
| `/api/verify` | `GET` | Validación de credenciales de sesión |
| `/api/scan` | `GET` | Devuelve JSON con redes WiFi cercanas (RSSI y Auth) |
| `/api/wifi` | `POST`| Guarda SSID y Password y reinicia |
| `/api/mqtt` | `GET` | Recupera la configuración MQTT actual |
| `/api/mqtt` | `POST`| Guarda la configuración del broker MQTT |
| `/api/btn?id=N` | `GET` | Recupera los ajustes actuales de una entrada |
| `/api/btn` | `POST`| Guarda configuración de la entrada (GPIO, tipo, acción, etc.) y reinicia |
| `/api/test` | `POST`| Realiza una prueba sincrónica HTTP o MQTT y devuelve el resultado |
| `/api/netinfo`| `GET` | Devuelve IPs, MAC, Gateway y estado de red actual |
| `/api/admin` | `GET/POST` | Recupera o cambia credenciales de admin, AP y ajustes de sistema |
| `/api/factory_reset` | `POST` | Borra toda la NVS y reinicia con valores de fábrica |
| `/api/ota` | `POST`| Recibe un binario y ejecuta actualización de firmware |

## Tabla de particiones

Configurada con soporte Dual OTA para que el firmware nunca se corrompa en caso de corte de energía durante una actualización vía web.

| Nombre | Tipo | Subtipo | Tamaño |
|-----------|------|---------|--------|
| nvs | data | nvs | 24 KB |
| otadata | data | ota | 8 KB |
| phy_init | data | phy | 4 KB |
| ota_0 | app | ota_0 | 1700 KB |
| ota_1 | app | ota_1 | 1700 KB |
| storage | data | spiffs | 300 KB |

## Licencia

Este proyecto se distribuye bajo la licencia **MIT**. Puedes usarlo, modificarlo y distribuirlo libremente.
