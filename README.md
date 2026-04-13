# 🚀 Colección de Proyectos ESP32

Repositorio especializado en soluciones de red de alto rendimiento y dispositivos IoT avanzados utilizando el framework **ESP-IDF (v5.x/v6.x)**.

---

## 🌐 Infraestructura y Red (WiFi Repeaters)

Soluciones para extender la cobertura inalámbrica con capacidades de enrutamiento y gestión profesional.

| Proyecto | Chip | Características Destacadas |
| :--- | :--- | :--- |
| [**WiFi-Repeater-WPA2-Enterprise**](./WiFi-Repeater-WPA2-Enterprise) | ESP32-C3 | Soporte **EAP-PEAP/TTLS**, NAPT, Port Forwarding, Log Viewer |
| [**WiFi-Repeater**](./WiFi-Repeater) | ESP32-C3 | Extensor estándar WPA2-PSK, Interfaz Web, OTA, Ping Test |

### 🔍 ¿Cuál elegir?
*   **WiFi-Repeater (Estándar):** Elígelo para uso doméstico. Se conecta a routers normales con una contraseña simple (WPA2-PSK).
*   **WiFi-Repeater-WPA2-Enterprise:** Elígelo para entornos corporativos o universitarios (eduroam, oficinas). Permite conectar el repetidor a redes que requieren **usuario y contraseña** (EAP), además de permitir abrir puertos (**Port Forwarding**) para servicios internos.

---

## 🔘 Dispositivos IoT (Smart Buttons & Sensores)

Nodos de automatización configurables que disparan acciones HTTP, MQTT o WebSockets.

### Serie ESP32-C5 (Dual-Band WiFi 6)
| Proyecto | Protocolos | Extras |
| :--- | :--- | :--- |
| [**Smartbutton-c5-ws**](./Smartbutton-esp32-c5-ws) | HTTP, MQTT, **WebSockets** | Ultra-baja latencia, feedback LED RGB |
| [**Smartbutton-c5**](./Smartbutton-esp32-c5) | HTTP, MQTT | Fiabilidad en 2.4/5GHz, Deep Sleep |

### Serie ESP32-C3 (Miniatura)
| Proyecto | Entradas | Sensores Soportados |
| :--- | :--- | :--- |
| [**Smartbutton-C3-SENSORS**](./Smartbutton-esp32-C3-Supermini-SENSORS) | 3 Entradas | **PIR (HC-SR505), Radar (XYC-WB-DC)**, Botones |
| [**Smartbutton-C3-Supermini**](./Smartbutton-esp32-C3-Supermini) | 2 Botones | Botones mecánicos (diseño ultra compacto) |

---

## 🛠️ Guía de Selección de Hardware

### 1. WiFi: ¿C5 o C3?
*   **Elige ESP32-C5 si:** Necesitas conectar en la banda de **5GHz**, utilizas **WiFi 6** o requieres la latencia mínima de los **WebSockets** para domótica crítica.
*   **Elige ESP32-C3 si:** Buscas el menor coste (~2€) y el tamaño más reducido posible para ocultar el dispositivo en cajas de registro o sensores pequeños.

### 2. Funcionalidad: ¿Botón o Sensor?
*   **Smartbutton estándar:** Ideal para mandos a distancia, timbres o interruptores de escena.
*   **Smartbutton SENSORS:** Diseñado específicamente para seguridad y automatización de presencia. Incluye lógica de **estabilización** (espera a que el sensor se calibre tras encender) y **cooldown** (evita disparos falsos repetitivos).

---

## 📝 Estándares Técnicos del Repositorio

Todos los proyectos incluyen las siguientes capacidades de serie:
*   **Portal Cautivo:** Configuración inicial sin cables; el dispositivo crea su propia red para ser configurado.
*   **Persistencia NVS:** Todos los ajustes (WiFi, MQTT, GPIOs) se guardan en la memoria flash.
*   **Dual OTA:** Actualización de firmware vía web sin riesgo de bloqueo (si la actualización falla, vuelve a la versión anterior).
*   **Interfaz Dark Mode:** UI web profesional, rápida y adaptada a móviles (Vanilla JS).
*   **Seguridad:** Acceso al panel web protegido por credenciales configurables.

---
*Desarrollado con ❤️ por [soyunomas]. Licencia MIT.*
