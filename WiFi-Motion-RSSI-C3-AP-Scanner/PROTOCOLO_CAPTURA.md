# Protocolo de captura — Fase 1

## Objetivo

Medir cadencia, cobertura y variación RSSI sin ajustar todavía un detector.
Todas las decisiones de selección y umbral se harán después sobre sesiones
separadas de entrenamiento y validación.

## Preparación

1. Colocar el ESP32-C3 en una posición fija, alimentado por USB.
2. Anotar ubicación, orientación, hora y cambios deliberados del entorno.
3. Flashear `firmware/wifi-ap-scan-probe-c3.bin` o la imagen completa.
4. Iniciar `tools/capture_jsonl.py`.
5. No mover la placa ni el cable durante una sesión.

El firmware predeterminado usa escaneo pasivo, 120 ms por canal y 500 ms de
espera tras cada barrido. Son parámetros experimentales, no valores finales.

## Sesión mínima

Realizar sesiones independientes, sin concatenarlas:

1. habitación vacía y quieta;
2. una persona entrando, moviéndose y saliendo;
3. habitación vacía con tráfico Wi-Fi variable;
4. apagar temporalmente un AP cercano;
5. repetir vacío y movimiento en otra ubicación.

Cada sesión debe incluir un periodo vacío antes y después de los eventos. Su
duración se decidirá antes de capturar y se anotará junto al archivo.

## Etiquetado con BOOT

- Pulsación corta: empieza el evento y emite `marker: started`.
- Siguiente pulsación corta: termina el evento y emite `marker: finished`.
- No reiniciar ni mantener BOOT pulsado durante la captura.

El identificador `event_id` aumenta al empezar cada evento. La etiqueta es
verdad de campo introducida por el usuario; no es una predicción.

## Captura

```bash
mkdir -p capturas
python3 tools/capture_jsonl.py \
  --port /dev/ttyACM0 \
  --output capturas/ubicacion-a-vacio-01.jsonl \
  --duration 300
```

La herramienta ignora logs de arranque y guarda únicamente objetos JSON con
esquema `wifi_ap_scan/v1`. No reinicia el dispositivo al abrir el puerto. Cada
resumen `scan` contiene el modo y los tiempos efectivos, aunque la captura haya
comenzado después del registro `boot`. Los AP se agrupan por `scan_id`; si la
captura empieza a mitad de un barrido o falta alguna línea, ese barrido se
descarta completo.

Interrumpir limpiamente con `Ctrl+C`. No editar los archivos originales; las
transformaciones deben crear archivos derivados.

## Comprobaciones inmediatas

Al terminar una sesión:

- debe haber registros `scan` crecientes;
- `scan_error` debe ser excepcional;
- `dropped_events` debe permanecer en cero;
- `truncated` debe ser falso o quedar anotado;
- `minimum_free_heap` no debe caer de forma sostenida;
- los eventos deben tener pares `started`/`finished`;
- el archivo debe acabar en una línea JSON completa.

## Variantes controladas

Cambiar una sola variable por compilación mediante `idf.py menuconfig`:

- escaneo pasivo frente a activo;
- tiempo por canal;
- espera entre barridos;
- máximo de AP emitidos.

Cada archivo debe conservar registros `scan`, que indican modo, permanencia e
intervalo efectivos. Si también contiene `boot`, se conservará como metadato de
la placa y el reinicio.

## Privacidad

Las capturas contienen SSID y BSSID del entorno. Deben tratarse como datos
locales sensibles:

- no subir capturas sin revisión;
- no enviarlas por Telegram;
- no capturar payloads Wi-Fi;
- anonimizar SSID/BSSID antes de compartir conjuntos de datos.
