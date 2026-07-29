# Plan alternativo — Procesamiento matemático de señal RSSI

## Propósito

Este plan investiga si una transformación matemática más rica del RSSI escalar puede superar al detector base de media de diferencias absolutas, calibración estadística e histéresis.

No sustituye al plan principal. Se ejecutará como una comparación A/B sobre las mismas capturas, con idénticas particiones de entrenamiento y evaluación.

## Hipótesis

El detector inicial resume cada ventana con una única medida de variación. Puede perder información contenida en:

- cambios de curvatura y pendiente;
- perturbaciones coherentes en varias escalas temporales;
- pequeños cambios persistentes que no superan un umbral instantáneo;
- variaciones de energía local;
- cambios en la organización temporal con media y varianza similares;
- desviaciones respecto al subespacio dinámico de la habitación vacía.

La hipótesis principal es que una combinación de curvatura multiescala, energía no lineal y acumulación secuencial detectará transitorios más débiles y reducirá falsas alarmas frente al detector inicial.

La presencia estática solo se considerará demostrada si las características de una persona quieta se separan de las capturas vacías en sesiones y días distintos. Recordar una entrada no cuenta como evidencia física de permanencia.

## Resultado esperado frente al plan inicial

Se espera una mejora probable en:

- detección de entradas, salidas y cruces débiles;
- latencia de detección;
- robustez frente a impulsos aislados;
- adaptación a diferentes escalas temporales;
- explicación del motivo de cada alarma;
- capacidad para decidir objetivamente si el RSSI escalar contiene información suficiente.

No se presupone una mejora en presencia completamente inmóvil. Esa capacidad queda sujeta a evidencia experimental.

## Detector de referencia

El detector principal se congela como baseline:

- ventana de RSSI;
- media de diferencias absolutas consecutivas;
- calibración en reposo;
- umbral adaptativo;
- confirmación e histéresis.

Ningún algoritmo alternativo se considerará mejor si no supera este baseline en sesiones de evaluación no utilizadas para ajustar parámetros.

---

## Fase A0 — Contrato experimental

### Objetivos

- Definir escenarios, etiquetas, métricas y particiones antes de modificar algoritmos.
- Evitar ajustar y evaluar con las mismas capturas.
- Establecer límites de recursos del ESP32-C3.

### Escenarios mínimos

1. Habitación vacía en varias horas y días.
2. Entrada y salida cruzando el enlace.
3. Movimiento lateral, cerca y lejos del enlace.
4. Persona quieta durante al menos diez minutos.
5. Puertas, ventilador, tráfico Wi-Fi y objetos en movimiento.
6. Cambios normales del punto de acceso y reconexiones.

### Métricas

- precisión;
- recall;
- F1;
- falsos positivos por hora;
- latencia p50, p95 y máxima;
- tiempo de liberación;
- estabilidad entre sesiones;
- CPU, RAM y tiempo por muestra.

### Criterio de salida

Protocolo versionado y un baseline reproducible con parámetros congelados.

---

## Fase A1 — Auditoría de la señal disponible

### Objetivos

Determinar qué información entrega realmente `esp_wifi_sta_get_ap_info()` y a qué frecuencia efectiva cambia.

### Medidas

- intervalo real entre lecturas;
- intervalo entre cambios de valor RSSI;
- porcentaje de muestras repetidas;
- número de niveles RSSI distintos por minuto;
- autocorrelación;
- distribución de primeras y segundas diferencias;
- deriva térmica y temporal;
- Allan deviation por escala;
- respuesta ante cambios de tráfico generado.

### Decisiones

- Seleccionar frecuencia de consulta sin contar lecturas repetidas como observaciones independientes.
- Identificar las escalas temporales donde domina ruido, deriva o perturbación ambiental.
- Definir un preprocesamiento de valores repetidos y muestras perdidas.

### Criterio de salida

Informe que determine la frecuencia informativa efectiva y las escalas candidatas.

### Criterio de abandono

Si el RSSI permanece prácticamente constante durante perturbaciones etiquetadas, no se invertirá esfuerzo en operadores más complejos y se adelantará CSI.

---

## Fase A2 — Detector de curvatura multiescala

### Objetivo

Trasladar al dominio temporal la idea de detección de bordes mediante derivadas suavizadas.

### Operadores

Para escalas `s` configurables:

```text
D1_s[n] = (x[n+s] - x[n-s]) / (2s)
D2_s[n] = (x[n+s] - 2x[n] + x[n-s]) / s²
```

Se compararán:

- diferencias directas;
- derivadas Savitzky-Golay;
- primera derivada de gaussiana;
- segunda derivada de gaussiana o wavelet Mexican Hat;
- máximos persistentes entre escalas.

Cada salida se normalizará mediante mediana y desviación absoluta mediana del reposo:

```text
z = (feature - median) / (1.4826 * MAD + epsilon)
```

### Pruebas

- escalas de 100 ms a varios segundos;
- ruido sintético y cuantización entera;
- impulsos aislados;
- rampas, escalones y cruces reales;
- persistencia de máximos entre escalas.

### Criterio de salida

Mejorar el recall o la latencia del baseline sin aumentar más de un 10 % los falsos positivos por hora.

---

## Fase A3 — Energía local y detección secuencial

### Objetivo

Detectar oscilaciones débiles y acumular cambios pequeños persistentes.

### Características

- operador de energía Teager-Kaiser sobre señal centrada;
- razón de varianza corta/larga;
- energía de primera y segunda derivada;
- rango y MAD robusta por ventana.

Operador Teager-Kaiser:

```text
Psi[n] = z[n]² - z[n-1] * z[n+1]
```

El baseline `z[n]` solo se actualizará durante estados considerados de reposo.

### Detectores secuenciales

- CUSUM positivo y negativo;
- CUSUM de cambio de varianza;
- EWMA como referencia simple;
- detector GLR offline para comparar límites.

### Criterio de salida

Detectar cambios pequeños acumulados que el baseline no detecte, manteniendo `<= 1` falso positivo por hora en el perfil equilibrado.

---

## Fase A4 — Estructura temporal no lineal

### Objetivo

Comprobar si existen diferencias de dinámica que no aparezcan en media, dispersión o derivadas.

### Métodos offline

- entropía de permutación con tratamiento explícito de empates;
- probabilidades de patrones ordinales de longitud 3 y 4;
- tasa de cambios de dirección;
- irreversibilidad temporal;
- curtosis y curtosis espectral;
- sample entropy solo si la longitud y cuantización lo permiten.

### Restricción

Estas características no se llevarán al firmware hasta demostrar ganancia estable entre sesiones.

### Criterio de salida

Seleccionar únicamente características cuya separación se mantenga en días y posiciones diferentes.

---

## Fase A5 — Modelo de habitación vacía mediante subespacios

### Objetivo

Determinar si una ventana con perturbación o persona quieta deja de pertenecer a la dinámica habitual del entorno vacío.

### Método principal

- construir matrices de Hankel con ventanas RSSI;
- aplicar Singular Spectrum Analysis o SVD;
- estimar un subespacio de baja dimensión con capturas vacías;
- medir la energía residual de cada nueva ventana:

```text
R(y) = ||y - U_r * U_r^T * y||²
```

### Métodos de contraste

- PCA sobre vectores de características;
- Matrix Profile para descubrir subsecuencias discordantes;
- distancia Mahalanobis robusta;
- Isolation Forest únicamente como referencia offline.

### Prueba crítica de presencia estática

Comparar exclusivamente:

- habitación vacía en sesiones no vistas;
- persona quieta después de los primeros 30 segundos;
- persona quieta después de cinco minutos.

Los transitorios de entrada y salida se excluirán de esta evaluación.

### Criterio de éxito

Separación repetible entre vacío y quieto con F1 `>= 0.80` en días no vistos.

### Criterio de abandono

Si no supera F1 `0.65` entre sesiones, se concluirá que el RSSI escalar no ofrece evidencia estable de presencia quieta para esa geometría.

---

## Fase A6 — Selección y fusión de características

### Objetivo

Construir el detector alternativo mínimo que aporte una mejora real.

### Proceso

1. Normalizar todas las características con estadísticas robustas.
2. Medir información mutua y redundancia.
3. Ejecutar ablaciones: retirar una característica cada vez.
4. Comparar suma ponderada, regresión logística y modelo de dos estados.
5. Rechazar cualquier característica cuya mejora no se reproduzca.

### Vector candidato

```text
[curvatura_multiescala,
 energía_Teager,
 razón_varianza,
 CUSUM,
 residuo_subespacio]
```

### Reglas

- No usar una red neuronal en esta fase.
- No optimizar pesos con el conjunto de evaluación.
- Mantener una versión explicable con umbrales visibles.

### Criterio de salida

Detector alternativo con como máximo cinco características y mejora estadísticamente consistente frente al baseline.

---

## Fase A7 — Implementación ESP32-C3

### Arquitectura

Crear un frontend de señal intercambiable:

```text
RSSI source
  -> preprocessing
  -> feature bank
  -> sequential detector
  -> occupancy state
  -> telemetry
```

### Configuración

- selección de detector `baseline` o `advanced`;
- escalas temporales;
- longitud Savitzky-Golay;
- activación de Teager-Kaiser;
- parámetros CUSUM;
- política de actualización del baseline;
- pesos de fusión;
- perfiles `low`, `balanced` y `high`.

### Límites iniciales

- memoria estática;
- sin asignaciones dinámicas en el bucle de muestreo;
- tiempo de procesamiento menor al 20 % del intervalo de muestra;
- telemetría de tiempos y desbordamientos;
- posibilidad de compilar cada característica de forma opcional.

### Pruebas

- vectores dorados generados en Python;
- igualdad numérica dentro de tolerancia entre Python y C;
- fuzzing de configuración;
- ASan/UBSan en host;
- sesiones largas sobre hardware.

### Criterio de salida

Paridad con el prototipo Python y cero pérdidas sostenidas de muestras.

---

## Fase A8 — Inferencia de estado y presencia

### Objetivo

Separar claramente la detección física de eventos de la memoria lógica de ocupación.

### Estados

```text
VACIO -> ENTRADA_PROBABLE -> OCUPADO -> SALIDA_PROBABLE -> VACIO
```

### Evidencias

- transitorios multiescala;
- evidencia secuencial acumulada;
- residuo respecto al subespacio vacío;
- tiempo desde el último evento;
- confianza independiente para movimiento y ocupación.

### Salidas

```text
motion_probability
occupancy_probability
physical_evidence_age_ms
last_transition
```

### Regla de honestidad

Cuando solo exista memoria de una entrada, el estado debe exponerse como `ocupación inferida`, no como `presencia físicamente observada`.

### Criterio de salida

Transiciones consistentes y ausencia de confusión entre evento detectado y presencia medida.

---

## Fase A9 — Comparación A/B final

### Detectores

- `B0`: detector inicial;
- `B1`: mejor estadística simple por ventana;
- `A1`: curvatura multiescala;
- `A2`: curvatura + Teager + CUSUM;
- `A3`: fusión final;
- `A4`: fusión con residuo SSA, si es viable.

### Evaluación

- validación por sesión y por día;
- bootstrap de intervalos de confianza;
- curvas precision-recall;
- comparación a igual tasa de falsos positivos;
- análisis de errores por escenario;
- costes de CPU, RAM y energía.

### Condición para adoptar el plan alternativo

El detector avanzado debe cumplir simultáneamente:

- mejora absoluta de F1 de al menos `0.05` frente al baseline;
- o reducción de al menos `30 %` de falsos positivos a igual recall;
- latencia p95 no superior al baseline en más de `250 ms`;
- funcionamiento estable en sesiones no vistas;
- coste compatible con ESP32-C3.

### Condición para conservar el detector inicial

Si la mejora es menor o depende de una sola sesión, se mantendrá el baseline por simplicidad.

---

## Fase A10 — Decisión RSSI o CSI

### Continuar con RSSI cuando

- el detector avanzado cumpla los criterios A/B;
- la geometría objetivo sea fija;
- la presencia estática no sea un requisito duro;
- la tasa de falsas alarmas sea aceptable.

### Migrar a CSI cuando

- el RSSI no cambie con perturbaciones relevantes;
- vacío y persona quieta no sean separables entre sesiones;
- el resultado dependa excesivamente del día o del tráfico;
- se necesite cobertura uniforme, localización o actividad fina.

El trabajo matemático no se pierde al migrar: derivadas multiescala, CUSUM, entropías y análisis de subespacios pueden reutilizarse sobre amplitudes y fases CSI, donde existe una observación de mayor dimensión.

---

## Orden recomendado de implementación

1. Auditoría de señal y dataset.
2. Segunda derivada multiescala.
3. Normalización robusta.
4. CUSUM de curvatura y varianza.
5. Teager-Kaiser.
6. Evaluación A/B inicial.
7. SSA y Matrix Profile offline.
8. Fusión mínima.
9. Portado a ESP32-C3.
10. Decisión RSSI/CSI.

## Riesgos

- El RSSI puede actualizarse a una frecuencia menor que la frecuencia de consulta.
- La cuantización puede dominar derivadas y patrones ordinales.
- Un algoritmo complejo puede ajustarse a deriva o tráfico y no a la presencia física.
- La persona quieta puede no dejar una firma observable en RSSI escalar.
- Una mejora offline puede no compensar el coste embebido.

## Conclusión

Este plan tiene una probabilidad razonable de superar al detector inicial para movimiento y cambios débiles. Su mayor valor adicional es convertir la pregunta sobre presencia estática en una prueba falsable: o encontramos una firma reproducible entre sesiones, o demostramos que el RSSI escalar es insuficiente y migramos a CSI sin seguir ajustando umbrales indefinidamente.
