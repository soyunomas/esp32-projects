# Protocolo experimental RSSI

Este protocolo mide el detector sin mezclar ajuste y evaluación. No recoge
audio, imágenes, MAC de clientes ni identificadores de personas. El BSSID del
punto de acceso forma parte de la telemetría técnica y no debe publicarse sin
anonimizarlo.

## Preparación

1. Fijar AP, canal, posiciones, altura, orientación y distancia.
2. Dejar inmóvil el enlace durante toda la calibración inicial.
3. Usar salida CSV y ejecutar `./tools/test-host.sh` para disponer del replay.
4. Definir antes de capturar qué sesiones son `tuning` y cuáles son
   `evaluation`. Nunca cambiar el split después de mirar los resultados.
5. Mantener personas no participantes y dispositivos móviles fuera del área.

## Escenarios mínimos

- `empty_room`: reposo sin personas en la zona medida.
- `link_crossing`: cruce completo entre AP y sensor.
- `lateral_motion`: movimiento lateral sin cruzar directamente el enlace.
- `outside_path`: movimiento fuera del trayecto directo.
- `non_human_disturbance`: puertas, ventiladores u objetos móviles definidos.

Cada evento debe tener inicio y fin definidos antes de revisar sus resultados.
Para pruebas físicas se recomienda el marcador BOOT local, porque evita la
latencia no determinista de instrucciones remotas. Se recomiendan al menos diez
eventos por escenario y split, varias
posiciones/orientaciones y orden aleatorio. Para sostener el objetivo de falsos
positivos se exige al menos una hora acumulada de reposo en evaluación.

## Captura

Ejemplo de una sesión de cinco minutos con tres cruces:

```bash
python3 tools/experiment.py capture \
  --port /dev/ttyACM0 --duration 300 \
  --output dataset/evaluation/crossing-01.csv \
  --split evaluation --scenario link_crossing \
  --event cross-1:60:64 --event cross-2:140:144 --event cross-3:220:224 \
  --distance-m 3.2 --height-m 1.1 \
  --orientation usb-up --placement shelf-a
```

El CSV añade etiquetas y metadatos a cada muestra. El JSON adyacente conserva
geometría, eventos, split, número de muestras y SHA-256 de la secuencia RSSI.
También se puede importar una captura existente con `--input archivo.log`.

### Marcado físico con BOOT

El firmware configura por defecto GPIO9 como botón activo en bajo, con 250 ms
de antirrebote. GPIO9 es un pin de arranque del ESP32-C3: solo se pulsa cuando
el firmware ya está ejecutándose; nunca se mantiene pulsado al conectar,
reiniciar o flashear la placa.

```bash
python3 tools/experiment.py capture \
  --port /dev/ttyACM0 --duration 300 --marker-events \
  --output dataset/tuning/crossing-boot-01.csv \
  --split tuning --scenario link_crossing \
  --distance-m 1.3 --height-m 1.2 \
  --orientation usb-up --placement center-room
```

Una pulsación abre el evento y la siguiente lo cierra. La persona debe pulsar
BOOT justo antes de cruzar, realizar el cruce a paso normal y pulsarlo de nuevo
inmediatamente después. La captura falla de forma explícita si termina con un
evento abierto o sin ningún par completo.

## Ajuste, comparación y evaluación

Comparar las 30 combinaciones de cinco algoritmos, dos baselines y tres perfiles
sobre exactamente las mismas muestras de ajuste:

```bash
python3 tools/experiment.py compare \
  --input dataset/tuning/crossing-01.csv \
  --input dataset/tuning/idle-01.csv \
  --split tuning --output reports/tuning-comparison.json
```

Después de elegir y bloquear una configuración, evaluar exclusivamente el split
no visto:

```bash
python3 tools/experiment.py evaluate \
  --input dataset/evaluation/crossing-01.csv \
  --input dataset/evaluation/idle-01.csv \
  --split evaluation --output reports/evaluation.json
```

El informe contiene recall por evento, precisión/recall/F1 por muestra, falsas
activaciones por hora, latencias medianas, muestras repetidas y estabilidad del
baseline. Solo declara elegibles los objetivos con diez eventos como mínimo y
una hora acumulada de reposo.

## Criterio de decisión

En evaluación no vista y para el trayecto declarado:

- recall por evento >= 90 %;
- falsos positivos <= 1 por hora;
- latencia mediana de activación <= 1 segundo.

Si ninguna configuración cumple, se conserva el informe negativo y se pasa a
CSI. No se reutiliza el split de evaluación para volver a ajustar RSSI.
