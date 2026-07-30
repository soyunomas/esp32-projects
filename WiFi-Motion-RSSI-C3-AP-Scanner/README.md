# WiFi Motion RSSI C3 AP Scanner

[Español](README.md) | [English](README.en.md)

Detector experimental de cambios compatibles con movimiento para un
ESP32-C3 SuperMini. Observa el RSSI de varios puntos de acceso cercanos sin
asociarse a ellos, crea su propia red local y muestra el resultado en una web.

No necesita la clave de las redes observadas, cámara, micrófono ni sensor
externo.

## Estado

Prototipo funcional compilado, probado y flasheado en un ESP32-C3 SuperMini:

- escaneo Wi-Fi pasivo y asíncrono;
- selección automática o manual de hasta ocho SSID;
- seguimiento interno por BSSID;
- calibración inicial y recalibración diferida desde la web;
- detector multirreferencia con umbral adaptativo;
- gráfica temporal, cobertura y progreso de confirmación;
- historial en memoria de las últimas 128 detecciones y descarga JSON/CSV;
- reloj local ajustable desde el navegador;
- portal cautivo y punto de acceso local permanente;
- configuración y referencias persistentes en NVS;
- telemetría JSON Lines por USB.

Es un detector experimental de cambios de propagación, no un detector de
presencia estática certificado. Una persona inmóvil puede no producir cambios
y otras alteraciones del entorno pueden modificar el RSSI.

## Cómo funciona

Durante la calibración recorre todos los canales, identifica BSSID estables y
guarda su RSSI mediano y MAD. Después escanea únicamente los canales necesarios
y combina las desviaciones normalizadas de las referencias visibles.

El umbral se calcula con una ventana fija de 32 scores tranquilos usando
mediana y MAD. Nunca baja del mínimo compilado (`2,50`), se congela durante una
posible detección o durante `MOTION` y, con el perfil predeterminado, basta un
barrido por encima del umbral. La pérdida de cobertura se informa
como `DEGRADED` o `NO_DATA`, no como movimiento.

## Inicio rápido con el firmware incluido

Los binarios listos para grabar están en [`firmware/`](firmware/). Requieren un
ESP32-C3 con flash de 4 MB y `esptool`:

```bash
python3 -m pip install esptool
```

Flasheo normal, conservando la configuración y las referencias guardadas:

```bash
PORT=/dev/ttyACM0 ./firmware/flash-prebuilt.sh
```

Instalación completa, borrando NVS y forzando una calibración nueva:

```bash
PORT=/dev/ttyACM0 ./firmware/flash-factory.sh
```

En sistemas donde el puerto tenga otro nombre, sustituya `/dev/ttyACM0`; por
ejemplo `/dev/ttyUSB0` en Linux o `COM5` en Windows. Los offsets, hashes y
archivos individuales se documentan en
[`firmware/README.md`](firmware/README.md).

## Conectarse al panel

El dispositivo mantiene esta red mientras detecta:

- Wi-Fi: `Motion-C3-Setup`
- clave Wi-Fi: `motion-c3-setup`
- página alternativa: `http://192.168.4.1`
- usuario web inicial: `admin`
- contraseña web inicial: `admin`

El portal cautivo intenta abrir el formulario automáticamente. Si no lo hace,
abra `http://192.168.4.1`. El usuario y la contraseña web pueden cambiarse
desde Configuración. La interfaz se abre en inglés de forma predeterminada y
los enlaces **English** y **Español** permiten cambiar de idioma sin modificar
la configuración.

La página muestra:

- estado grande: calibrando, sin movimiento, movimiento o cobertura insuficiente;
- gráfica de dos minutos con score, umbral adaptativo y detecciones;
- cobertura y número de referencias observadas;
- progreso de confirmación cuando se eligen dos o tres lecturas;
- tabla con las últimas 128 detecciones y descargas en JSON o CSV;
- buscador de redes y botones **+ Añadir** y **Quitar**.

Como el equipo no obtiene la hora de Internet, después de cada arranque la
sección **Fecha y hora** propone la hora local del teléfono u ordenador.
Confírmela para fechar las detecciones. El reloj y el historial viven en RAM:
se conservan mientras el ESP32 siga encendido y se borran al reiniciar. Si se
ajusta la hora después de una detección, el portal calcula también la hora de
los eventos anteriores de esa misma sesión.

La configuración expone únicamente perfiles sencillos de sensibilidad,
confirmación (1, 2 o 3 lecturas), velocidad, duración del estado de movimiento
(2, 4 u 8 segundos) y calibración (15, 25 o 40 escaneos). El perfil
predeterminado usa una lectura de confirmación y 25 escaneos de calibración.
**Guardar configuración** conserva las referencias; **Guardar y recalibrar**
las sustituye. **Restaurar ajustes de detección** no borra credenciales, redes
elegidas ni referencias.

En modo automático se eligen los BSSID más estables. En modo manual se pueden
añadir hasta ocho SSID desde los resultados de búsqueda. El ESP32 nunca intenta
conectarse a esas redes y no solicita sus contraseñas.

## Calibrar con el ambiente vacío

En **Calibración sin presencia**:

1. elija entre 5 y 300 segundos;
2. pulse **Salir y calibrar**;
3. abandone la zona durante la cuenta atrás;
4. permanezca fuera mientras avanza la calibración (25 barridos por defecto);
5. regrese cuando la web vuelva a mostrar **Sin movimiento**.

Al comenzar se eliminan las referencias anteriores, se vuelven a explorar
todos los canales y se construye una línea base nueva. Durante la cuenta atrás
el detector anterior continúa funcionando.

Una calibración rápida de 15 escaneos acaba antes, pero puede seleccionar
referencias menos estables; la normal usa 25 y la precisa 40. La elección se
aplica tanto a la primera calibración como a las solicitadas desde el portal.

## Compilar y probar

La revisión de ESP-IDF utilizada está registrada en
[`firmware/BUILD-INFO.txt`](firmware/BUILD-INFO.txt).

```bash
source /ruta/a/esp-idf/export.sh
./tools/build.sh
```

El script:

1. compila y ejecuta las pruebas host;
2. compila el firmware ESP32-C3;
3. genera la aplicación y la imagen completa;
4. actualiza `firmware/BUILD-INFO.txt` y `firmware/SHA256SUMS`.

También puede grabarse directamente desde ESP-IDF:

```bash
idf.py -p /dev/ttyACM0 flash
```

## Captura y diagnóstico

```bash
python3 tools/capture_jsonl.py \
  --port /dev/ttyACM0 \
  --output capturas/sesion-01.jsonl \
  --duration 300
```

Una pulsación corta de BOOT alterna el marcador experimental. Mantener BOOT
durante tres segundos vuelve a solicitar el portal, aunque en esta versión la
red local permanece activa normalmente. El LED integrado en GPIO8 queda
apagado en `IDLE`, encendido en `MOTION` y parpadea durante calentamiento o
pérdida de cobertura.

La salida USB utiliza el esquema `wifi_ap_scan/v1` e incluye registros `boot`,
`ap`, `scan`, `calibration`, `reference`, `detector`, `motion_event` y errores
explícitos.

## Documentación técnica

- [Estudio de viabilidad](ESTUDIO_VIABILIDAD.md)
- [Plan por fases](PLAN_POR_FASES.md)
- [Protocolo de captura](PROTOCOLO_CAPTURA.md)
- [Resultados de fase 1](RESULTADOS_FASE1.md)

## Funciones que no incluye

- No se asocia a los AP usados como referencia.
- No necesita ni almacena sus claves Wi-Fi.
- No envía avisos por Telegram porque opera sin conexión a Internet.
- No incluye actualización OTA; se actualiza por USB con los binarios de
  `firmware/`.
- No garantiza detectar una persona completamente inmóvil.
