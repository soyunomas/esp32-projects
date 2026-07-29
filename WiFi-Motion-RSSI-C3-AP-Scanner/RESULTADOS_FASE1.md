# Resultados de fase 1

## Validación física inicial

Fecha: 29 de julio de 2026
Placa: ESP32-C3 QFN32 revisión 0.4, flash integrada XMC de 4 MB
Interfaz: USB Serial/JTAG
MAC: omitida deliberadamente de la documentación publicable

El firmware fue escrito en `/dev/ttyACM0`. Esptool verificó el hash de
bootloader, tabla de particiones y aplicación.

## Configuración

- modo: escaneo pasivo;
- permanencia: 120 ms por canal;
- espera tras barrido: 500 ms;
- asociación: ninguna; el firmware no llama a `esp_wifi_connect()`;
- esquema: `wifi_ap_scan/v1`.

## Persistencia de la calibración

Se validó en la placa una selección automática seguida de un reinicio sin
borrar NVS:

| Comprobación | Resultado |
|---|---:|
| Escaneos completos usados para calibrar | 40 |
| Referencias seleccionadas | 2 |
| Canales únicos resultantes | 1 y 6 |
| Estado de escritura NVS | 0 (`OK`) |
| Duración posterior por ciclo | 243–244 ms |
| Primer ciclo capturado tras reiniciar | 243 ms, modo `selected` |
| Ciclos `all` observados tras reiniciar | 0 |

La selección persistida incluye versión, CRC32, BSSID, SSID, canal, mediana y
MAD. El firmware rechaza blobs corruptos o incompatibles y recalibra con todos
los canales. Las capturas de validación son
`capturas/nvs-first-calibration.jsonl` y
`capturas/nvs-restored-boot.jsonl`.

## Captura de humo

Resultado de una ventana de 20 segundos:

| Métrica | Resultado |
|---|---:|
| Resúmenes de escaneo recibidos | 9 |
| Grupos completamente capturados | 8 |
| Duración mínima/máxima | 1685 / 1685 ms |
| Errores de escaneo | 0 |
| Eventos de callback perdidos | 0 |
| Escaneos truncados | 0 |
| Heap libre mínimo observado | 221268 bytes |
| Mínimo histórico de heap | 216824 bytes |
| BSSID observados | 2 |

Un BSSID del canal 1 apareció de forma estable alrededor de -49 dBm. Otro del
canal 6 apareció entre -85 y -79 dBm. Estos valores sólo describen esta prueba y
no son umbrales de detección.

## Hallazgos

1. El escaneo pasivo desconectado funciona de forma repetible en el hardware.
2. La cadencia observada es aproximadamente un barrido cada 2,185 segundos al
   sumar escaneo y espera configurada.
3. La memoria se mantuvo estable durante la captura corta.
4. Abrir el puerto a mitad de un barrido puede producir un grupo parcial. La
   herramienta se corrigió para agrupar por `scan_id` y descartar grupos cuyo
   número de AP no coincida con `emitted_aps`.
5. Cada resumen incluye ahora modo y tiempos efectivos, incluso si no se capturó
   el registro de arranque.
6. Una segunda captura verificó el filtro: descartó el primer grupo parcial y
   conservó cuatro grupos; en todos coincidieron `emitted_aps` y las líneas `ap`.

## Captura ambiental sin etiqueta

Se recogieron 45 segundos adicionales sin asumir si había movimiento:

| Referencia | Canal | Presencia | Mediana RSSI | MAD | Rango |
|---|---:|---:|---:|---:|---:|
| `Sala_N` | 1 | 100 % (20/20) | -50 dBm | 0,5 dB | -53 a -49 dBm |
| `WIFI-C` | 6 | 100 % (20/20) | -84 dBm | 2 dB | -88 a -81 dBm |

Los 20 escaneos duraron 1685 ms, sin errores, truncamientos ni callbacks
perdidos. El heap libre osciló sólo entre 221268 y 221276 bytes.

Esta captura confirma que ambos BSSID pueden ser candidatos de calibración,
especialmente `Sala_N`, pero no mide capacidad de detección porque carece de
etiqueta de habitación vacía o movimiento.

Se añadió `tools/summarize_capture.py` para obtener automáticamente presencia,
mediana, MAD, rango, canales, cadencia, memoria y errores sin escoger todavía
referencias ni umbrales.

## Prueba prolongada de estabilidad sin etiqueta

Se realizó una captura continua de 120 segundos. La condición física de la
habitación no fue confirmada, por lo que sus datos se usan únicamente para
evaluar estabilidad, cadencia y recursos:

| Métrica | Resultado |
|---|---:|
| Barridos completos | 53 |
| Registros válidos | 158 |
| Errores de escaneo | 0 |
| Eventos de callback perdidos | 0 |
| Escaneos truncados | 0 |
| Duración mínima/mediana/máxima | 1685 / 1685 / 1685 ms |
| Heap libre observado | 221268 a 221276 bytes |
| Mínimo histórico de heap | 216824 bytes |
| Observaciones de AP | 105 |

| Referencia | Canal | Presencia | Mediana | MAD | Rango | Mediana 1.ª mitad | Mediana 2.ª mitad | Cambio |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `Sala_N` | 1 | 100 % (53/53) | -53 dBm | 2 dB | -57 a -48 dBm | -53 dBm | -53 dBm | 0 dB |
| `WIFI-C` | 6 | 98,1 % (52/53) | -84 dBm | 2 dB | -88 a -80 dBm | -84 dBm | -85 dBm | -1 dB |

No se aprecia deriva monotónica relevante entre las dos mitades de la sesión.
Ambos BSSID son candidatos persistentes; `Sala_N` es además la referencia con
mayor RSSI. Esta prueba no permite medir separación entre reposo y movimiento
porque no tiene una etiqueta física fiable.

## Sesión etiquetada: habitación vacía

El usuario confirmó la habitación vacía antes de iniciar una sesión independiente
de 300 segundos. La captura original se conserva localmente como
`capturas/ubicacion-a-vacio-01.jsonl`.

| Métrica | Resultado |
|---|---:|
| Barridos completos | 136 |
| Registros conservados | 404 |
| Primer grupo parcial descartado | 1 |
| Errores de escaneo | 0 |
| Eventos de callback perdidos | 0 |
| Escaneos truncados | 0 |
| Duración mínima/mediana/máxima | 1684 / 1685 / 1685 ms |
| Heap libre observado | 221268 a 221276 bytes |
| Mínimo histórico de heap | 216824 bytes |
| Observaciones de AP | 268 |

| Referencia | Canal | Presencia | Mediana | MAD | Rango | Mediana 1.ª mitad | Mediana 2.ª mitad | Cambio |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `Sala_N` | 1 | 99,3 % (135/136) | -51 dBm | 3 dB | -60 a -44 dBm | -50 dBm | -51 dBm | -1 dB |
| `WIFI-C` | 6 | 97,8 % (133/136) | -86 dBm | 2 dB | -94 a -80 dBm | -85 dBm | -88 dBm | -3 dB |

La sesión es válida como primera referencia de habitación vacía. El BSSID de
`Sala_N` es la referencia principal por potencia y persistencia. La deriva de
-3 dB observada en `WIFI-C`, aun sin movimiento deliberado, demuestra que el
detector deberá modelar cambios lentos y no basarse sólo en una diferencia
instantánea de RSSI.

## Sesión etiquetada: movimiento

El usuario confirmó el inicio y se movió por la habitación. La sesión prevista
para 300 segundos se detuvo a petición del usuario tras aproximadamente 197
segundos; se conserva completa como
`capturas/ubicacion-a-movimiento-01.jsonl`.

| Métrica | Resultado |
|---|---:|
| Barridos completos | 90 |
| Registros conservados | 265 |
| Primer grupo parcial descartado | 1 |
| Errores de escaneo | 0 |
| Eventos de callback perdidos | 0 |
| Escaneos truncados | 0 |
| Duración mínima/mediana/máxima | 1684 / 1685 / 1685 ms |
| Heap libre observado | 221268 a 221276 bytes |
| Observaciones de AP | 175 |

| Referencia | Presencia | Mediana | MAD | Rango | Cambio adyacente mediano | Cambio adyacente P90 |
|---|---:|---:|---:|---:|---:|---:|
| `Sala_N` | 100 % (90/90) | -46 dBm | 1 dB | -50 a -40 dBm | 1 dB | 4 dB |
| `WIFI-C` | 94,4 % (85/90) | -81 dBm | 1 dB | -89 a -76 dBm | 1 dB | 4 dB |

## Primera comparación vacío frente a movimiento

| Referencia | Mediana vacía | Mediana movimiento | Diferencia | P90 adyacente vacío | P90 adyacente movimiento |
|---|---:|---:|---:|---:|---:|
| `Sala_N` | -51 dBm | -46 dBm | +5 dB | 3 dB | 4 dB |
| `WIFI-C` | -86 dBm | -81 dBm | +5 dB | 5 dB | 4 dB |

Las medianas cambiaron +5 dB en ambos BSSID al entrar una persona, pero la
variación rápida no aumentó de forma consistente: sólo subió 1 dB en `Sala_N`
y bajó 1 dB en `WIFI-C`. Con estas dos sesiones existe una señal prometedora de
presencia o cambio de posición, pero aún no evidencia suficiente para afirmar
que se detecta movimiento. Harán falta repeticiones independientes para separar
el efecto humano de la deriva temporal del entorno.

## Variante limitada a canales 1 y 6

Se añadió un `scan_scheduler` que ejecuta un escaneo por cada canal configurado
y los agrega como un único ciclo lógico, deduplicando por BSSID. La variante se
compiló, flasheó y verificó físicamente durante 45 segundos:

| Métrica | Barrido completo | Canales 1 y 6 |
|---|---:|---:|
| Duración mediana del radio | 1685 ms | 244 ms |
| Pausa entre ciclos | 500 ms | 500 ms |
| Periodo total aproximado | 2185 ms | 744 ms |
| Barridos en 45 segundos | 20 | 59 |
| Mejora de cadencia | 1× | 2,94× |
| Errores / truncamientos / pérdidas | 0 / 0 / 0 | 0 / 0 / 0 |

`Sala_N` estuvo presente en 59/59 ciclos y `WIFI-C` en 58/59. La memoria libre
se mantuvo fija en 215380 bytes, unos 5,9 KiB menos que el firmware anterior
por el búfer adicional de agregación, sin deriva durante la prueba. Cada
registro `scan` declara `channel_mode`, `channel_count`, `channel_1` y
`channel_2`, de modo que las capturas siguen siendo reproducibles.

## Calibración automática integrada

Se flasheó y ejecutó la integración completa de `observation_store`,
`reference_selector` y `scan_scheduler`. El arranque realizó 40 barridos
pasivos de todos los canales y produjo estas decisiones:

| Referencia | Canal | Presencia | Mediana | MAD | Rango | Resultado |
|---|---:|---:|---:|---:|---:|---|
| `Sala_N` | 1 | 100 % (40/40) | -37 dBm | 0 dB | -37 a -37 dBm | seleccionada, rango 1 |
| `WIFI-C` | 6 | 100 % (40/40) | -84 dBm | 1 dB | -87 a -81 dBm | seleccionada, rango 2 |

Ambos BSSID conservaron SSID y canal estables y obtuvieron
`rejection_flags: 0`. Al completar la calibración, el firmware cambió
automáticamente del barrido completo a los canales únicos 1 y 6:

| Estado | Modo de canales | Duración de radio |
|---|---|---:|
| Calibración | todos | 1685 ms |
| Monitorización posterior | seleccionados 1 y 6 | 243–244 ms |

La captura `capturas/live-auto-calibration-01.jsonl` contiene 83 ciclos
completos, cero errores, cero truncamientos y cero eventos perdidos. El heap
libre se mantuvo entre 207556 y 207564 bytes, con mínimo histórico de 203112
bytes. El coste de las tablas de calibración es estable y queda margen amplio.

## Primer detector multirreferencia

El componente usa desviaciones RSSI normalizadas por
`max(MAD, suelo de ruido)`, agrega con la mediana y mantiene la salud de
cobertura separada del score. La pérdida de referencias produce
`DEGRADED/NO_DATA`, no `MOTION`.

Un smoke test físico produjo 32 ciclos completos con las dos referencias,
cobertura del 100 %, estado `IDLE`, ningún error y scores entre 0,25 y 1,00.

Se ejecutó después exactamente el mismo núcleo C sobre el primer par de
capturas etiquetadas. Con activación 2,50, liberación 1,25 y tres confirmaciones
consecutivas:

| Captura | Ciclos evaluados | Ciclos `MOTION` | Score máximo |
|---|---:|---:|---:|
| Habitación vacía | 136 | 0 | 3,08 |
| Movimiento | 90 | 70 | 3,83 |

El máximo aislado de la sesión vacía no genera evento debido a la confirmación
consecutiva. El resultado es prometedor, pero los datos usados pertenecen a un
único par temporal y a una ubicación; el umbral 2,50 sigue siendo provisional
hasta superar repeticiones independientes y controles de cobertura.

## Estado de la puerta de fase

La integración de hardware y la captura son viables. La fase 1 todavía no se
considera cerrada: ya se completaron la prueba prolongada y un primer par
vacío/movimiento, y la variante de canales limitados mejora la cadencia casi
tres veces; faltan repeticiones con esta variante, tráfico variable, AP ausente
y una prueba en otra ubicación. La validación del botón físico sigue abierta:
las ventanas realizadas no recibieron una transición y se añadió telemetría
eléctrica `button` para diferenciar pin incorrecto, ausencia de pulsación y
problema lógico.
