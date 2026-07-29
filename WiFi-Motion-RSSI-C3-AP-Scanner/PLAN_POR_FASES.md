# Plan por fases

El plan parte del estudio de viabilidad. Cada fase termina con una puerta de
decisión; no se construirá la interfaz completa ni Telegram antes de demostrar
que la señal base aporta información útil.

## Fase 0 — Especificación medible

### Objetivo

Convertir “detectar movimiento” en un experimento reproducible.

### Entregables

- protocolo con posición fija del C3 y escenarios etiquetados;
- definición previa de métricas y umbrales de aceptación;
- formato versionado de observaciones;
- inventario de canales y AP presentes;
- decisión de escaneo pasivo inicial y variante activa de comparación;
- ADR de modos del radio y política de privacidad.

### Pruebas

- habitación vacía prolongada;
- entradas, salidas y movimiento a distintas distancias;
- AP apagado o ausente;
- tráfico Wi-Fi alto/bajo;
- repetición en al menos dos ubicaciones.

### Puerta de salida

El protocolo permite repetir la prueba sin decisiones manuales durante la
captura y separa etiquetas reales de predicciones.

## Fase 1 — Sonda de escaneo y captura de datos

**Estado: firmware, captura, primer par vacío/movimiento y variante limitada a
canales 1/6 implementados; además se inició de forma aislada el núcleo de
selección de referencias. Quedan escenarios de control antes de cerrar la
puerta de viabilidad.**

### Objetivo

Medir qué ofrece realmente el ESP32-C3 desconectado.

### Implementación

- proyecto ESP-IDF mínimo para C3 SuperMini;
- STA iniciado sin llamar a `esp_wifi_connect()`;
- escaneo asíncrono pasivo;
- registros por BSSID: SSID, canal, RSSI, tiempo y duración del ciclo;
- cola desde callbacks hacia una tarea de aplicación;
- salida CSV/JSON Lines por USB serie;
- botón físico para marcar eventos reutilizando `event_marker`;
- contadores de errores, memoria libre y watchdog.

Implementado con esquema de telemetría `wifi_ap_scan/v1`, SSID exacto en
hexadecimal, marcador BOOT y empaquetado reproducible en `firmware/`.

### Experimentos

- barrido completo: completado;
- canales limitados a los descubiertos: completado para 1 y 6;
- comparación pasivo/activo;
- tiempos de permanencia distintos dentro de límites documentados;
- prueba continua de estabilidad: completada.

### Puerta de decisión

Avanzar si la cadencia y cobertura son suficientemente estables para crear
ventanas temporales y existe separación medible entre vacío y movimiento. Si no:

1. probar sniffer de beacons en canales seleccionados;
2. valorar CSI promiscuo como investigación;
3. cerrar el proyecto si ninguna variante separa los escenarios.

## Fase 2 — Referencias manuales y automáticas

### Objetivo

Crear un modelo reproducible de AP de referencia.

### Componentes nuevos

- `scan_scheduler`: implementado inicialmente para barrido completo o lista de
  canales sin duplicados;
- `observation_store`: tabla limitada por BSSID implementada y probada;
- `reference_selector`: núcleo manual/automático implementado y probado;
- `radio_coordinator`: único propietario de cambios de modo;
- esquema NVS con versión y CRC32/validación: implementado y probado.

`observation_store` separa muestras almacenadas de presencia observada, calcula
mediana y MAD, y marca cambios de SSID o canal. El almacén y el selector ya
están conectados al bucle: la calibración usa todos los canales y el planificador
pasa después a los canales seleccionados. El resultado se persiste y se
restaura desde NVS; si el blob falta, está corrupto o se fuerza recalibración,
vuelve de forma segura al barrido completo. El modo automático/manual y hasta
dos SSID ya tienen un esquema NVS versionado y validado; falta exponerlo en el
portal.

### Selección manual

- aceptar lista de SSID;
- resolver y conservar sus BSSID durante calibración;
- mostrar nuevos BSSID como candidatos, sin incorporarlos automáticamente.

### Selección automática

- calcular presencia, mediana, MAD, canal y cantidad de muestras;
- rankear candidatos estables;
- limitar el conjunto elegido;
- explicar en diagnósticos por qué se aceptó o rechazó cada candidato.

### Pruebas

- SSID repetido con varios BSSID;
- SSID oculto;
- AP que aparece/desaparece;
- cambio de canal;
- tabla llena y cadenas en longitud límite;
- datos NVS corruptos y migración.

### Puerta de salida

El mismo conjunto de calibración produce siempre la misma selección, los BSSID
no se mezclan y la ausencia de datos tiene estado explícito.

## Fase 3 — Detector multirreferencia

**Estado: núcleo entero implementado, conectado al firmware y probado con
secuencias sintéticas, hardware y el primer replay vacío/movimiento. Pendiente
validación independiente antes de cerrar la puerta.**

### Objetivo

Transformar observaciones irregulares en un score robusto.

### Implementación

- baseline por BSSID con mediana y MAD: implementado;
- control de presencia y antigüedad por referencia: implementado;
- desviación normalizada: implementada con suelo de ruido;
- agregación robusta: mediana implementada;
- score de movimiento separado de salud de cobertura: implementado;
- estados `CALIBRATING`, `WARMUP`, `IDLE`, `MOTION`, `DEGRADED` y `NO_DATA`;
- umbral, histéresis y confirmación consecutiva: implementados;
- congelación de adaptación en estados no seguros: implementada.

### Pruebas

- unitarias con secuencias sintéticas;
- replay determinista de capturas de fase 1;
- falsos positivos en vacío;
- sensibilidad y latencia con movimiento;
- referencias ausentes y valores extremos;
- comparación selección manual/automática.

### Puerta de decisión

El detector cumple los umbrales predefinidos en datos no usados para ajustar el
modelo. Si sólo funciona en un entorno concreto, se documentará esa limitación
en vez de generalizarla.

## Fase 4 — Portal de mantenimiento

**Estado: portal funcional implementado con SoftAP WPA2, DNS cautivo, lista de
redes descubiertas, formulario automático/manual, persistencia y reinicio.
Pendiente endurecimiento de sesión.**

### Objetivo

Hacer configurable el equipo sin asociación permanente.

### Implementación

- BOOT largo entra en modo mantenimiento: implementado;
- SoftAP autenticado temporal y DNS cautivo: implementados;
- lista de SSID agrupada con canal y RSSI: implementada;
- lista de SSID agrupada con detalle de BSSID/canal/RSSI;
- modos manual y automático: implementados;
- inicio y progreso de calibración;
- diagnóstico de cobertura y motivos de selección;
- cierre seguro del portal y transición a `WARMUP`;
- controles grandes, legibles y adaptados a móvil.

### Reutilización

Adaptar `config_portal`, `portal_auth`, `captive_dns`, `recovery_button` y
`app_config` del proyecto C3 existente.

### Pruebas

- host tests de HTML/API;
- autenticación y caducidad;
- reinicios durante guardado/calibración;
- cambio de esquema NVS;
- entrada/salida repetida de mantenimiento;
- verificación de que monitorización y portal no compiten por el radio.

### Puerta de salida

Un usuario puede configurar, calibrar, consultar salud y volver a detectar sin
consola serie y sin dejar el SoftAP abierto.

## Fase 5 — Eventos locales y operación prolongada

### Objetivo

Convertir el prototipo en firmware autónomo.

### Implementación

- LED con patrones distintos para calibración, movimiento y degradado:
  implementado;
- cola de eventos con deduplicación y cooldown;
- diagnósticos resumidos sin exponer redes vecinas;
- recuperación ante errores de escaneo;
- límites de memoria y watchdog;
- reinicio seguro conservando configuración.

### Validación

- prueba prolongada;
- ciclos de energía;
- AP de referencia apagado;
- entorno Wi-Fi cambiante;
- medición de consumo;
- revisión de privacidad y seguridad.

### Puerta de salida

No hay fugas de memoria, bloqueos ni falsos estados de movimiento ante pérdida
de cobertura.

## Fase 6 — Telegram opcional

### Objetivo

Notificar sin convertir la conexión en requisito de detección.

### Variante recomendada

- credenciales de una red de subida separadas de las referencias;
- cola persistente de eventos;
- asociación temporal y explícita;
- envío HTTPS reutilizando `telegram_notifier`;
- timeout estricto y backoff;
- desconexión, limpieza de estado y `WARMUP`;
- opción “modo estrictamente desconectado” como valor claro.

### Pruebas

- sin red de subida;
- DNS/TLS/Telegram caídos;
- varios eventos en cola;
- reinicio con pendientes;
- timeout sin bloquear detector;
- verificación de que no se anuncian SSID/BSSID vecinos.

### Alternativa futura

Prototipo de pasarela externa sólo si el requisito exige que el sensor nunca se
asocie. Se estudiaría ESP-NOW u otro enlace local como proyecto separado.

### Puerta de salida

Los fallos de Internet no afectan a la captura y la interfaz deja claro cuándo
el equipo se asociará temporalmente.

## Fase 7 — Empaquetado y documentación

### Objetivo

Publicar una versión reproducible y flasheable.

### Entregables

- README de instalación, uso, calibración y límites;
- tabla de particiones revisada;
- compilación limpia con la revisión ESP-IDF fijada;
- pruebas host y firmware en CI/local;
- `firmware/` con binarios, `flash_args`, checksums y versión;
- instrucciones para flashear y restaurar;
- registro de versión y resultados de validación.

### Puerta de salida

Un equipo nuevo puede flashearse sólo con los artefactos de `firmware/`, y el
resultado coincide con el código y la revisión ESP-IDF documentados.

## Orden y dependencias

```text
F0 especificación
  -> F1 señal real
      -> [viable] F2 referencias
          -> F3 detector
              -> F4 portal
                  -> F5 autonomía
                      -> F6 Telegram opcional
                          -> F7 publicación
      -> [no viable] sniffer/CSI experimental o cierre
```

## Definición global de terminado

- la detección no llama a `esp_wifi_connect()`;
- manual y automático están probados con varios BSSID por SSID;
- cobertura degradada nunca equivale por sí sola a movimiento;
- todas las transiciones de radio pertenecen a un coordinador;
- los experimentos son reproducibles y usan datos de validación separados;
- Telegram es opcional y no bloquea el muestreo;
- la privacidad y las limitaciones quedan visibles en la documentación;
- las pruebas pasan y los binarios reproducibles quedan en `firmware/`.
