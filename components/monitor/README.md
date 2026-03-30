# Monitor

## Overview

The monitor component is a continuously-running FreeRTOS task that samples four ADC channels on an ADS1115 (I2C address 0x49) in single-shot mode, converts the raw readings into forward power, reflected power, SWR, and two PA temperature values, and publishes the results to the shared `system_state` blackboard every cycle (~500 ms). When any reading exceeds a configurable threshold, the monitor injects a fault event into the sequencer's event queue, triggering the sequencer's protection logic (immediate transition to fault-latched state and PA de-energisation).

The component owns the I2C master bus and both ADS1115 chip handles. It is the sole producer of sensor data in the system and one of three event producers (alongside PTT and buttons) that feed the sequencer FSM.

## Channel Map

| ADS1115 Channel | Signal          | Physical Input              |
|-----------------|-----------------|-----------------------------|
| AIN0            | Forward power   | Directional coupler forward |
| AIN1            | Reflected power | Directional coupler reverse |
| AIN2            | Temperature 1   | NTC thermistor (PA)         |
| AIN3            | Temperature 2   | NTC thermistor (PA)         |

Both ADS1115 chips are initialised with PGA +/-4.096 V. Only chip 1 (0x49) is actively sampled; chip 0 (0x48) is initialised but reserved for future expansion.

## Key Data Structures

### Configuration (`app_config_t` fields consumed)

At init, the monitor snapshots the full `app_config_t` via `config_snapshot()`. Runtime updates arrive automatically through the registered `config_register_apply_cb()` callback, which writes to the module-level copy under a spinlock. The task takes a local snapshot of this copy at the start of each ADC cycle. The fields consumed:

| Field                       | Purpose                                          | Default  |
|-----------------------------|--------------------------------------------------|----------|
| `fwd_power_cal_factor`      | Calibration: `P_fwd = factor * V^2`              | 1.0      |
| `ref_power_cal_factor`      | Calibration: `P_ref = factor * V^2`              | 1.0      |
| `swr_fault_threshold`       | SWR above this triggers `SEQ_FAULT_HIGH_SWR`     | 3.0      |
| `temp1_fault_threshold_c`   | Temp1 above this triggers `SEQ_FAULT_OVER_TEMP1` | 65.0 C   |
| `temp2_fault_threshold_c`   | Temp2 above this triggers `SEQ_FAULT_OVER_TEMP2` | 65.0 C   |
| `thermistor_beta`           | Steinhart-Hart beta coefficient                  | 3950     |
| `thermistor_r0_ohms`        | NTC resistance at 25 C (T0)                      | 100k ohm |
| `thermistor_r_series_ohms`  | Series resistor in the voltage divider           | 100k ohm |

### Internal State

- **`s_cfg`** (`app_config_t`) -- Module-level copy of configuration, updated under `s_cfg_mux` spinlock.
- **`s_cfg_mux`** (`portMUX_TYPE`) -- Spinlock protecting reads and writes to `s_cfg`. Used by `monitor_update_config()` (writer) and the task loop (reader) to ensure the task always sees a consistent configuration snapshot.
- **`s_bus`** (`i2c_master_bus_handle_t`) -- The I2C master bus, owned and created by monitor.
- **`s_chip[2]`** (`ads1115_handle_t`) -- Handles for both ADS1115 devices. Index 0 = 0x48 (reserved), index 1 = 0x49 (active).
- **`s_adc_queue`** (`QueueHandle_t`) -- FreeRTOS queue (depth 8) carrying chip-index bytes from the ALERT/RDY ISR. Used to synchronise single-shot conversions.
- **`s_adc_mutex`** (`SemaphoreHandle_t`) -- Mutex arbitrating ADC access between the monitor task loop and external callers (e.g. CLI `adc` command via `monitor_read_channel()`).

### Published State (via `system_state`)

Each cycle, the monitor calls `system_state_set_sensors()` to publish:

```c
system_state_set_sensors(fwd_power_w, ref_power_w, swr, temp1_c, temp2_c);
```

These values are immediately available to any consumer calling `system_state_get()` (display, CLI, web server).

## Conversion Mathematics

### Power

```
P = cal_factor * V_adc^2
```

Squared-voltage relationship assumes a power detector whose output voltage is proportional to the square root of RF power. The cal_factor absorbs coupler loss, detector sensitivity, and unit scaling.

### SWR

```
gamma = V_reflected / V_forward
SWR   = (1 + gamma) / (1 - gamma)
```

SWR checking is suppressed when forward power is below 0.1 W (`MIN_FWD_POWER_FOR_SWR_W`) to avoid false alarms from noise. If gamma >= 1.0 (total or over-reflection), SWR is clamped to 99.9.

### Temperature (Steinhart-Hart beta equation)

The thermistor circuit is: VCC (3.3 V) -> R_series -> NTC -> GND. The ADC measures the voltage across the NTC.

```
R_ntc = R_series * V_adc / (VCC - V_adc)
T_K   = 1 / ( 1/298.15 + ln(R_ntc / R0) / beta )
T_C   = T_K - 273.15
```

Returns NAN if the voltage is at the rail (open/short circuit NTC), which suppresses the fault check via the `!isnan()` guard.

## Event Flow

### Normal Monitoring Cycle

```
1. monitor_task snapshots s_cfg into a local app_config_t under s_cfg_mux spinlock
2. monitor_task acquires s_adc_mutex
3. For each channel (AIN0..AIN3):
   a. Drain stale ALERT events from s_adc_queue
   b. Trigger single-shot conversion via I2C
   c. Block on s_adc_queue (250 ms timeout) waiting for ALERT/RDY ISR
   d. Read raw ADC value via I2C, convert to voltage
4. Compute power, SWR, temperatures from voltages using the local config snapshot
5. Publish to system_state after power readings, then after each temp
6. Check each value against its fault threshold (from the local snapshot)
7. Release s_adc_mutex
8. vTaskDelay(10 ms) -- yield window for external ADC callers
9. Repeat
```

### Fault Detection

When a threshold is breached, the monitor constructs a `seq_event_t` with type `SEQ_EVENT_FAULT` and sends it to the sequencer's event queue via `xQueueSend`. The sequencer then transitions to `SEQ_STATE_FAULT`, de-energises the PA relay, and latches until a manual clear.

Three fault types can be raised:
- `SEQ_FAULT_HIGH_SWR` -- SWR exceeds `swr_fault_threshold` while forward power >= 0.1 W
- `SEQ_FAULT_OVER_TEMP1` -- Temperature 1 exceeds `temp1_fault_threshold_c`
- `SEQ_FAULT_OVER_TEMP2` -- Temperature 2 exceeds `temp2_fault_threshold_c`

### External ADC Access

`monitor_read_channel()` allows any task to perform an ad-hoc single-channel read (e.g. the CLI `adc` command). It acquires the same `s_adc_mutex` with a 1-second timeout, so it blocks until the monitor's current cycle yields the mutex during its 10 ms delay window.

## Architecture Decisions

- **ISR-driven conversion synchronisation.** Rather than polling the ADS1115 status register over I2C, the component configures the ALERT/RDY pin as a conversion-ready signal and uses a GPIO ISR to post to a FreeRTOS queue. This avoids busy-wait I2C traffic and provides a clean blocking wait with a timeout for detecting hardware faults.

- **Single-shot mode over continuous mode.** The ADS1115 is operated in single-shot mode, reading each of the four channels sequentially. This gives explicit control over channel multiplexing order and timing, avoids stale-data ambiguity inherent in continuous mode with MUX changes, and allows the conversion timeout to serve as a hardware health check.

- **Mutex-based ADC sharing.** The monitor task holds the ADC mutex for the duration of its four-channel sweep, then explicitly yields for 10 ms. Without this yield, the monitor would immediately re-acquire the mutex and starve the CLI. The 10 ms window is a deliberate trade-off: short enough to maintain ~500 ms cycle time, long enough for a CLI command to slip in.

- **System state updated incrementally.** `system_state_set_sensors()` is called after power readings and then again after each temperature reading, rather than once at the end. This means consumers see partial updates mid-cycle, but it ensures power/SWR data is available as early as possible -- important for the sequencer's fault response latency.

- **Spinlock-protected config with per-cycle snapshots.** `monitor_update_config()` writes to `s_cfg` under a `portMUX_TYPE` spinlock (`s_cfg_mux`), and the task loop reads `s_cfg` under the same spinlock into a stack-local `app_config_t` at the top of each ADC cycle. This guarantees the entire four-channel sweep uses a single consistent set of thresholds and calibration values, even if a config update arrives mid-cycle. The spinlock is lightweight (critical section, no context switch) and held only for the duration of a `memcpy`.

- **Self-registering config callback.** During `monitor_init()`, the monitor registers `monitor_update_config` as a config apply callback via `config_register_apply_cb()`. This decouples the monitor from the config component's apply workflow -- the monitor does not need to be explicitly called by whoever triggers a config apply; the config component invokes it automatically.

- **Parameterless init with internal config snapshot.** `monitor_init()` takes no arguments. It calls `config_snapshot(&s_cfg)` to populate its initial config from the global config state. This eliminates the need for callers to pass a config pointer, reducing coupling between the init call-site and the config component.

- **Chip 0 initialised but unused.** The second ADS1115 at address 0x48 is initialised at startup with its ALERT GPIO configured as input-with-pullup but no ISR. This reserves the hardware path for future expansion without requiring a firmware change to the init sequence.

## Dependencies

| Component      | Role                                                       |
|----------------|------------------------------------------------------------|
| `ads1115`      | I2C driver for ADS1115 16-bit ADC                          |
| `config`       | `app_config_t` type definition, calibration/threshold data |
| `sequencer`    | Fault event queue (`sequencer_get_event_queue()`)          |
| `system_state` | Blackboard for publishing sensor readings                  |
| `hw_config`    | GPIO pin assignments, I2C port/address constants           |
| `driver`       | ESP-IDF GPIO and I2C master drivers                        |
| `freertos`     | Task, queue, semaphore, delay primitives                   |

## Public API

```c
// Initialise I2C bus, both ADS1115 chips, ALERT GPIO ISRs.
// Snapshots config internally and registers as a config apply callback.
// Must be called after config_init() and sequencer_init().
esp_err_t monitor_init(void);

// FreeRTOS task entry point. Stack: 4096, Priority: 7.
void monitor_task(void *arg);

// Update config (thresholds, calibration, thermistor params).
// Called automatically by the config apply callback mechanism.
// Also safe to call directly from any task.
esp_err_t monitor_update_config(const app_config_t *cfg);

// Ad-hoc single-channel read (blocking, mutex-protected, 1s timeout).
esp_err_t monitor_read_channel(ads1115_channel_t ch, float *out_voltage);
```

## Usage Notes

- **Init ordering matters.** `monitor_init()` must be called after both `config_init()` and `sequencer_init()`. It depends on `config_init()` because it calls `config_snapshot()` and `config_register_apply_cb()` during init. It depends on `sequencer_init()` because it uses `sequencer_get_event_queue()` at runtime (during fault injection). The sequencer queue handle is fetched on each fault, not cached at init, so the sequencer just needs to be initialised before the monitor task starts running.

- **Config updates are automatic.** After init, the monitor receives config changes automatically via its registered apply callback. Callers do not need to explicitly call `monitor_update_config()` when config is applied through the normal config workflow -- the config component handles dispatch.

- **Cycle time is approximately 500 ms.** Four channels at 8 SPS (125 ms per conversion) plus I2C overhead and the 10 ms yield delay. This is not configurable at runtime.

- **The 250 ms conversion timeout is a hard-coded safety net.** If the ALERT/RDY ISR never fires (e.g. I2C bus lockup, chip failure), the channel read returns -1.0f and that channel's readings are skipped for the cycle. No fault is injected for ADC hardware failure -- only for threshold breaches on successfully-read values.

- **SWR is not checked below 0.1 W forward power.** This prevents false SWR faults from ADC noise when the transmitter is off or at very low power.

- **Temperature NAN suppresses fault checks.** If the thermistor is open-circuit or shorted (voltage at rail), the conversion returns NAN, and the `!isnan()` guard prevents a spurious fault. The NAN value is still written to system_state, so consumers should handle it.

- **The monitor owns the I2C bus.** No other component should create an I2C master bus on `I2C_NUM_0`. Other components needing I2C access should go through `monitor_read_channel()` or a future bus-sharing API.
