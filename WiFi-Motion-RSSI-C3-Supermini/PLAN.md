# Plan por fases

## Fase 0 — Base reproducible (esta entrega)

- ESP-IDF fijado a `v6.0.2`, objetivo `esp32c3`.
- Núcleo de detección desacoplado del Wi-Fi y de FreeRTOS.
- Pruebas host deterministas con GCC/CMake/CTest.
- Compilación de firmware en CI mediante la imagen oficial de Espressif.
- Configuración inicial con Kconfig y estructura versionada para persistencia NVS.

Criterio de salida: pruebas host verdes y firmware compilando en CI.

## Fase 1 — Captura RSSI y telemetría

- Conexión STA 2.4 GHz.
- Muestreo periódico con ahorro de energía Wi-Fi desactivado.
- Salida CSV o JSON Lines.
- LED de estado opcional.
- Registro de baseline, score, umbral y transiciones.

Criterio de salida: sesión de 15 minutos sin reinicios y sin pérdidas sostenidas de muestras.

## Fase 2 — Configuración en tiempo de ejecución

- Portal de configuración local reutilizando patrones de los Smartbutton.
- Credenciales Wi-Fi, intervalo, ventana, calibración, sensibilidad e histéresis en NVS.
- Importación/exportación JSON y restauración de fábrica.
- Validación de rangos y migración versionada de configuración.

Criterio de salida: todos los parámetros modificables sin recompilar ni perder compatibilidad tras reinicio.

## Fase 3 — Calibración experimental

- Herramienta Python para capturar sesiones etiquetadas: vacío, movimiento y perturbaciones.
- Métricas: sensibilidad, falsos positivos por hora, latencia y tiempo de recuperación.
- Perfiles de habitación y selección automática de umbral.
- Dataset pequeño reproducible, sin datos personales.

Criterio de salida: sensibilidad >= 90 % en el trayecto de prueba y <= 1 falso positivo/hora en reposo.

## Fase 4 — Integraciones

- MQTT y HTTP webhook.
- Home Assistant mediante MQTT Discovery.
- OTA con rollback y watchdog.
- Métricas de salud: uptime, reconexiones, heap y calidad de señal.

## Fase 5 — CSI opcional

- Evaluar CSI con uno o dos ESP32-C3.
- Mantener la misma interfaz del detector y sustituir únicamente la fuente de características.
- Comparativa RSSI frente a CSI con el mismo protocolo experimental.
