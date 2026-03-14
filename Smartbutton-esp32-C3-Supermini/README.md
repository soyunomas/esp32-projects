# SmartButton

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.1-blue)](https://github.com/espressif/esp-idf)
[![MCU](https://img.shields.io/badge/MCU-ESP32--C3-green)](https://www.espressif.com/en/products/socs/esp32-c3)
[![License](https://img.shields.io/badge/license-MIT-orange)](LICENSE)

Botón IoT dual configurable basado en ESP32-C3 SuperMini. Pulsa un botón físico y ejecuta una petición HTTP o publica un mensaje MQTT. Configurable fácilmente desde cualquier dispositivo vía portal web captivo.

## Características

- **2 botones físicos** con acción configurable cada uno (HTTP GET/POST o MQTT).
- **Soporte MQTT** — publica mensajes a un broker MQTT al pulsar un botón.
- **Portal web captivo** — al conectarte a su red, se abre automáticamente.
- **Interfaz Dark moderna** — panel responsive con estilo oscuro.
- **Escaneo WiFi** desde la web para seleccionar tu red visualmente.
- **HTTPS nativo** — soporte TLS integrado mediante `esp_crt_bundle` para llamar APIs seguras.
- **Autenticación** con usuario/contraseña (por defecto `admin`/`admin`).
- **Cooldown y Timeout** — anti-spam de pulsaciones y control de tiempo de espera.
- **Evitar caché (HTTP 304)** — opción por botón para agregar un parámetro aleatorio a la URL y evitar respuestas 304 cacheadas.
- **Deep Sleep** — modo ultra bajo consumo que despierta al pulsar un botón y ejecuta la acción correspondiente.
- **Modo Cliente Puro** — oculta el AP propio tras conectar a tu red WiFi para mayor seguridad.
- **AP configurable** — SSID y contraseña del punto de acceso personalizables (abierto o WPA2).
- **Feedback visual RTOS** — LED azul onboard con notificaciones FreeRTOS en tiempo real.
- **Botón de test** para probar las acciones HTTP y MQTT desde el propio panel web.
- **Factory reset dinámico** — mantén ambos botones (tiempo configurable desde la web).
- **Actualizaciones OTA** — sube nuevos firmwares `.bin` directamente desde el panel de control.
- **Configuración avanzada** — reintentos WiFi, canal AP, timeouts de deep sleep y anti-rebote de botones ajustables desde la web.
- **Fallback automático** — si falla la conexión WiFi (reintentos configurables), vuelve a crear su propio AP.

## Hardware

### MCU

- **ESP32-C3** (RISC-V, WiFi 4 2.4 GHz, BLE 5)
- Flash: **4MB**

### Pinout

| Función | GPIO | Configuración |
|---------|------|---------------|
| Botón 1 | **GPIO 4** | Pull-up interno, active-low |
| Botón 2 | **GPIO 5** | Pull-up interno, active-low |
| LED Azul | **GPIO 8** | Salida digital (Active LOW, integrado en la placa SuperMini) |

### Esquema de conexión

*Nota: La placa ESP32-C3 SuperMini incluye un LED azul integrado en GPIO 8.*

```text
ESP32-C3 SuperMini
┌──────────┐
│ GPIO 4   ├──── BTN1 ──── GND
│ GPIO 5   ├──── BTN2 ──── GND
│          │
│ GPIO 8   ├──── LED Azul (Onboard)
└──────────┘
```

Los botones son **normalmente abiertos** (NO). Al pulsar conectan el GPIO a GND. No necesitan resistencia externa — el firmware activa el pull-up interno del ESP32.

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
cd esp32-projects/Smartbutton-esp32-C3-Supermini

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
7. En **Botones**, configura las acciones HTTP o MQTT que necesites.
8. Guarda. El dispositivo se reiniciará y se conectará a tu red.

### Comportamiento del LED Azul

El dispositivo cuenta con un sistema de estados RTOS que responde **al instante** mediante notificaciones FreeRTOS:

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
| **Entrando en Deep Sleep** | Se apaga por completo |

### Deep Sleep (Modo bajo consumo)

Activable desde **Sistema > Deep Sleep** en el panel web. Ideal para dispositivos alimentados por batería.

- **Arranque en frío** (primera alimentación o reset): el dispositivo permanece despierto un tiempo configurable (por defecto **3 minutos**) para permitir acceso al panel de configuración y luego entra en deep sleep.
- **Despertar por botón**: al pulsar cualquiera de los dos botones, el dispositivo despierta, conecta a WiFi, ejecuta la acción del botón correspondiente y vuelve a dormir automáticamente.
- **Detección temprana del botón**: el GPIO se lee al inicio mismo del arranque (antes de cualquier inicialización) para identificar correctamente qué botón despertó el dispositivo mediante el wake stub del C3.
- **Timeout de seguridad**: si la acción no se completa en el tiempo configurado (por defecto 30 segundos), el dispositivo vuelve a dormir igualmente.

### Modo Cliente Puro

Activable desde **Sistema > Modo Cliente Puro**. Oculta el punto de acceso propio (`SmartButton-XXXX`) una vez conectado exitosamente a tu red WiFi. Útil para mayor seguridad en entornos de producción. Si la conexión falla tras los reintentos configurados (por defecto 5), el AP se reactiva automáticamente como fallback.

### Factory Reset (Restaurar de fábrica)

Mantén **ambos botones pulsados simultáneamente**. Por defecto el tiempo es de 8 segundos (configurable en la web). 

- **Fase 1 (0 a T-3 seg):** El LED parpadea lento advirtiendo de la pulsación prolongada. Si sueltas los botones, la acción se cancela y se retoma la normalidad.
- **Fase 2 (Últimos 3 seg):** El LED parpadeará rápidamente advirtiendo del borrado inminente.
- **Fase 3 (Fin del tiempo):** El LED se queda fijo, la placa se formatea por completo (borrando NVS) y vuelve a iniciar en Modo AP de fábrica.

También se puede realizar Factory Reset desde el panel web en **Sistema > Factory Reset**.

## Configuración de los Botones

Desde la web puedes ajustar parámetros avanzados por cada botón:

| Parámetro | Descripción |
|-----------|-------------|
| **Tipo de Acción** | `Petición HTTP` o `Mensaje MQTT` |
| **URL / Topic** | Dirección HTTP a llamar (soporta HTTP y HTTPS) o topic MQTT donde publicar |
| **Método** | `GET` o `POST` (solo HTTP) |
| **Payload** | Cuerpo de la petición (POST) o mensaje MQTT — hasta ~1 KB, compatible con ntfy.com, Telegram, etc. |
| **Timeout** | Tiempo máximo a esperar la respuesta (1 a 30 seg) |
| **Cooldown**| Tiempo de enfriamiento anti-spam entre pulsaciones (0.5 a 60 seg) |
| **Evitar caché** | Agrega un parámetro aleatorio a la URL para evitar respuestas HTTP 304 |

### GET vs POST: ¿cuándo usar cada uno?

- **GET** — Toda la información va en la URL. No envía body/payload. Ideal para webhooks simples, APIs que reciben parámetros por query string, o servicios que solo necesitan que "toques" una URL.
- **POST** — Envía la URL + un cuerpo (payload) con datos adicionales (se envía como `Content-Type: application/json`). Necesario cuando el servicio espera recibir datos estructurados.

### Ejemplos prácticos

#### ntfy.sh — Notificación push (GET)

La forma más sencilla. Todo va en la URL. **Importante:** para GET se usa el endpoint `/publish` (o `/send` o `/trigger`):

| Campo | Valor |
|-------|-------|
| **Método** | `GET` |
| **URL** | `https://ntfy.sh/mi-topic/publish?message=Boton+1+pulsado&title=Alerta&priority=high&tags=rotating_light` |
| **Payload** | *(vacío)* |

#### ntfy.sh — Notificación push (POST)

Con POST se envía directamente al topic. Útil cuando el mensaje es largo o contiene caracteres especiales:

| Campo | Valor |
|-------|-------|
| **Método** | `POST` |
| **URL** | `https://ntfy.sh/mi-topic` |
| **Payload** | `{"topic":"mi-topic","title":"Alerta","message":"Botón pulsado desde SmartButton","priority":4,"tags":["rotating_light"]}` |

#### Telegram — Enviar mensaje vía Bot API (GET)

| Campo | Valor |
|-------|-------|
| **Método** | `GET` |
| **URL** | `https://api.telegram.org/bot<TU_TOKEN>/sendMessage?chat_id=<CHAT_ID>&text=Boton+pulsado` |
| **Payload** | *(vacío)* |

#### Telegram — Enviar mensaje vía Bot API (POST)

| Campo | Valor |
|-------|-------|
| **Método** | `POST` |
| **URL** | `https://api.telegram.org/bot<TU_TOKEN>/sendMessage` |
| **Payload** | `{"chat_id":"<CHAT_ID>","text":"🔘 Botón 1 pulsado","parse_mode":"HTML"}` |

#### Home Assistant — Llamar un webhook (POST)

| Campo | Valor |
|-------|-------|
| **Método** | `POST` |
| **URL** | `https://tu-ha.duckdns.org/api/webhook/mi-boton-1` |
| **Payload** | `{"action":"toggle","entity":"light.salon"}` |

#### IFTTT — Disparar un evento (GET)

| Campo | Valor |
|-------|-------|
| **Método** | `GET` |
| **URL** | `https://maker.ifttt.com/trigger/boton_pulsado/with/key/<TU_KEY>?value1=btn1` |
| **Payload** | *(vacío)* |

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

Desde **Sistema > AP Local & Ahorro** puedes personalizar el punto de acceso:

| Parámetro | Descripción |
|-----------|-------------|
| **SSID** | Nombre personalizado de la red AP. En blanco genera `SmartButton-XXXX` automáticamente |
| **Contraseña WPA2** | Mínimo 8 caracteres para activar cifrado WPA2. En blanco = red abierta |
| **Modo Cliente Puro** | Oculta el AP tras conectar exitosamente a tu red WiFi |
| **Deep Sleep** | Activa el modo bajo consumo (duerme tras ejecutar acción) |

## Configuración Avanzada

Desde **Sistema > Avanzado** se pueden ajustar parámetros internos del firmware:

| Parámetro | Descripción | Rango | Default |
|-----------|-------------|-------|---------|
| **Reintentos WiFi** | Intentos de reconexión antes de activar el fallback AP | 1 – 20 | 5 |
| **Canal AP WiFi** | Canal del punto de acceso propio (para evitar interferencias) | 1 – 13 | 1 |
| **Timeout wakeup** | Segundos máximos despierto tras wakeup por botón (deep sleep) | 10 – 120 | 30 |
| **Tiempo config** | Segundos despierto en arranque frío para permitir configuración | 30 – 600 | 180 |
| **Anti-rebote** | Tiempo mínimo de pulsación para considerar un botón válido (ms) | 50 – 500 | 200 |

## Arquitectura del Software

Este proyecto está diseñado en **C** con **ESP-IDF** y divide sus responsabilidades en componentes débilmente acoplados:

- `app_core`: Máquina de estados global, gestión de Event Groups.
- `app_nvs`: Capa de persistencia en Flash (NVS) para WiFi, credenciales, botones, MQTT y ajustes de sistema.
- `app_wifi`: Modos STA, AP y APSTA con fallback automático tras reintentos configurables.
- `app_web`: Servidor HTTP, interfaz de usuario embebida (`html_ui.h`) y endpoints REST con autenticación Basic Auth.
- `app_http`: Cliente HTTP/HTTPS asíncrono con TLS via `esp_crt_bundle`, ejecutado en tarea independiente.
- `app_mqtt`: Cliente MQTT oneshot — conecta, publica y desconecta por cada acción de botón.
- `app_buttons`: Polling GPIO anti-rebotes con lógica dinámica para Factory Reset y despacho HTTP/MQTT.
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
| `/api/btn?id=N` | `GET` | Recupera los ajustes actuales de un botón |
| `/api/btn` | `POST`| Guarda configuración del botón (target, método, tipo acción, etc.) |
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
