# Resultado de la fase 3

Estado: **APTO**.

## Funcionalidad

- Portal HTTP local para editar credenciales Wi-Fi y todos los parámetros del
  detector sin recompilar.
- API JSON versionada para consultar, guardar, importar, exportar y restaurar.
- Exportación segura por defecto; los secretos solo se incluyen con una opción
  explícita.
- Diagnóstico con configuración efectiva, estado, RSSI y contadores de salud.
- AP de recuperación cuando faltan credenciales o falla la conexión STA.
- Persistencia NVS con formato fijo v3 y migración automática desde el blob v2.
- Reinicio controlado después de guardar para aplicar una configuración completa.
- Telemetría USB encolada y no bloqueante para que un host que no lea el puerto
  serie no detenga el servidor en el ESP32-C3 de un solo núcleo.

## Validación

```text
4/4 pruebas host superadas
4/4 pruebas superadas con AddressSanitizer y UndefinedBehaviorSanitizer
firmware CSV compilado
firmware JSON Lines compilado
```

Las pruebas con NVS simulado cubren valores predeterminados, validación de
credenciales, guardado/carga, corrupción, borrado y migración v2 a v3.

En la placa se cambió el intervalo de 100 a 110 ms mediante HTTP, se comprobó
el valor después del reinicio y se restauró a 100 ms mediante un segundo
reinicio. También se validaron rechazo HTTP 400, importación, exportación normal
y explícita con secretos, portal HTML y diagnóstico.

Prueba final sin lector serie:

```text
uptime: 59,5 s
muestras: 576/576
calibrado: sí
estado: idle
errores de lectura: 0
pérdidas de planificación: 0
desconexiones/reconexiones: 0/0
```

Binario CSV: `0xd2940` bytes, con 45 % libre en la partición de aplicación.

La restauración de fábrica no se invocó físicamente para no borrar las
credenciales reales de la placa. El borrado de ambos esquemas NVS está cubierto
por prueba host y `esp_wifi_restore()` está integrado y compilado en el handler.
