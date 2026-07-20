# Plan por fases

## Decisión técnica basada en la literatura

El proyecto seguirá una estrategia de dos niveles:

1. **RSSI como línea base de bajo coste.** Debe demostrar detección reproducible de movimiento evidente con un ESP32-C3 y un punto de acceso, sin prometer presencia estática, localización ni clasificación de actividades.
2. **CSI como segunda fuente de detección.** Una vez caracterizado RSSI, se
   mantiene la misma interfaz del detector y se sustituye la fuente de
   características por CSI. El método con un ESP32-C3 y un router ya ha sido
   confirmado en la instalación real; la comparación ampliada con otras
   geometrías y, si aporta valor, dos ESP32-C3 continúa como trabajo posterior.

Esta decisión se apoya en los siguientes resultados:

- **RASID** demuestra detección pasiva robusta con RSSI, calibración de reposo, dispersión en ventanas y adaptación estadística. Su arquitectura suele beneficiarse de varios enlaces Wi-Fi.
- **WiRSSI** confirma que RSSI contiene información útil para sensing, aunque sus resultados de seguimiento emplean varias antenas y muestreo mucho más rápido que el disponible mediante un RSSI agregado convencional.
- **FIMD** y **PhaseMode** muestran que CSI proporciona mayor resolución y robustez que RSSI para detección fina.
- Los experimentos publicados con **ESP32 y CSI** demuestran presencia, reconocimiento de actividad y localización aproximada, pero dependen fuertemente de geometría, número de receptores, antenas y procesamiento.
- Espressif mantiene `esp-csi` y `esp-radar`, incluyendo una ruta compatible con ESP32-C3.

Por tanto, RSSI será un baseline medido y documentado, no el objetivo final de prestaciones.

## Línea alternativa de investigación matemática

El archivo [PLAN_ALTERNATIVO_PROCESADO_SENAL.md](PLAN_ALTERNATIVO_PROCESADO_SENAL.md) define una comparación A/B frente al detector inicial mediante derivadas multiescala, Teager-Kaiser, CUSUM, entropía ordinal, análisis de subespacios y Matrix Profile. Esta línea solo se adoptará si demuestra una mejora reproducible en sesiones no vistas y dentro de los límites del ESP32-C3.

## Fase 0 — Base reproducible (completada)

- ESP-IDF fijado a `v6.0.2`, objetivo `esp32c3`.
- Núcleo de detección desacoplado del Wi-Fi y de FreeRTOS.
- Pruebas host deterministas con GCC/CMake/CTest.
- Compilación de firmware en CI mediante la imagen oficial de Espressif.
- Configuración inicial con Kconfig y estructura versionada para persistencia NVS.

Criterio de salida: pruebas host verdes y firmware compilando en CI.

## Fase 1 — Captura RSSI y telemetría (completada)

- Conexión STA 2.4 GHz.
- Muestreo periódico con ahorro de energía Wi-Fi desactivado.
- Medición de la cadencia real de actualización del RSSI, diferenciándola de la frecuencia con la que se consulta la API.
- Salida CSV o JSON Lines.
- LED de estado opcional.
- Registro de RSSI, baseline, score, umbral, estado y transiciones.
- Registro de pérdidas de muestra, reconexiones, canal, BSSID y uptime.

Criterio de salida: sesión de 15 minutos sin reinicios, sin pérdidas sostenidas y con cadencia real caracterizada.

Resultado: completado; véase [PHASE1_RESULTS.md](PHASE1_RESULTS.md).

## Fase 2 — Detector RSSI configurable y comparativo (completada)

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

Resultado: completado; véase [PHASE2_RESULTS.md](PHASE2_RESULTS.md).

## Fase 3 — Configuración en tiempo de ejecución (completada)

- Portal de configuración local reutilizando patrones de los Smartbutton.
- Credenciales Wi-Fi, intervalo, ventana, calibración, algoritmo, sensibilidad e histéresis en NVS.
- Importación y exportación JSON.
- Restauración de fábrica.
- Validación de rangos y migración versionada de configuración.
- Endpoint de diagnóstico con configuración efectiva y estado del detector.

Criterio de salida: todos los parámetros modificables sin recompilar y conservados correctamente tras reinicio y actualización.

Resultado: completado; véase [PHASE3_RESULTS.md](PHASE3_RESULTS.md).

## Fase 4 — Protocolo experimental RSSI (cerrada: resultado inconcluso)

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

Resultado: infraestructura completada y campaña cerrada como inconclusa por
limitaciones de seguridad del montaje; véase
[PHASE4_RESULTS.md](PHASE4_RESULTS.md). No se declaran cumplidos ni incumplidos
los objetivos métricos con una muestra insuficiente.

## Fase 5 — CSI con ESP32-C3 (funcional; validación ampliada en curso)

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

## Fase 6 — Acceso, red y notificaciones (en curso)

Estas mejoras operativas se ejecutan de forma secuencial. No se inicia una
subfase mientras la anterior tenga errores de pruebas, compilación o placa.
No cambian el estado de validación de la detección CSI.

### Fase 6A — Acceso administrador

Estado: **completada y verificada en placa (2026-07-20)**.

- Usuario fijo `admin` y contraseña inicial `admin`.
- Cambio de contraseña autenticado desde la web.
- Contraseña almacenada como derivación con sal, nunca recuperable por la API.
- Sesión aleatoria con caducidad, cookie `HttpOnly` y limitación de intentos.
- Protección de configuración, diagnóstico, importación, exportación,
  calibración y futuras integraciones.
- Aviso visible mientras permanezca la contraseña predeterminada.

Criterio de salida: pruebas de credenciales, hash, sesión, expiración,
autorización de todos los endpoints, cambio de contraseña y validación física
después de reiniciar.

### Fase 6B — Selector Wi-Fi

Estado: **completada y verificada en placa (2026-07-20)**.

- Escaneo de redes cercano sin bloquear el muestreo de movimiento.
- Lista accesible con SSID, RSSI y estado abierto/protegido.
- Selección visual, contraseña oculta y conservación explícita de credenciales.
- Aplicación mediante guardado y reinicio, manteniendo el AP de recuperación.

Criterio de salida: escaneo real, selección y reconexión verificados sin
errores, pérdidas sostenidas ni exposición de contraseñas.

### Fase 6C — Telegram

Estado: **completada y verificada en placa (2026-07-20)**.

- Configuración protegida de habilitación, token de bot y `chat_id`.
- Token oculto en lectura y exportación normal.
- Envío HTTPS mediante la Bot API con validación de certificado.
- Cola no bloqueante, timeout, cooldown, deduplicación y contadores de salud.
- Botón de mensaje de prueba con respuesta clara en la interfaz.

Criterio de salida: pruebas host del formato y deduplicación, compilación,
rechazo seguro de configuración inválida y un mensaje real confirmado por el
usuario. No se registrará ni mostrará el token completo.

### Fase 6D — Integraciones posteriores

Solo se abordará después de las subfases anteriores y de disponer de un
detector validado para el uso declarado:

- MQTT y HTTP webhook.
- Home Assistant mediante MQTT Discovery.
- OTA con rollback.
- Watchdog.
- Métricas adicionales de heap y calidad de señal.

## Fase 7 — CSI, portabilidad e interfaz bilingüe (en curso)

Estas subfases son secuenciales. No se inicia una mientras la anterior tenga
errores de pruebas, compilación o verificación en placa.

### Fase 7A — CSI como fuente soportada

Estado: **completada y verificada en placa (2026-07-20)**.

- Retirar las etiquetas `experimental` y `sombra` de la interfaz y la
  documentación de CSI.
- Mantener visibles score, umbral, estado, hitos y contadores de calidad CSI.
- Documentar que su respuesta depende de router, tráfico, canal y geometría sin
  presentarlo como una limitación de estado del producto.

Criterio de salida: textos coherentes, pruebas host verdes, firmware compilado,
instalado y selector CSI verificado en placa sin errores.

### Fase 7B — Recuperación Wi-Fi al cambiar de ubicación

Estado: **completada y verificada en placa (2026-07-20)**.

- Conservar el fallback existente: tras fallar la red guardada, crear
  `WiFi-Motion-XXXXXX` en `192.168.4.1` sin borrar la configuración.
- Añadir DNS cautivo, siguiendo el patrón de los proyectos Smartbutton, para
  redirigir al portal sin exigir que el usuario recuerde la IP.
- Permitir que una pulsación de BOOT de 5 segundos, realizada después del
  arranque y confirmada al soltar, reinicie una vez en modo recuperación sin
  borrar ninguna configuración. Una pulsación corta conserva el marcador.
- Verificar escaneo, cambio de SSID, guardado, reinicio y reconexión desde el AP
  de recuperación.
- Adaptar el portal a móvil: formularios a una columna, controles táctiles de
  al menos 44 px, gráfica sin desbordamiento, redes apiladas y barra de guardado
  no bloqueante. Validado en viewport de 360 px y confirmado en el teléfono real.
- Mantener el reset de fábrica web como acción separada y destructiva.

Criterio de salida: solicitar recuperación con BOOT, conectar un teléfono u
ordenador al AP, abrir el portal sin desbordamientos en móvil, seleccionar una
red y recuperar el modo STA.

### Fase 7C — Interfaz web en inglés y español

Estado: **completada y verificada en placa (2026-07-20)**.

- Inglés como idioma predeterminado, sin depender del idioma del navegador.
- Selector visible `English / Español` también en la pantalla de acceso.
- Preferencia guardada localmente en el navegador.
- Traducción de etiquetas, ayudas, estados, errores y confirmaciones visibles.
- Actualización de `lang`, nombres accesibles y formato de horas al cambiar.

Criterio de salida: diccionarios completos, JavaScript válido, navegación por
teclado, persistencia de preferencia y recorridos completos en ambos idiomas.

Verificación: diccionarios con claves idénticas, 12/12 suites host, compilación
ESP-IDF, instalación en placa, primera apertura inglesa, cambio a español y
persistencia tras recargar comprobados con navegador real en viewport móvil.

### Fase 7D — Recuperación adicional mediante RESET

Estado: **no necesaria por ahora**.

- RESET mantiene su función normal de reiniciar.
- Solo se evaluaría una secuencia de varios RESET si BOOT y el AP cautivo no
  cubrieran algún caso real.
- Se evita contar reinicios por defecto para no confundir cortes de alimentación
  con una orden de recuperación.

Criterio de salida, si se reabre: activación inequívoca y sin falsos positivos
por alimentación inestable.

### Fase 8 — Paquete publicable y firmware precompilado

Estado: **completada y verificada (2026-07-20)**.

- Usar `README.md` en inglés como portada predeterminada y mantener la
  documentación completa en español en `README-ES.md`.
- Incluir binarios separados para actualizar conservando NVS y una imagen
  combinada para una instalación de fábrica limpia.
- Documentar offsets, diferencias destructivas, compilación desde fuentes,
  flasheo en Linux, macOS y Windows, versión exacta de ESP-IDF y recuperación.
- Añadir scripts de flasheo, información de compilación y hashes SHA-256.
- Sincronizar código, pruebas, documentación y firmware en `github/`, sin
  credenciales, capturas locales ni artefactos generados por ESP-IDF.

Criterio de salida: 12/12 suites host, binario ESP32-C3 válido, hashes correctos,
imagen combinada verificada por offsets, enlaces Markdown válidos y carpeta
`github/` autocontenida.

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
