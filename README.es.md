# Proyectos ESP32

[English](README.md) | **Español**

Colección de firmwares ESP-IDF para placas ESP32-C3, ESP32-C5 y ESP32-S3. El
repositorio contiene tres tipos de dispositivos claramente diferenciados:

- repetidores Wi-Fi que comparten una red existente mediante un punto de acceso
  nuevo;
- botones configurables y nodos para sensores digitales que lanzan acciones
  HTTP, MQTT o WebSocket;
- dispositivos experimentales que detectan cambios en la propagación Wi-Fi
  compatibles con movimiento.

Cada proyecto tiene sus propios requisitos de hardware, instrucciones de
flasheo, guía de configuración y limitaciones. La mayoría se incluye como una
carpeta de este repositorio; los proyectos ESP32-S3 con Tailscale enlazan a sus
propios repositorios.

## Elegir un proyecto

| Quiero... | Proyecto recomendado | Placa |
|---|---|---|
| Ampliar una red Wi-Fi doméstica WPA2-Personal | [WiFi-Repeater](./WiFi-Repeater) | ESP32-C3 SuperMini |
| Ampliar una red empresarial que pide usuario y contraseña | [WiFi-Repeater-WPA2-Enterprise](./WiFi-Repeater-WPA2-Enterprise) | ESP32-C3 SuperMini |
| Ampliar una red empresarial y acceder remotamente a sus dispositivos mediante Tailscale | [esp32-s3-tailscale-enterprise](https://github.com/soyunomas/esp32-s3-tailscale-enterprise) | ESP32-S3 |
| Combinar el repetidor con Tailscale y automatización USB HID programable para laboratorios autorizados | [esp32-s3-tailscale-marauder](https://github.com/soyunomas/esp32-s3-tailscale-marauder) | ESP32-S3 |
| Lanzar dos acciones HTTP o MQTT con botones físicos | [Smartbutton ESP32-C3 SuperMini](./Smartbutton-esp32-C3-Supermini) | ESP32-C3 SuperMini |
| Lanzar cinco acciones HTTP o MQTT | [Smartbutton ESP32-C3 SuperMini Plus 5](./Smartbutton-esp32-C3-Supermini-Plus-5) | ESP32-C3 SuperMini Plus |
| Conectar sensores PIR, radares de microondas, botones u otros sensores con salida digital | [Smartbutton ESP32-C3 Sensors](./Smartbutton-esp32-C3-Supermini-SENSORS) | ESP32-C3 SuperMini |
| Usar un nodo de dos botones con Wi-Fi 6 de doble banda | [Smartbutton ESP32-C5](./Smartbutton-esp32-c5) | ESP32-C5 |
| Añadir acciones WebSocket al nodo ESP32-C5 | [Smartbutton ESP32-C5 WS](./Smartbutton-esp32-c5-ws) | ESP32-C5 |
| Experimentar con la detección de movimiento mediante cambios en el enlace de radio entre un router y un ESP32 | [WiFi Motion RSSI ESP32-C3](./WiFi-Motion-RSSI-C3-Supermini) | ESP32-C3 SuperMini |
| Hacer el mismo experimento con una pantalla local pequeña | [WiFi Motion RSSI ESP32-C3 OLED](./WiFi-Motion-RSSI-ESP32-C3-OLED-0.42) | 01Space ESP32-C3 0.42 OLED |
| Hacer el experimento del enlace de radio en un ESP32-S3 | [WiFi Motion RSSI ESP32-S3](./WiFi-Motion-RSSI-ESP32-S3) | ESP32-S3 |
| Observar varios puntos de acceso cercanos sin conectarse a ellos | [WiFi Motion RSSI C3 AP Scanner](./WiFi-Motion-RSSI-C3-AP-Scanner) | ESP32-C3 SuperMini |

## Repetidores Wi-Fi

Estos proyectos se conectan como estación a una red Wi-Fi existente y crean
otro punto de acceso para los dispositivos cliente. NAPT enruta el tráfico
entre ambas redes.

### WiFi-Repeater

[WiFi-Repeater](./WiFi-Repeater) amplía una red WPA2-Personal convencional.
Incluye portal cautivo de configuración, panel web adaptable, búsqueda de
redes, pruebas de conectividad, configuración persistente y actualización del
firmware desde la web.

### WiFi-Repeater-WPA2-Enterprise

[WiFi-Repeater-WPA2-Enterprise](./WiFi-Repeater-WPA2-Enterprise) es la variante
para redes con autenticación EAP-PEAP o EAP-TTLS, habituales en empresas y
centros educativos. También añade redirección de puertos TCP/UDP y un visor de
registros en la web.

### esp32-s3-tailscale-enterprise

[esp32-s3-tailscale-enterprise](https://github.com/soyunomas/esp32-s3-tailscale-enterprise)
combina un repetidor WPA2-Enterprise para ESP32-S3 con NAPT, interfaz web y
enrutamiento de subred mediante Tailscale. Está pensado para ampliar una red
Wi-Fi empresarial y, al mismo tiempo, permitir el acceso a los dispositivos
situados detrás del ESP32 desde una red Tailscale.

### esp32-s3-tailscale-marauder

[esp32-s3-tailscale-marauder](https://github.com/soyunomas/esp32-s3-tailscale-marauder)
amplía el repetidor empresarial ESP32-S3 y sus funciones de Tailscale con
automatización USB HID programable. Admite DuckyScript, macros almacenadas,
varias distribuciones de teclado y ejecución programada para entornos
autorizados de laboratorio, pruebas y automatización.

## Botones inteligentes y entradas para sensores

Estos dispositivos se conectan a tu red Wi-Fi y ejecutan una acción configurada
cuando cambia una entrada física. Según el proyecto, una entrada puede enviar
una petición HTTP GET/POST, publicar un mensaje MQTT o enviar datos por
WebSocket.

| Proyecto | Entradas | Acciones | Indicadores locales |
|---|---:|---|---|
| [ESP32-C3 SuperMini](./Smartbutton-esp32-C3-Supermini) | 2 botones | HTTP, MQTT | LED azul integrado |
| [ESP32-C3 SuperMini Plus 5](./Smartbutton-esp32-C3-Supermini-Plus-5) | 5 botones | HTTP, MQTT | LED RGB WS2812 integrado |
| [ESP32-C3 Sensors](./Smartbutton-esp32-C3-Supermini-SENSORS) | 3 entradas digitales configurables | HTTP, MQTT | LED azul integrado |
| [ESP32-C5](./Smartbutton-esp32-c5) | 2 botones | HTTP, MQTT | LED RGB WS2812 y dos LED en los botones |
| [ESP32-C5 WS](./Smartbutton-esp32-c5-ws) | 2 botones | HTTP, MQTT, WebSocket | LED RGB WS2812 y dos LED en los botones |

La variante **Sensors** no es por sí sola un detector PIR ni un radar. Es una
interfaz ESP32-C3 para conectar hasta tres dispositivos externos con salida
digital, como sensores PIR, radares de microondas o botones mecánicos. Añade un
periodo de estabilización al arrancar y un tiempo de espera entre activaciones
para reducir disparos repetidos.

## Detección experimental de movimiento mediante Wi-Fi

Estos proyectos miden cómo cambia una señal de radio Wi-Fi con el tiempo. El
movimiento de una persona o un objeto puede alterar los reflejos y recorridos
entre un transmisor y el ESP32; el firmware clasifica los cambios
suficientemente grandes como eventos compatibles con movimiento.

> [!IMPORTANT]
> Estos dispositivos detectan **cambios en la propagación de radio**, no
> personas. No pueden identificar ni contar ocupantes, pueden no detectar a una
> persona inmóvil y pueden reaccionar a otros cambios del entorno. Son
> experimentos, no sensores de presencia certificados ni alarmas de seguridad.

### Variantes conectadas al router

Las siguientes variantes se conectan a una red Wi-Fi configurada y analizan
RSSI, CSI o ambas medidas. Incluyen calibración, interfaz web bilingüe, gráficas
en directo, avisos por Telegram y telemetría experimental.

| Proyecto | Diferencia principal |
|---|---|
| [ESP32-C3 SuperMini](./WiFi-Motion-RSSI-C3-Supermini) | Implementación base y compacta para ESP32-C3 |
| [ESP32-C3 OLED 0.42](./WiFi-Motion-RSSI-ESP32-C3-OLED-0.42) | Añade una OLED de 72×40 para ver localmente el estado, la IP, el RSSI y los scores |
| [ESP32-S3](./WiFi-Motion-RSSI-ESP32-S3) | Adaptación para placas ESP32-S3 convencionales, con consola y telemetría por UART0 |

### Escáner autónomo de puntos de acceso

[WiFi Motion RSSI C3 AP Scanner](./WiFi-Motion-RSSI-C3-AP-Scanner) busca cambios
compatibles con movimiento en el RSSI de varios puntos de acceso Wi-Fi
cercanos. **No** se conecta a esas redes y no necesita ni almacena sus
contraseñas. En su lugar:

- selecciona automática o manualmente hasta ocho SSID y sigue sus BSSID;
- aprende una referencia a partir de puntos de acceso cercanos estables;
- combina las desviaciones de señal mediante un umbral adaptativo;
- crea su propia red Wi-Fi local con un panel cautivo;
- informa de una cobertura insuficiente sin confundirla con movimiento.

Esta variante funciona de forma local e incluye firmware listo para grabar por
USB. No ofrece avisos por Telegram ni actualizaciones OTA.

## Antes de flashear

Abre el README del proyecto elegido antes de conectar o grabar una placa. Los
pinouts, versiones de ESP-IDF, offsets de flasheo, binarios precompilados,
credenciales iniciales y métodos de actualización varían entre proyectos.

En general:

- la configuración se guarda en NVS cuando así lo indica el proyecto;
- algunos firmwares admiten OTA y otros deben actualizarse por USB;
- los botones inteligentes pueden usar deep sleep, pero los experimentos de
  movimiento por Wi-Fi deben permanecer activos para tomar medidas;
- conviene cambiar las credenciales predeterminadas del punto de acceso y del
  panel web antes del uso habitual.

## Licencia

Este repositorio se distribuye bajo la [licencia MIT](LICENSE). Comprueba
también la licencia incluida dentro de cada proyecto cuando exista.
