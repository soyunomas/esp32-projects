# Resultados de la fase 4

## Estado

Fase cerrada con resultado experimental **inconcluso**. La infraestructura de
captura, etiquetado, replay y evaluación queda implementada y sin errores
conocidos, pero la campaña física no alcanza el tamaño mínimo necesario para
declarar que RSSI cumple o incumple los objetivos de producto.

No se atribuyen prestaciones generales a partir de esta muestra y no se
reutiliza como evaluación no vista.

## Implementación verificada

- Captura CSV desde archivo, entrada estándar o `/dev/ttyACM0`.
- Reloj monotónico del host para alinear eventos físicos con telemetría viva.
- Metadatos de sesión, split, escenario, geometría y SHA-256 del RSSI.
- Separación estricta entre `tuning` y `evaluation`.
- Métricas de recall por evento, precisión, recall y F1 por muestra, falsos
  positivos por hora, latencias, muestras repetidas y estabilidad del baseline.
- Replay de las 30 combinaciones de perfil, algoritmo y baseline usando el
  mismo núcleo C que el firmware.
- Marcador físico BOOT/GPIO9 configurable, activo en bajo, con antirrebote y
  sondeo dedicado a 10 ms. Una pulsación abre el evento y la siguiente lo
  cierra. GPIO9 solo se usa después del arranque normal.
- Las capturas rechazadas se conservan en `dataset/rejected/` con
  `usable_for_metrics: false` y un motivo explícito.

## Errores encontrados y corregidos

1. Los informes no creaban su directorio de salida.
2. Las etiquetas vivas usaban el uptime disperso del dispositivo en vez del
   reloj de llegada del host.
3. El evaluador reconocía `active`, pero el firmware emite `motion`.
4. Las instrucciones remotas introducían una latencia no determinista; se
   sustituyeron por un marcador físico local.
5. El sondeo inicial de BOOT a 100 ms podía perder pulsaciones breves; se movió
   a una tarea de 10 ms manteniendo 250 ms de antirrebote.

Tras cada corrección se volvió a ejecutar la suite completa antes de continuar.

## Validación

- Pruebas host: **6/6 correctas**, incluidos detector, métricas, configuración,
  marcador físico, validador de fase 1 y herramientas experimentales.
- Firmware CSV: compilación correcta, `0xd2e20` bytes y 45 % libre en la
  partición de aplicación.
- Firmware JSON Lines: compilación correcta.
- Flasheo físico: escritura y verificación SHA correctas en ESP32-C3; reinicio
  posterior correcto y configuración NVS conservada.
- Smoke físico posterior al flasheo: 57/57 muestras, 41 campos por fila, cero
  errores de lectura, cero pérdidas de planificación y marcador inicialmente
  cerrado.
- Toma preliminar reproducible:
  `dataset/tuning/link-crossing-center-valid-04.csv`, 623/623 muestras, a 1,3 m
  y 1,2 m de altura.

## Resultado preliminar, no elegible

En la única toma utilizable el detector registró 1/1 cruce y una latencia de
activación de 95 ms. La precisión muestral fue 79,6 % y el F1 76,7 %. Estos
valores no son elegibles para los objetivos: solo existe un evento y unos 50 s
de reposo, frente al mínimo definido de diez eventos y una hora.

La cifra extrapolada de falsos positivos por hora tampoco se interpreta: una
activación comenzó 0,9 s antes de la ventana manual y el período de reposo es
demasiado corto.

## Decisión de salida

Completar la matriz original exige cruzar repetidamente una instalación con
cables. El montaje actual dificulta el movimiento y aumenta el riesgo de
tropiezo, por lo que no se fuerza la campaña ni se fabrican etiquetas.

RSSI queda caracterizado como baseline preliminar. Se avanza a CSI manteniendo
el mismo formato de telemetría y protocolo. Una futura campaña RSSI solo se
reabrirá con alimentación y adquisición que no obliguen a cruzar cables, por
ejemplo mediante telemetría inalámbrica o una disposición física segura.
