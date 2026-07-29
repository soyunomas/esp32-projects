# Estudio de viabilidad

Fecha: 29 de julio de 2026
Objetivo: ESP32-C3 SuperMini sin asociación Wi-Fi permanente

## 1. Resumen ejecutivo

Sí es técnicamente posible observar AP cercanos sin asociarse a ellos. ESP-IDF
ofrece escaneo activo y pasivo, además de modo promiscuo. El camino recomendado
para el primer prototipo es usar `esp_wifi_scan_start()` en modo STA
desconectado, de forma asíncrona y preferentemente con escaneo pasivo.

Esto permite construir un vector temporal de RSSI de varios AP, pero cambia la
naturaleza de la señal respecto al firmware existente:

- ya no hay una lectura frecuente de un único enlace conectado;
- el radio recorre canales y las medidas de un escaneo no son simultáneas;
- un AP puede no aparecer en un escaneo;
- SSID no es una identidad única: hay que trabajar por BSSID;
- el RSSI de beacons/probe responses es ruidoso y depende del entorno y del AP.

Por ello el resultado debe describirse inicialmente como **detección
experimental de cambios**, no como sensor de presencia garantizado. La
viabilidad funcional se decidirá con datos reales.

## 2. Qué significa “sin conectarse a un AP”

Hay tres conceptos distintos:

| Modo | Asociación a AP externo | Transmisión del C3 | Utilidad |
|---|---:|---:|---|
| Escaneo pasivo | No | No durante el escaneo | Oye beacons; opción inicial recomendada |
| Escaneo activo | No | Sí, envía probe requests | Descubrimiento más rápido/completo |
| Modo promiscuo | No | No es necesaria | Recibe tramas y ofrece más muestras, con más complejidad |

Por tanto, “no asociarse” es viable. Si además se exige silencio radioeléctrico,
debe emplearse escaneo pasivo y evitar el modo de subida temporal.

ESP-IDF documenta expresamente los escaneos activos y pasivos y el modo
promiscuo. El sniffer puede ejecutarse incluso en `WIFI_MODE_NULL` o
`WIFI_MODE_STA`, y su callback se ejecuta dentro de la tarea Wi-Fi, por lo que
sólo debe encolar datos y delegar el procesamiento
([API Wi-Fi](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/api-reference/network/esp_wifi.html),
[modo sniffer](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/api-guides/wifi-driver/wifi-modes.html)).

## 3. Limitaciones físicas y temporales

El ESP32-C3 dispone de un solo radio Wi-Fi. Un barrido completo observa los
canales de manera secuencial: una medida del canal 1 y otra del canal 11
pertenecen a instantes diferentes. Esto reduce la resolución temporal frente a
la lectura RSSI de un AP ya conectado.

La API permite escaneo asíncrono, por canal, por SSID o BSSID, y con tiempos
activos/pasivos configurables. Los resultados devuelven, entre otros datos,
SSID, BSSID, RSSI y canal. La memoria interna de resultados debe liberarse
leyendo o limpiando la lista tras cada escaneo. El ejemplo oficial de Espressif
sirve como base de integración
([ejemplo `wifi/scan`](https://github.com/espressif/esp-idf/tree/master/examples/wifi/scan)).

Consecuencias para el diseño:

- no ejecutar trabajo pesado en callbacks de Wi-Fi;
- mantener una única máquina de estados propietaria del radio;
- no solapar escaneo, portal, conexión de subida o CSI;
- registrar duración real, canales visitados, número de AP y errores;
- evaluar escaneo por todos los canales frente a una lista corta de canales de
  referencia.

## 4. Selección manual y automática

### 4.1 Identidad correcta

El usuario seleccionará nombres **SSID** porque son comprensibles, pero el motor
almacenará y calibrará **BSSID**. Dos AP con el mismo SSID pueden ser radios
distintos, estar en canales distintos y tener señales independientes.

La configuración propuesta conserva:

- lista de SSID seleccionados por el usuario;
- política manual o automática;
- BSSID descubiertos para esos SSID durante calibración;
- canal, RSSI basal, dispersión y tasa de presencia por BSSID;
- instante y versión de la calibración.

Los SSID ocultos se pueden mostrar como “oculto” y elegir sólo por BSSID. No se
deben fusionar BSSID antes de calcular sus estadísticas.

### 4.2 Selección manual

Durante el modo de mantenimiento:

1. se ejecuta un escaneo de descubrimiento;
2. la interfaz agrupa BSSID bajo su SSID y muestra canal y RSSI;
3. el usuario marca uno o varios SSID;
4. la calibración observa todos sus BSSID;
5. sólo se retienen los BSSID con presencia y estabilidad suficientes.

Si aparece después un nuevo BSSID con el mismo SSID, no se incorporará
silenciosamente al modelo activo: se mostrará como candidato y requerirá
recalibración o una política explícita.

### 4.3 Selección automática

Si no se selecciona ningún SSID, varios escaneos de calibración crean una tabla
de candidatos por BSSID. Se puntúan usando:

- tasa de presencia;
- RSSI mediano;
- dispersión robusta, como MAD;
- estabilidad de canal;
- número de muestras válidas;
- redundancia con otros BSSID.

Se escogerá un conjunto pequeño de BSSID persistentes y no simplemente los más
potentes. Los valores iniciales —número de escaneos, presencia mínima, RSSI
mínimo y número máximo de referencias— serán hipótesis configurables. No se
fijarán como valores de producción hasta analizar los datos de la fase 1.

## 5. Modelo de señal propuesto

Cada ciclo produce un conjunto:

```text
observación = { BSSID -> (RSSI, canal, tiempo) }
```

Para cada BSSID de referencia se aprende una mediana basal y una dispersión
robusta. En ejecución se calcula una desviación normalizada y se combinan las
referencias con una función robusta, por ejemplo mediana o media recortada.

```text
desviación_bssid = |RSSI_actual - mediana_basal| / dispersión_basal
score_movimiento = agregado_robusto(desviaciones_bssid válidas)
```

El motor tendrá dos salidas independientes:

- **score de cambio/movimiento**;
- **salud de cobertura**, basada en referencias observadas, cadencia y
  antigüedad.

Una referencia ausente no se convertirá directamente en movimiento. Si no hay
cobertura suficiente, el estado será `DEGRADED` o `NO_DATA`. El disparo usará
histeresis y confirmación en varios ciclos. La actualización adaptativa del
baseline se congelará durante movimiento, cobertura degradada y cambios de
modo.

## 6. Arquitectura propuesta

```text
Wi-Fi radio owner / state machine
    ├── maintenance: SoftAP + portal, detector pausado
    ├── calibration: scan scheduler -> observation store
    ├── detection:   scan scheduler -> reference tracker
    └── uplink:      asociación temporal opcional, detector pausado

observation store
    -> reference selector (manual/auto)
    -> robust feature aggregator
    -> existing scalar motion detector
    -> LED / serial / event queue
    -> optional Telegram uplink
```

La máquina de estados evita que distintos componentes intenten controlar a la
vez el único radio:

`BOOT -> MAINTENANCE -> CALIBRATING -> WARMUP -> MONITORING -> DEGRADED`.

La subida temporal, si se habilita, añade:

`MONITORING -> UPLINK -> WARMUP -> MONITORING`.

Tras portal o subida se exige `WARMUP`, ya que cambiar canal y actividad de radio
puede contaminar las primeras observaciones.

## 7. Portal de configuración

El detector no necesita asociarse a un AP externo. Para configurarlo, una
pulsación larga de BOOT reiniciará o cambiará temporalmente a modo mantenimiento:

- el C3 crea su propio SoftAP protegido;
- se abre el portal local;
- se escanean y eligen referencias;
- se inicia calibración;
- al terminar, se cierra el SoftAP y se vuelve a STA desconectado.

Durante el portal la detección queda pausada. Mantener portal y monitorización
multicanal simultáneamente daría resultados difíciles de interpretar porque
comparten el radio.

## 8. Telegram y conectividad

Telegram no puede enviar mensajes sin una ruta a Internet. `sendMessage` es una
llamada a la Bot API de Telegram
([Bot API](https://core.telegram.org/bots/api#sendmessage)). Por tanto hay tres
opciones:

1. **Modo estrictamente desconectado:** LED, USB serie y eventos locales; sin
   Telegram en tiempo real.
2. **Subida temporal opcional:** se encola el evento, se pausa el detector, el C3
   se asocia brevemente a una red configurada, envía por HTTPS, se desconecta y
   hace warm-up. La detección no depende del AP, pero el dispositivo sí se asocia
   para notificar.
3. **Pasarela externa futura:** el sensor entrega eventos a otro equipo, por
   ejemplo mediante ESP-NOW; la pasarela usa Internet. Mantiene el sensor sin
   asociación, pero añade hardware y otro protocolo.

La opción 2 es la evolución más corta porque reutiliza el cliente existente.
Debe ser opt-in y mostrarse claramente que rompe el modo “nunca asociarse”.

## 9. Reutilización del proyecto existente

Origen: `WiFi-Motion-RSSI-C3-Supermini`.

| Componente actual | Decisión | Trabajo necesario |
|---|---|---|
| `motion_detector` | Reutilizar | Alimentarlo con el score agregado, no con RSSI directo |
| `config_portal` | Adaptar | Selección multivalor SSID/BSSID y modos del radio |
| escaneo del portal | Reutilizar como base | Conservar BSSID; hoy agrupa sólo por SSID |
| `app_config` + NVS | Adaptar | Nuevo esquema y migración segura |
| `portal_auth` | Reutilizar | Integración con SoftAP temporal |
| `captive_dns` | Reutilizar | Sólo en mantenimiento |
| `recovery_button` | Reutilizar | Entrada/salida del mantenimiento |
| `event_marker` | Reutilizar | Etiquetar experimentos reales |
| `sample_metrics` | Adaptar | Métricas por escaneo, BSSID y cobertura |
| `telegram_notifier` | Adaptar | Cola persistente y coordinador de subida |
| `csi_capture/features/traffic` | Posponer | No forma parte del MVP scan-RSSI |
| pruebas host | Ampliar | Selector, agregador, estados y migración |

El código actual obtiene la señal principal con
`esp_wifi_sta_get_ap_info()`, lo que presupone asociación. También dispone ya de
un escaneo asíncrono en el portal; éste será la semilla del nuevo colector, pero
dejará de colapsar todos los BSSID de un mismo SSID.

## 10. CSI sin asociación

Espressif indica que una estación desconectada no recibe normalmente paquetes y
recomienda habilitar sniffer para obtener más CSI. Es posible investigarlo, pero
requiere fijar o recorrer canales, filtrar transmisores y gestionar callbacks de
alta frecuencia
([guía CSI](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/api-guides/wifi-driver/wifi-vendor-features.html)).

No se incluye CSI en el MVP. Sólo se abrirá una rama experimental si el escaneo
RSSI no ofrece suficiente cadencia y los datos justifican esa complejidad.

## 11. Riesgos y mitigaciones

| Riesgo | Impacto | Mitigación o prueba |
|---|---|---|
| Cadencia baja de barrido completo | Movimiento corto no observado | Medir tiempos; reducir canales tras calibración |
| AP ausente o que cambia de canal | Falsos eventos | Salud separada, expiración y recalibración |
| Varios BSSID con mismo SSID | Mezcla de señales | Identidad interna por BSSID |
| Tráfico y potencia variables del AP | Ruido | Estadística robusta y conjunto de referencias |
| Cambio de mobiliario o posición | Deriva | Recalibración explícita y baseline controlado |
| Portal/subida altera el radio | Datos contaminados | Pausa y warm-up |
| Vecindario Wi-Fi cambia | Modelo obsoleto | Diagnóstico de cobertura y candidatos nuevos |
| Telegram bloquea muestreo | Pérdida de datos | Cola y tarea separada; subida coordinada |
| Datos de redes ajenas | Privacidad | Guardar sólo metadatos mínimos; no capturar payloads |

## 12. Seguridad y privacidad

- No capturar ni almacenar cargas útiles 802.11.
- Guardar sólo SSID/BSSID, canal, RSSI y estadísticas necesarias.
- No incluir SSID/BSSID vecinos en Telegram ni telemetría remota por defecto.
- Mantener token de Telegram y credenciales de subida en NVS, nunca en respuestas
  del portal.
- Proteger el SoftAP de mantenimiento y limitar su duración.
- Respetar el dominio regulatorio y sus canales configurados.
- Documentar que SSID y BSSID son información del entorno local.

## 13. Veredicto y condición para continuar

**Viabilidad técnica: sí.**
**Viabilidad como detector fiable de personas: aún no demostrada.**

Se recomienda continuar sólo con un prototipo instrumental de fase 1. La puerta
de avance se basará en métricas definidas antes de recoger datos:

- duración y regularidad de los escaneos;
- cobertura de referencias;
- separación entre vacío y movimiento;
- falsos positivos por hora en vacío;
- eventos reales detectados y latencia;
- comparación manual/automática;
- memoria, CPU y estabilidad durante una prueba prolongada.

Los umbrales numéricos de aceptación se fijarán al preparar el protocolo de
experimento, antes de mirar los resultados, para evitar escogerlos a posteriori.
