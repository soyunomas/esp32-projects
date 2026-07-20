# Resultado de la fase 1

Fecha: 2026-07-20  
Placa: ESP32-C3, revisión 0.4, flash 4 MB  
Interfaz: USB Serial/JTAG (`/dev/ttyACM0`)  
Red: 2,4 GHz, canal 1  
Intervalo configurado: 100 ms  

## Resultado

La captura física superó el criterio de salida de la fase 1:

| Métrica | Resultado |
|---|---:|
| Duración útil | 923,0 s |
| Muestras correctas | 9.231 |
| Errores de lectura | 0 |
| Intervalo de consulta mediano | 100,0 ms |
| Intervalo mediano entre cambios RSSI observados | 1.200,0 ms |
| Muestras con RSSI repetido | 8.960 (97,1 %) |
| Cambios RSSI observados | 270 |
| Pérdidas de planificación estimadas | 0 |
| Desconexiones / reconexiones | 0 / 0 |
| Racha máxima de errores | 0 ms |
| Reinicios detectados | 0 |

Resultado del validador: `APTO`.

La diferencia entre 100 ms de consulta y 1.200 ms entre cambios observados
confirma que consultar la API con mayor frecuencia no implica recibir un valor
RSSI nuevo en cada llamada.

## Reproducción

```bash
timeout 925s minicom -D /dev/ttyACM0 -b 115200 \
  -C phase1-session.log -o
python3 tools/validate_phase1.py phase1-session.log
```

El archivo de captura no se versiona (`*.log`). La sesión se compiló localmente
con ESP-IDF `v6.1-dev-2938-g12f36a021f`; la compilación fijada del proyecto sigue
siendo ESP-IDF `v6.0.2` en CI. Este ensayo valida estabilidad y cadencia, no las
métricas de detección de movimiento de la fase experimental.
