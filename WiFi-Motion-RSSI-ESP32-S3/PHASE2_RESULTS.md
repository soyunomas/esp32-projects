# Resultado de la fase 2

Estado: **APTO**.

## Alcance validado

- Cinco extractores sobre la misma ventana: diferencia absoluta media,
  desviación estándar, varianza muestral, rango y desviación absoluta mediana.
- Baseline clásico por media/desviación estándar y robusto por mediana/MAD.
- Perfiles `low`, `balanced` y `high`, histéresis independiente y adaptación
  lenta del baseline únicamente en reposo.
- Selección reproducible mediante Kconfig y configuración persistente versionada.
- Telemetría comparable con algoritmo, baseline, perfil, score y umbrales de
  activación y liberación.

## Pruebas

El conjunto host ejecuta el mismo escenario estable y perturbado para las diez
combinaciones de extractor y baseline. También comprueba perfiles, histéresis,
adaptación solo en reposo, límites de configuración y métricas de muestreo.

```text
3/3 pruebas host superadas
3/3 pruebas superadas con AddressSanitizer y UndefinedBehaviorSanitizer
```

## Compilación y prueba física

- Placa: ESP32-C3 SuperMini, silicio revision 0.4, flash de 4 MB.
- Compilación local: ESP-IDF 6.1 de desarrollo.
- Versión canónica del proyecto y CI: ESP-IDF v6.0.2.
- Binario: 0xbd8f0 bytes; 51 % libre en la partición de aplicación.
- Captura física corta: 232 muestras durante 23,1 segundos a unos 100 ms.
- Resultado: calibración completada, estado `idle`, cero errores de lectura,
  pérdidas de planificación, desconexiones o reinicios observados.

La captura física confirma el formato y la ejecución integrados. La comparación
determinista de todos los algoritmos corresponde a las pruebas host, para que
cada variante reciba exactamente la misma señal de entrada.
