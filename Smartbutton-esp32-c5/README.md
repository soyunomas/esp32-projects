# SmartButton

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.1-blue)](https://github.com/espressif/esp-idf)
[![MCU](https://img.shields.io/badge/MCU-ESP32--C5-green)](https://www.espressif.com/en/products/socs/esp32-c5)
[![License](https://img.shields.io/badge/license-MIT-orange)](LICENSE)

Botón IoT dual configurable basado en ESP32-C5. Pulsa un botón físico y ejecuta una petición HTTP o publica un mensaje MQTT. Configurable fácilmente desde cualquier dispositivo vía portal web captivo.

## Características

- **2 botones físicos** con acción configurable cada uno (HTTP GET/POST o MQTT).
- **Nombres personalizables** — pon nombre propio a cada botón (ej: "Botón Rojo", "Alarma") desde la web.
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
- **Feedback visual RTOS** — LED RGB WS2812 con notificaciones FreeRTOS en tiempo real.
- **LEDs integrados en botones** — los LEDs rojo (GPIO 6) y verde (GPIO 8) del propio botón confirman visualmente la pulsación y el estado de conexión WiFi.
- **Botón de test** para probar las acciones HTTP y MQTT desde el propio panel web, con LED del botón encendido durante la prueba.
- **Factory reset dinámico** — mantén ambos botones (tiempo configurable desde la web), con LEDs alternados durante la pulsación y parpadeo rápido al ejecutar.
- **Indicación de no configurado** — al arrancar sin WiFi, ambos LEDs parpadean 5 veces rápido para avisar visualmente.
- **Actualizaciones OTA** — sube nuevos firmwares `.bin` directamente desde el panel de control.
- **Configuración avanzada** — reintentos WiFi, canal AP, tiempo de reset, timeouts de deep sleep y anti-rebote de botones ajustables desde la web.
- **Fallback automático** — si falla la conexión WiFi (reintentos configurables), vuelve a crear su propio AP.

## Hardware

### MCU

- **ESP32-C5** (RISC-V, WiFi 6 dual-band 2.4/5 GHz, BLE 5, Zigbee, Thread)
- Flash: **4MB**

### Pinout

| Función | GPIO | Configuración |
|---------|------|---------------|
| Botón 1 (Rojo) | **GPIO 4** | Pull-up interno, active-low |
| Botón 2 (Verde) | **GPIO 5** | Pull-up interno, active-low |
| LED Botón 1 (Rojo) | **GPIO 6** | Salida digital, LED integrado en botón |
| LED Botón 2 (Verde) | **GPIO 8** | Salida digital, LED integrado en botón |
| LED RGB | **GPIO 27** | Salida digital (Protocolo WS2812 / NeoPixel) |

### Esquema de conexión

*Nota: El ESP32-C5-DevKitC-1 incluye un LED RGB WS2812 integrado en GPIO 27.*

```text
ESP32-C5
┌──────────┐
│ GPIO 4   ├──── BTN1 (Rojo) ──── GND
│ GPIO 5   ├──── BTN2 (Verde) ──── GND
│ GPIO 6   ├──── LED BTN1 (Rojo, integrado en botón)
│ GPIO 8   ├──── LED BTN2 (Verde, integrado en botón)
│          │
│ GPIO 27  ├──── Data IN (LED WS2812 onboard)
│ 5V / 3V3 ├──── VDD     (LED WS2812)
│ GND      ├──── GND     (LED WS2812 / Común)
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
esptool.py --chip esp32c5 -b 460800 \
  --before default-reset --after hard-reset \
  write_flash --flash_mode dio --flash_size 4MB --flash_freq 80m \
  0x2000   firmware/bootloader.bin \
  0x8000   firmware/partition-table.bin \
  0xf000   firmware/ota_data_initial.bin \
  0x20000  firmware/smartbutton_c5.bin
```

> ⚠️ Sustituye el puerto si es necesario añadiendo `-p /dev/ttyUSB0` (Linux) o `-p COM3` (Windows).

> 💡 Si no detecta la placa, mantén pulsado el botón **BOOT** mientras conectas el USB.

---

## Compilar desde el código fuente

### Requisitos

- [ESP-IDF v6.1+](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c5/get-started/) configurado y activo.
- Placa ESP32-C5 (Flash: 4MB).

### Clonar y compilar

```bash
git clone https://github.com/soyunomas/esp32-projects.git
cd esp32-projects/Smartbutton-esp32-c5

# Configurar entorno ESP-IDF (si no lo tienes en tu .bashrc/.zshrc)
. $HOME/esp/esp-idf/export.sh

# Configurar target (solo la primera vez)
idf.py set-target esp32c5

# Compilar
idf.py build

# Flashear primera vez (borra NVS anterior para asegurar limpieza)
# Cambia /dev/ttyUSB0 por tu puerto correspondiente (COMx en Windows)
idf.py -p /dev/ttyUSB0 erase_flash flash monitor

# Flashear futuras actualizaciones por cable (conserva configuración WiFi/Botones)
idf.py -p /dev/ttyUSB0 flash monitor
```

Si al compilar da error de directorio de build incompatible, limpia y reconfigura:

```bash
idf.py fullclean
idf.py set-target esp32c5
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

### Comportamiento del LED RGB (WS2812 onboard)

El dispositivo cuenta con un sistema de estados RTOS que responde **al instante** mediante notificaciones FreeRTOS:

| Estado | Color LED | Patrón de parpadeo |
|--------|-----------|--------------------|
| **Modo AP (Configuración)** | 🔵 Azul | Parpadeo lento (1s) |
| **Conectando a WiFi** | 🟡 Amarillo | Parpadeo rápido (200ms) |
| **Conectado / Listo** | 🟢 Verde | **Fijo** |
| **Procesando Petición** | 🌐 Cyan | Pulso hiperrápido (150ms) |
| **Petición Exitosa (HTTP 2xx / MQTT OK)**| 🟢 Verde | Flash brillante de 1 segundo |
| **Error (Timeout / Fallo)** | 🔴 Rojo | Triple flash rápido |
| **Aviso de Reset Inminente** | 🔴 Rojo | Parpadeo muy rápido (100ms) |
| **Reset en curso** | 🔴 Rojo | **Fijo** |
| **Entrando en Deep Sleep** | 🔵 Azul | Flash breve de despedida |

### Comportamiento de los LEDs de botón (Rojo GPIO 6 / Verde GPIO 8)

Los LEDs integrados en los propios botones proporcionan feedback visual incluso con la caja cerrada:

| Situación | LED Rojo (BTN1) | LED Verde (BTN2) |
|-----------|-----------------|------------------|
| **Sin configurar (arranque)** | 🔴🟢 5 parpadeos rápidos simultáneos (100ms) | |
| **Conectando a WiFi** | 🔴🟢 Parpadeo alternado rojo↔verde (300ms) |  |
| **WiFi conectado** | Apagado | 🟢 2 flashes verdes y apaga |
| **WiFi fallido** | 🔴 3 flashes rojos y apaga | Apagado |
| **Botón 1 pulsado** | 🔴 Encendido mientras procesa | Apagado |
| **Botón 2 pulsado** | Apagado | 🟢 Encendido mientras procesa |
| **Test desde la web** | 🔴/🟢 LED del botón encendido durante la prueba | |
| **Reset (ambos botones)** | 🔴🟢 Parpadeo alternado (250ms) | |
| **Reset ejecutado** | 🔴🟢 5 parpadeos rápidos simultáneos (100ms) | |
| **Deep Sleep** | Apagado | Apagado |

### Deep Sleep (Modo bajo consumo)

Activable desde **Sistema > Deep Sleep** en el panel web. Ideal para dispositivos alimentados por batería.

- **Arranque en frío** (primera alimentación o reset): el dispositivo permanece despierto un tiempo configurable (por defecto **3 minutos**) para permitir acceso al panel de configuración y luego entra en deep sleep.
- **Despertar por botón**: al pulsar cualquiera de los dos botones, el dispositivo despierta, conecta a WiFi, ejecuta la acción del botón correspondiente y vuelve a dormir automáticamente.
- **Detección temprana del botón**: el GPIO se lee al inicio mismo del arranque (antes de cualquier inicialización) para identificar correctamente qué botón despertó el dispositivo.
- **Timeout de seguridad**: si la acción no se completa en el tiempo configurado (por defecto 30 segundos), el dispositivo vuelve a dormir igualmente.

### Modo Cliente Puro

Activable desde **Sistema > Modo Cliente Puro**. Oculta el punto de acceso propio (`SmartButton-XXXX`) una vez conectado exitosamente a tu red WiFi. Útil para mayor seguridad en entornos de producción. Si la conexión falla tras los reintentos configurados (por defecto 5), el AP se reactiva automáticamente como fallback.

### Factory Reset (Restaurar de fábrica)

Mantén **ambos botones pulsados simultáneamente**. Por defecto el tiempo es de 8 segundos (configurable en **Sistema > Avanzado > Tiempo de Reset**). 

- **Fase 1 (0 a T-3 seg):** El LED RGB parpadea lento y los LEDs de los botones se alternan (rojo↔verde cada 250ms) advirtiendo de la pulsación prolongada. Si sueltas los botones, la acción se cancela y se retoma la normalidad.
- **Fase 2 (Últimos 3 seg):** El LED RGB parpadeará en rojo rápidamente advirtiendo del borrado inminente. Los LEDs de botón siguen alternando.
- **Fase 3 (Fin del tiempo):** Ambos LEDs de botón parpadean 5 veces rápidamente, el LED RGB se queda en Rojo fijo, la placa se formatea por completo (borrando NVS) y vuelve a iniciar en Modo AP de fábrica.

También se puede realizar Factory Reset desde el panel web en **Sistema > Factory Reset**.

## Configuración de los Botones

Desde la web puedes ajustar parámetros avanzados por cada botón:

| Parámetro | Descripción |
|-----------|-------------|
| **Nombre** | Nombre personalizado del botón (hasta 30 caracteres). Se muestra en las pestañas de la UI |
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
| **Tiempo de Reset** | Segundos manteniendo ambos botones para factory reset | 3 – 60 | 8 |
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
- `app_led`: Feedback visual en tiempo real usando `led_strip` (WS2812) y notificaciones RTOS (`xTaskNotify`).
- `app_btn_leds`: Control de los LEDs integrados en los botones (GPIO 6/8) para feedback visual de pulsación y estado WiFi.
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
