# 🚀 Colección de Proyectos ESP32

Colección de soluciones profesionales de red y dispositivos IoT de alto rendimiento, desarrollados íntegramente en **ESP-IDF (v5.x/v6.x)**. Optimizados para microcontroladores ESP32-C3, ESP32-S3 y ESP32-C5.

---

## 🌐 Infraestructura de Red (WiFi Repeaters)

Sistemas avanzados para extender la cobertura inalámbrica con capacidades de enrutamiento (NAPT) y gestión profesional vía Web UI.

| Proyecto | Chip | Características Destacadas |
| :--- | :--- | :--- |
| [**esp32-s3-tailscale-marauder**](https://github.com/soyunomas/esp32-s3-tailscale-marauder) | ESP32-S3 | **Tailscale**, WPA2-Enterprise, NAPT, **DuckyScript HID**, Scheduler, Web UI |
| [**esp32-s3-tailscale-enterprise**](https://github.com/soyunomas/esp32-s3-tailscale-enterprise) | ESP32-S3 | **Tailscale (Subnet Router)**, WPA2-Enterprise, NAPT, Web UI |
| [**WiFi-Repeater-WPA2-Enterprise**](./WiFi-Repeater-WPA2-Enterprise) | ESP32-C3 | Soporte **EAP-PEAP/TTLS**, NAPT, Port Forwarding, Log Viewer |
| [**WiFi-Repeater**](./WiFi-Repeater) | ESP32-C3 | Extensor estándar WPA2-PSK, Interfaz Web, OTA, Ping Test |

### 🔍 ¿Cuál elegir?
*   **WiFi-Repeater (Estándar):** Ideal para el hogar. Extiende redes WiFi convencionales de forma rápida.
*   **WiFi-Repeater-WPA2-Enterprise:** Obligatorio para oficinas, universidades (eduroam) o redes corporativas que piden **usuario y contraseña**. Incluye **Port Forwarding** para acceder a dispositivos internos desde el router principal.
*   **esp32-s3-tailscale-enterprise:** La solución definitiva para **teletrabajo**. Combina el acceso a redes corporativas (WPA2-Ent) con **Tailscale**, permitiéndote acceder a tu red local desde cualquier parte del mundo sin tocar el router principal.
*   **esp32-s3-tailscale-marauder:** Plataforma avanzada de **auditoría y automatización HID**. Añade ejecución de **DuckyScript por USB**, almacenamiento de macros, múltiples layouts de teclado y automatización programable mediante scheduler, manteniendo además las capacidades de repetidor profesional y acceso remoto con **Tailscale**.

---

## 🔘 Dispositivos IoT (Smart Buttons & Sensores)

Nodos inteligentes configurables para control de escenas y automatización mediante disparadores HTTP, MQTT o WebSockets.

### Serie ESP32-C3 (Compactos y Eficientes)
| Proyecto | Entradas | Feedback Visual | Uso Ideal |
| :--- | :--- | :--- | :--- |
| [**Smartbutton-C3-Plus-5**](./Smartbutton-esp32-C3-Supermini-Plus-5) | 5 Botones | LED RGB WS2812 | Paneles de control de múltiples escenas. |
| [**Smartbutton-C3-SENSORS**](./Smartbutton-esp32-C3-Supermini-SENSORS) | 3 Entradas | LED Azul Onboard | Sensores de movimiento (**PIR, Radar**) y botones. |
| [**Smartbutton-C3-Supermini**](./Smartbutton-esp32-C3-Supermini) | 2 Botones | LED Azul Onboard | Control simple en formato miniatura (~2€). |
| [**WiFi-Motion-RSSI-C3-Supermini**](./WiFi-Motion-RSSI-C3-Supermini) | RSSI + CSI WiFi | Web móvil bilingüe, LED y Telegram | Detección de movimiento sin cámara, micrófono ni sensor externo. |

### Serie ESP32-C5 (WiFi 6 & Dual-Band)
| Proyecto | Protocolos | Feedback Visual | Ventaja Clave |
| :--- | :--- | :--- | :--- |
| [**Smartbutton-C5-ws**](./Smartbutton-esp32-c5-ws) | HTTP, MQTT, **WebSockets** | LED RGB + 2 LEDs | Latencia mínima y bandas 2.4/5GHz. |
| [**Smartbutton-C5**](./Smartbutton-esp32-c5) | HTTP, MQTT | LED RGB + 2 LEDs | Estabilidad WiFi 6 en entornos saturados. |

---

## 🛠️ Guía de Selección de Hardware

### 1. ¿WiFi 2.4GHz o 5GHz?
*   **ESP32-C3 / S3:** Operan principalmente en 2.4GHz. El **S3** ofrece mayor potencia de cómputo para tareas avanzadas como cifrado VPN, automatización HID y ejecución de macros complejas.
*   **ESP32-C5:** Soporta **5GHz y WiFi 6**. Elígelo si tienes un router moderno y quieres evitar interferencias o si necesitas la velocidad de los **WebSockets** para una respuesta instantánea.

### 2. VPN y Acceso Remoto
*   Si necesitas acceder a tus dispositivos desde fuera de casa de forma segura y sencilla, los modelos con **Tailscale (ESP32-S3)** son la opción recomendada, ya que actúan como puerta de enlace (Subnet Router) accesible desde cualquier lugar.

### 3. Automatización HID y Auditoría
*   **esp32-s3-tailscale-marauder:** Recomendado para entornos de laboratorio, pentesting ético y automatización avanzada. Soporta ejecución de **DuckyScript**, layouts internacionales, almacenamiento persistente de macros y ejecución programada desde interfaz web.

### 4. ¿Botón mecánico o Sensor de Presencia?
*   **Plus-5 / Supermini:** Diseñados para interacción humana directa. El modelo **Plus-5** destaca por su **LED RGB** que cambia de color según el estado (conectando, éxito, error).
*   **SENSORS:** Diseñado para automatización invisible. Incluye lógica de **estabilización** (evita falsos positivos al encender el sensor) y **cooldown** (tiempo de espera entre detecciones).
*   **WiFi-Motion-RSSI-C3-Supermini:** Detecta perturbaciones mediante **RSSI, CSI o ambas fuentes**, ofrece gráfica en tiempo real, calibración programable, avisos por Telegram y recuperación mediante portal cautivo. Detecta cambios compatibles con movimiento, no presencia estática certificada.

---

## 📋 Estándares del Repositorio

Todos los firmwares comparten una base técnica de nivel industrial:
*   **Provisionamiento:** Todos incluyen **Portal Cautivo**. El dispositivo crea su propia red WiFi para que lo configures desde el móvil sin tocar el código.
*   **Persistencia:** Configuración guardada en **NVS** (memoria no volátil).
*   **Actualizaciones:** Soporte **Dual OTA** (Over-The-Air) para actualizar el firmware desde la web de forma segura.
*   **Energía:** Gestión de **Deep Sleep** en todos los Smartbuttons para funcionamiento prolongado con baterías.
*   **Seguridad:** Panel de administración protegido por credenciales configurables.

---

*Mantenido por [soyunomas]. Todos los proyectos bajo licencia MIT.*
