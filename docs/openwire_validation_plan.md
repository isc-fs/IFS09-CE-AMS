# Protocolo de Validación en Banco HIL, ACU y Coche: Detección de Open-Wire con Debounce y Fast Re-check

**Documento:** `docs/openwire_validation_plan.md`  
**Equipo:** ICAI Speed Club — Formula Student  
**Subsistema:** Firmware AMS (`IFS09-CE-AMS`)  
**Rama:** `fix/ams-openwire-debounce-timing`  
**Normativa Aplicable:** Formula Student Rules (SDC apertura obligatoria $\le 500\text{ ms}$ ante fallo crítico).

---

## 1. Resumen Ejecutivo de la Estrategia

Para erradicar los falsos positivos durante transitorios de tracción/regeneración sin comprometer la seguridad ni superar el límite normativo de 500 ms:
1. **Filtro Temporal / Debounce de $N$ Muestras:** Ante una anomalía en el ciclo ADOW ($PU - PD < -\Delta V$), el estado pasa a `OW_SUSPECT` y no abre AIRs en la primera muestra.
2. **Fast Re-check Inmediato:** El evento `PollAdowFastDue` despierta inmediatamente a `BmsPollTask` para ejecutar un segundo ciclo ADOW (~28 ms después) en lugar de esperar los 200 ms del periodo nominal.
3. **Confirmación o Descarte:**
   - Si el fallo persiste durante $N$ ciclos consecutivos ($N = 2$ por defecto, configurable vía `OpenWireConfirmSamples`), se confirma `OW_FAULT`, se escribe `cell_open_mask` y `SafetyTask` desenergiza `AMS_OK` (abriendo el SDC en $< 310\text{ ms}$).
   - Si el transitorio desaparece en el re-chequeo, el estado vuelve a `OW_CLEAR` y el monoplaza continúa en pista sin interrupción.

---

## 2. Presupuesto Temporal (Timing Budget)

$$\begin{aligned}
T_{trip} &= T_{cadencia\_espera} + T_{ADOW\_1} + (N - 1) \cdot (T_{fast\_recheck} + T_{delay}) + T_{SafetyTask} + T_{AIR\_mecanico} \\
T_{trip}(N=2) &\le 200\text{ ms} + 28\text{ ms} + 28\text{ ms} + 10\text{ ms} + 40\text{ ms} = \mathbf{306\text{ ms}} \ll 500\text{ ms} \quad (\text{Margen: } 194\text{ ms}) \\
T_{trip}(N=3) &\le 200\text{ ms} + 28\text{ ms} + 56\text{ ms} + 10\text{ ms} + 40\text{ ms} = \mathbf{334\text{ ms}} \ll 500\text{ ms} \quad (\text{Margen: } 166\text{ ms})
\end{aligned}$$

---

## 3. Instrumentación con Osciloscopio / Analizador Lógico

Para perfilar con exactitud los retardos de conversión y propagación física isoSPI, activar `#define ENABLE_ADOW_PROFILING 1` en `ams_config.hpp` o en flags de compilación.

### Canales y Puntos de Medición:

| Canal Osciloscopio | Señal | Pin / Testpoint en AMS Lite | Propósito de Medición |
|---|---|---|---|
| **CH1** | `DEBUG_PROF` | `GPIOB PIN 1` (o TP asignado) | Profiling por registro `BSRR` (pulsos de ADC, isoSPI y cómputo MCU). |
| **CH2** | `LTC6820_CS` | `GPIOB PIN 9` (LTC6820 SPI CS) | Tiempo real de transferencias SPI entre STM32 y transceptor isoSPI. |
| **CH3** | `AMS_OK` / SDC | `GPIOB PIN 4` (AMS_OK salida) | Disparo del circuito de seguridad (caída de 3.3V/5V a 0V al abrir relé). |
| **CH4** | `AIR_AUX` / SDC Loop | Manguera SDC / Auxiliar AIR | Apertura mecánica real de los contactos del Shutdown Circuit. |

### Patrón de Pulsos del Pin de Profiling (`DEBUG_PROF`):
- **Pulso ADC:** Flanco de subida al iniciar `AdcvSettleMs` de ADOW (PUP y PDN). Duración esperada: $2 \times 3\text{ ms} = 6\text{ ms}$ por cada pase.
- **Pulso isoSPI:** Nivel alto durante la lectura en ráfaga de `RDCFGA` + `RDCVA..D` (320 bytes a 515.6 kHz). Duración esperada: ~8-9 ms por pase.
- **Pulso MCU:** Pulso estrecho (< 100 µs) durante la ejecución de `detect_open_conductors` y actualización del estado de debounce.

---

## 4. Registro y Monitorización en Telemetría CAN

Durante pruebas en rodillo o pista, es esencial auditar si la cadena experimenta pulsos espurios de `OW_SUSPECT` para cuantificar la inmunidad al ruido antes de que se produzcan disparos.

### Señales a Monitorizar en CAN / SD Logger:
1. **`PitDiagFsmStatus` (`0x6C0`):**
   - Byte 6: `fault_reason` (Código 16 = `CellOpenWire`).
   - Byte 7: `fault_detail` (Máscara de módulo ofensivo `1u << module`).
2. **`PitDiagTiming` (`0x6C1`):**
   - Byte 0-1: `last_volt_poll_ms` (duración del último ciclo de adquisición de tensión + ADOW).
   - Byte 2-3: `max_volt_poll_ms` (pico máximo de duración).
3. **Contador de Transitorios `OW_SUSPECT`:**
   - La telemetría en CAN / SD log refleja `open_wire_consecutive_fails` y `cell_open_suspect_mask`.
   - Criterio de aceptación en pista: Ante vibración y picos de inversor de 150-180 A, pueden aparecer eventos esporádicos de 1 muestra en `cell_open_suspect_mask`, pero deben resolver a 0 en la muestra siguiente sin incrementar a $N$ ni abrir `AMS_OK`.

---

## 5. Tabla de Validación Comparativa (HIL vs. ACU vs. Coche)

Rellenar esta tabla durante la campaña de validación para certificar la conformidad con Formula Student:

| Parámetro | Límite Teórico / FS | Banco HIL (Simulador) | ACU Banco Taller (Pack Real) | Monoplaza (Pista / Dinámica) |
|---|---|---|---|---|
| Tiempo 1er pase ADOW ($PU + PD$) | $\le 35\text{ ms}$ | | | |
| Tiempo isoSPI cadena (12 BMS / 10 LTC) | $\le 20\text{ ms}$ | | | |
| Latencia Fast Re-check ($T_{recheck}$) | $\le 35\text{ ms}$ | | | |
| Tasa de reintentos PEC en ADOW | $< 1\%$ | | | |
| Rechazo de pulso espurio (1 muestra) | SDC CERRADO | | | |
| Tiempo total hasta caída `AMS_OK` | $\le 450\text{ ms}$ | | | |
| Tiempo total hasta apertura física AIRs | $\le 500\text{ ms}$ | | | |

---

## 6. Checklist de Procedimiento en Taller / Coche

1. **Pre-requisitos:**
   - Firmware flasheado con la rama `fix/ams-openwire-debounce-timing`.
   - Osciloscopio con masa aislada conectada a la masa analógica/digital del AMS Lite.
   - Sonda CH1 en `DEBUG_PROF`, CH2 en `LTC6820_CS`, CH3 en `AMS_OK`, CH4 en bobina AIR.
2. **Prueba de Inmunidad a Ruido:**
   - Arrancar monoplaza en estado `Ready to Drive` / `Run`.
   - Ejecutar aceleraciones bruscas en vacío o rampa de corriente en banco de tracción.
   - Verificar en el log CAN que `cell_open_mask` se mantiene en 0 y que el SDC no cae.
3. **Prueba de Disparo Físico de Seguridad (Inyección Real):**
   - Con el acumulador desenergizado y seguro (MSD extraído), desconectar una toma de celda intermedia (ej. Pin 3 de LTC_1 en Módulo 2) mediante conector de test.
   - Reinsertar MSD y activar monoplaza.
   - Trigger en osciloscopio por flanco de bajada de `AMS_OK`.
   - **Verificación obligatoria:** El tiempo desde el inicio del comando ADOW que detecta el corte hasta la caída a nivel bajo de `AMS_OK` debe ser estrictamente $\le 450\text{ ms}$.
