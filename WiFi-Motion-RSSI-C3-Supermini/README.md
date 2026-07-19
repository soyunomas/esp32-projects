# WiFi Motion RSSI — ESP32-C3 SuperMini

Detector experimental de movimiento basado en las variaciones del RSSI del punto de acceso al que está conectado un ESP32-C3. No identifica personas ni detecta de forma fiable a alguien completamente inmóvil.

## Estado

Primera implementación funcional:

- ESP-IDF `v6.0.2`, objetivo `esp32c3`.
- Muestreo de RSSI mediante `esp_wifi_sta_get_ap_info()`.
- Detector con calibración estadística, umbral adaptativo e histéresis.
- Salida CSV o JSON Lines por USB Serial/JTAG.
- Configuración con `idf.py menuconfig` y backend NVS versionado.
- Pruebas host y CI.

Consulta [PLAN.md](PLAN.md) para las fases y criterios de aceptación.

## Configuración rápida

```bash
./tools/bootstrap.sh
source "$HOME/esp/esp-idf-v6.0.2/export.sh"
idf.py set-target esp32c3
idf.py menuconfig
./tools/build.sh
PORT=/dev/ttyACM0 ./tools/flash.sh
```

En `menuconfig`, abre **WiFi motion detector** y configura al menos el SSID y la contraseña.

## Parámetros

| Parámetro | Predeterminado | Función |
|---|---:|---|
| Intervalo | 100 ms | Frecuencia de muestreo RSSI |
| Ventana | 20 muestras | Duración aproximada del score |
| Calibración | 120 scores | Baseline inicial en reposo |
| Multiplicador sigma | 6.0 | Sensibilidad adaptativa |
| Umbral mínimo | 0.30 dB/muestra | Evita umbrales degenerados |
| Disparo | 3 scores | Confirmación de movimiento |
| Liberación | 8 scores | Histéresis al volver a reposo |

Durante la calibración, no debe moverse nadie entre el punto de acceso y el ESP32.

## Formato CSV

```text
t_ms,rssi_dbm,score,threshold,state,calibrated
12540,-61,0.4211,0.7832,idle,1
12640,-54,1.2105,0.7832,motion,1
```

## Pruebas host

```bash
./tools/test-host.sh
```

Las pruebas validan configuración, calibración estable, activación por una secuencia perturbada, histéresis, umbral mínimo y una sesión sintética larga sin falsos positivos.

## Limitaciones iniciales

- El SSID y la contraseña se configuran por `menuconfig` en esta fase. El portal y la edición NVS llegan en fase 2.
- La detección depende de la geometría, multitrayecto, interferencias y tráfico del AP.
- No es un sistema de seguridad ni un detector certificado de ocupación.
- El GPIO 8 se usa como LED activo-bajo por defecto; se puede desactivar o cambiar en `menuconfig`.
