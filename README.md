# 🚀 Colección de Proyectos ESP32

Colección de soluciones profesionales de red y dispositivos IoT de alto rendimiento, desarrollados íntegramente en **ESP-IDF (v5.x/v6.x)**. Optimizados para microcontroladores ESP32-C3 y ESP32-C5.

---

## 🌐 Infraestructura de Red (WiFi Repeaters)

Sistemas avanzados para extender la cobertura inalámbrica con capacidades de enrutamiento (NAPT) y gestión profesional vía Web UI.

| Proyecto | Chip | Características Destacadas |
| :--- | :--- | :--- |
| [**WiFi-Repeater-WPA2-Enterprise**](./WiFi-Repeater-WPA2-Enterprise) | ESP32-C3 | Soporte **EAP-PEAP/TTLS**, NAPT, Port Forwarding, Log Viewer |
| [**WiFi-Repeater**](./WiFi-Repeater) | ESP32-C3 | Extensor estándar WPA2-PSK, Interfaz Web, OTA, Ping Test |

### 🔍 ¿Cuál elegir?
*   **WiFi-Repeater (Estándar):** Ideal para el hogar. Extiende redes WiFi convencionales de forma rápida.
*   **WiFi-Repeater-WPA2-Enterprise:** Obligatorio para oficinas, universidades (eduroam) o redes corporativas que piden **usuario y contraseña**. Incluye **Port Forwarding** para acceder a dispositivos internos desde el router principal.

---

## 🔘 Dispositivos IoT (Smart Buttons & Sensores)

Nodos inteligentes configurables para control de escenas y automatización mediante disparadores HTTP, MQTT o WebSockets.

### Serie ESP32-C3 (Compactos y Eficientes)
| Proyecto | Entradas | Feedback Visual | Uso Ideal |
| :--- | :--- | :--- | :--- |
| [**Smartbutton-C3-Plus-5**](./Smartbutton-esp32-C3-Supermini-Plus-5) | 5 Botones | LED RGB WS2812 | Paneles de control de múltiples escenas. |
| [**Smartbutton-C3-SENSORS**](./Smartbutton-esp32-C3-Supermini-SENSORS) | 3 Entradas | LED Azul Onboard | Sensores de movimiento (**PIR, Radar**) y botones. |
| [**Smartbutton-C3-Supermini**](./Smartbutton-esp32-C3-Supermini) | 2 Botones | LED Azul Onboard | Control simple en formato miniatura (~2€). |

### Serie ESP32-C5 (WiFi 6 & Dual-Band)
| Proyecto | Protocolos | Feedback Visual | Ventaja Clave |
| :--- | :--- | :--- | :--- |
| [**Smartbutton-C5-ws**](./Smartbutton-esp32-c5-ws) | HTTP, MQTT, **WebSockets** | LED RGB + 2 LEDs | Latencia mínima y bandas 2.4/5GHz. |
| [**Smartbutton-C5**](./Smartbutton-esp32-c5) | HTTP, MQTT | LED RGB + 2 LEDs | Estabilidad WiFi 6 en entornos saturados. |

---

## 🛠️ Guía de Selección de Hardware

### 1. ¿WiFi 2.4GHz o 5GHz?
*   **ESP32-C3:** Solo 2.4GHz. Excelente alcance y precio imbatible. Perfecto para la mayoría de automatizaciones.
*   **ESP32-C5:** Soporta **5GHz y WiFi 6**. Elígelo si tienes un router moderno y quieres evitar interferencias o si necesitas la velocidad de los **WebSockets** para una respuesta instantánea.

### 2. ¿Botón mecánico o Sensor de Presencia?
*   **Plus-5 / Supermini:** Diseñados para interacción humana directa. El modelo **Plus-5** destaca por su **LED RGB** que cambia de color según el estado (conectando, éxito, error).
*   **SENSORS:** Diseñado para automatización invisible. Incluye lógica de **estabilización** (evita falsos positivos al encender el sensor) y **cooldown** (tiempo de espera entre detecciones).

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
