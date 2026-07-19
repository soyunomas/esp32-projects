# Plan por fases

## Decisión técnica basada en la literatura

El proyecto seguirá una estrategia de dos niveles:

1. **RSSI como línea base de bajo coste.** Debe demostrar detección reproducible de movimiento evidente con un ESP32-C3 y un punto de acceso, sin prometer presencia estática, localización ni clasificación de actividades.
2. **CSI como siguiente hito experimental.** Una vez caracterizado RSSI, se mantendrá la misma interfaz del detector y se sustituirá la fuente de características por CSI, primero con un ESP32-C3 y un router y después, si es necesario, con dos ESP32-C3.

Esta decisión se apoya en los siguientes resultados:

- **RASID** demuestra detección pasiva robusta con RSSI, calibración de reposo, dispersión en ventanas y adaptación estadística. Su arquitectura suele beneficiarse de varios enlaces Wi-Fi.
- **WiRSSI** confirma que RSSI contiene información útil para sensing, aunque sus resultados de seguimiento emplean varias antenas y muestreo mucho más rápido que el disponible mediante un RSSI agregado convencional.
- **FIMD** y **PhaseMode** muestran que CSI proporciona mayor resolución y robustez que RSSI para detección fina.
- Los experimentos publicados con **ESP32 y CSI** demuestran presencia, reconocimiento de actividad y localización aproximada, pero dependen fuertemente de geometría, número de receptores, antenas y procesamiento.
- Espressif mantiene `esp-csi` y `esp-radar`, incluyendo una ruta compatible con ESP32-C3.

Por tanto, RSSI será un baseline medido y documentado, no el objetivo final de prestaciones.

## Fase 0 — Base reproducible (completada)

- ESP-IDF fijado a `v6.0.2`, objetivo `esp32c3`.
- Núcleo de detección desacoplado del Wi-Fi y de FreeRTOS.
- Pruebas host deterministas con GCC/CMake/CTest.
- Compilación de firmware en CI mediante la imagen oficial de Espressif.
- Configuración inicial con Kconfig y estructura versionada para persistencia NVS.

Criterio de salida: pruebas host verdes y firmware compilando en CI.

## Fase 1 — Captura RSSI y telemetría

- Conexión STA 2.4 GHz.
- Muestreo periódico con ahorro de energía Wi-Fi desactivado.
- Medición de la cadencia real de actualización del RSSI, diferenciándola de la frecuencia con la que se consulta la API.
- Salida CSV o JSON Lines.
- LED de estado opcional.
- Registro de RSSI, baseline, score, umbral, estado y transiciones.
- Registro de pérdidas de muestra, reconexiones, canal, BSSID y uptime.

Criterio de salida: sesión de 15 minutos sin reinicios, sin pérdidas sostenidas y con cadencia real caracterizada.

## Fase 2 — Detector RSSI configurable y comparativo

Se implementarán varios extractores de características sobre la misma ventana de muestras:

- Media de diferencias absolutas, ya disponible como baseline inicial.
- Desviación estándar.
- Varianza muestral.
- Rango máximo-mínimo.
- Desviación absoluta mediana.

Además:

- Umbral adaptativo por media y desviación estándar.
- Alternativa robusta por mediana, MAD o percentiles.
- Actualización lenta del baseline únicamente durante períodos clasificados como reposo.
- Histéresis y confirmación temporal independientes para activación y liberación.
- Perfiles `low`, `balanced` y `high`.
- Posibilidad de fijar el algoritmo y todos sus parámetros desde configuración.

Criterio de salida: todos los algoritmos pasan el mismo conjunto de pruebas sintéticas y producen telemetría comparable.

## Fase 3 — Configuración en tiempo de ejecución

- Portal de configuración local reutilizando patrones de los Smartbutton.
- Credenciales Wi-Fi, intervalo, ventana, calibración, algoritmo, sensibilidad e histéresis en NVS.
- Importación y exportación JSON.
- Restauración de fábrica.
- Validación de rangos y migración versionada de configuración.
- Endpoint de diagnóstico con configuración efectiva y estado del detector.

Criterio de salida: todos los parámetros modificables sin recompilar y conservados correctamente tras reinicio y actualización.

## Fase 4 — Protocolo experimental RSSI

- Herramienta Python para capturar sesiones etiquetadas: habitación vacía, cruce del enlace, movimiento lateral, movimiento fuera del trayecto y perturbaciones no humanas.
- Pruebas con varias posiciones, alturas, orientaciones y distancias entre AP y ESP32.
- Comparación de algoritmos y perfiles sobre las mismas capturas.
- Separación estricta entre datos de ajuste y datos de evaluación.
- Dataset pequeño reproducible y sin datos personales.

Métricas obligatorias:

- Sensibilidad o recall por evento.
- Precisión y F1.
- Falsos positivos por hora.
- Latencia de activación.
- Tiempo de liberación.
- Porcentaje de muestras repetidas o sin actualización real.
- Estabilidad del baseline.

Objetivo inicial, limitado al trayecto de prueba:

- Sensibilidad >= 90 % para cruces definidos.
- <= 1 falso positivo por hora en reposo.
- Latencia mediana <= 1 segundo.

Si RSSI no alcanza esos objetivos después de ajustar geometría y detector, se documentará el resultado negativo y se avanzará a CSI sin sobreadaptar el algoritmo.

## Fase 5 — CSI con ESP32-C3

### Fase 5A — Un ESP32-C3 y un router

- Integrar `esp-csi` o `esp-radar` como fuente alternativa.
- Generar tráfico controlado para obtener muestras CSI de forma estable.
- Mantener la interfaz común de salida: timestamp, score, threshold, state y calidad de muestra.
- Extraer características simples antes de introducir machine learning: amplitud, varianza por subportadora, energía temporal y componentes principales.
- Repetir exactamente el protocolo experimental de la fase RSSI.

### Fase 5B — Dos ESP32-C3

- Un nodo transmisor y otro receptor.
- Control explícito de canal, tasa de paquetes, potencia y geometría.
- Sincronización y etiquetado de sesiones.
- Comparación directa con un único receptor y con RSSI.

Criterio de salida: informe reproducible RSSI frente a CSI con igual ubicación, eventos, métricas y criterios de evaluación.

## Fase 6 — Integraciones y operación

Solo se abordará después de disponer de un detector validado:

- MQTT y HTTP webhook.
- Home Assistant mediante MQTT Discovery.
- OTA con rollback.
- Watchdog.
- Métricas de salud: uptime, reconexiones, heap, canal, BSSID y calidad de señal.
- Eventos con cooldown y deduplicación.

## Alcance explícitamente excluido del POC RSSI

La primera versión RSSI no se presentará como solución fiable para:

- Detectar una persona completamente inmóvil.
- Cubrir uniformemente una habitación completa.
- Contar, identificar o localizar personas.
- Clasificar actividades.
- Uso como alarma de seguridad certificada.

## Referencias técnicas

- Kosba et al., **RASID: A Robust WLAN Device-Free Passive Motion Detection System**: https://arxiv.org/abs/1105.6084
- **Rethinking RSSI for WiFi Sensing / WiRSSI**: https://www.nature.com/articles/s44459-026-00053-y
- Xiao et al., **FIMD: Fine-Grained Device-Free Motion Detection**: https://doi.org/10.1109/ICPADS.2012.49
- **PhaseMode: A Robust Passive Intrusion Detection System**: https://doi.org/10.1155/2018/8243905
- Strohmayer and Kampel, **WiFi CSI-Based Long-Range Through-Wall Human Activity Recognition with the ESP32**: https://repositum.tuwien.at/handle/20.500.12708/190653
- **From CSI to Coordinates: Device-Free Indoor Localization with ESP32 Nodes**: https://www.mdpi.com/1999-5903/17/9/395
- Espressif, **ESP-CSI**: https://github.com/espressif/esp-csi
- Espressif, **ESP-CSI Solution / ESP-Radar**: https://docs.espressif.com/projects/esp-techpedia/en/latest/esp-friends/solution-introduction/esp-csi/esp-csi-solution.html
