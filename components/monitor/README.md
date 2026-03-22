# Monitor

## Overview

The monitor component is a periodic ADC sensing task that reads forward/reflected RF power and two thermistor temperatures from an ADS1115 16-bit ADC using single-shot conversions over I2C. It converts raw voltages into engineering units (watts, SWR, degrees Celsius), checks them against configurable fault thresholds, and injects fault events into the sequencer's event queue when a threshold is breached. Live readings are published to the `system_state` blackboard and also exposed via thread-safe getters.

## Key Data Structures

### Configuration (`app_config_t` fields consumed)

| Field | Default | Purpose |
|---|---|---|
| `fwd_power_cal_factor` | 1.0 | Multiplier applied to V-squared for forward power (W) |
| `ref_power_cal_factor` | 1.0 | Multiplier applied to V-squared for reflected power (W) |
| `swr_fault_threshold` | 3.0 | SWR above which a `SEQ_FAULT_HIGH_SWR` is sent |
| `temp_fault_threshold_c` | 65.0 | Temperature (C) above which an over-temp fault is sent |
| `thermistor_beta` | 3950 | NTC beta coefficient for Steinhart-Hart equation |
| `thermistor_r0_ohms` | 100000 | NTC resistance at 25 C |
| `thermistor_r_series_ohms` | 100000 | Series resistor in the voltage divider |

A full copy of `app_config_t` is stored in `s_cfg` at init time and used for the lifetime of the task.

### Module State

| Variable | Type | Description |
|---|---|---|
| `s_chip[2]` | `ads1115_handle_t` | ADS1115 handles: `[0]` at 0x48 (reserved), `[1]` at 0x49 (active) |
| `s_adc_queue` | `QueueHandle_t` | Queue (depth 8) signalled by ALERT GPIO ISR when a conversion completes |

### Fault Types Emitted

| Fault | Trigger |
|---|---|
| `SEQ_FAULT_HIGH_SWR` | SWR exceeds threshold while forward power >= 0.1 W |
| `SEQ_FAULT_OVER_TEMP1` | Thermistor 1 reading exceeds temperature threshold |
| `SEQ_FAULT_OVER_TEMP2` | Thermistor 2 reading exceeds temperature threshold |

## Event Flow

The monitor runs as a free-running FreeRTOS task. Its pace is governed by ADC conversion time (~125 ms per channel at 8 SPS).

### Initialization

`monitor_init` creates the I2C bus, initializes both ADS1115 chips, and registers a falling-edge GPIO ISR on chip 1's ALERT/RDY pin that posts to `s_adc_queue`. Chip 0 (0x48) has its ALERT pin configured as input with pullup but no ISR -- it is reserved for future use. No conversions are started at init.

### Sequential Single-Shot Read Loop

The task reads all four channels on chip 1 sequentially using the `read_channel()` helper. Each channel is read via a single-shot conversion: trigger, wait for ALERT, read result.

```
 monitor_task loop:
 |
 |-- 1. read_channel(AIN0)            -- trigger single-shot, wait ALERT, read
 |      fwd_v = voltage                  (~125 ms)
 |
 |-- 2. read_channel(AIN1)            -- trigger single-shot, wait ALERT, read
 |      ref_v = voltage                  (~125 ms)
 |
 |-- 3. Compute fwd_w, ref_w, SWR from fwd_v and ref_v
 |      Update spinlock-protected readings + system_state
 |      Check SWR fault threshold
 |
 |-- 4. read_channel(AIN2)            -- trigger single-shot, wait ALERT, read
 |      temp1 = voltage_to_temp_c()      (~125 ms)
 |      Update readings + system_state
 |      Check temp1 fault threshold
 |
 |-- 5. read_channel(AIN3)            -- trigger single-shot, wait ALERT, read
 |      temp2 = voltage_to_temp_c()      (~125 ms)
 |      Update readings + system_state
 |      Check temp2 fault threshold
 |
 '-- Loop back to step 1 (~500 ms full cycle)
```

### The `read_channel` Helper

This function encapsulates the single-shot trigger/wait/read sequence for chip 1:

1. **Drain** any stale ALERT events from the queue with non-blocking receives.
2. **Trigger** a single-shot conversion via `ads1115_start_single_shot(s_chip[1], ch)`.
3. **Wait** for the ALERT/RDY event on `s_adc_queue` (250 ms timeout).
4. **Read** the conversion result via `ads1115_read_raw` and convert to volts with `fmaxf(..., 0.0f)`.

Returns `-1.0f` on timeout or I2C error, which the caller uses to skip that channel's processing.

### Fault Delivery

Faults are sent as `seq_event_t` messages to the sequencer's event queue via `xQueueSend` with zero timeout (non-blocking). If the queue is full, the send is silently dropped -- the rationale is that the sequencer already has a fault pending. The sequencer is responsible for latching the fault state; the monitor does not debounce or suppress repeated faults itself.

## Architecture Decisions

- **Single-shot mode with sequential reads.** Each channel is read via an explicit trigger/wait/read cycle. This eliminates the complexity of continuous-mode channel switching (stale semaphore drains, mid-conversion aborts) and is well suited to the 8 SPS data rate where the ~500 ms full cycle time is acceptable.

- **Queue instead of binary semaphores for ALERT.** A single queue (depth 8) replaces per-chip binary semaphores. The queue can be drained of stale events before triggering a new conversion, and the chip index carried in the queue item allows future expansion to chip 0.

- **Chip 0 reserved.** The ADS1115 at 0x48 is initialized and its ALERT GPIO configured, but no ISR is installed and no conversions are performed. This keeps the hardware ready for future use without adding dead code paths to the read loop.

- **`-1.0f` error sentinel.** `read_channel` returns `-1.0f` on failure rather than NAN. The caller checks `>= 0.0f` (for power) or `> 0.0f` (for temperature) to skip failed readings, which is simpler than `isnan()` checks.

- **Incremental system_state updates.** Sensor readings are published to `system_state` as soon as each group is computed (power pair, then each temperature), rather than batching all four at the end of the cycle. This gives consumers fresher partial data during the ~500 ms cycle.

- **Calibration via V-squared model.** Forward and reflected power are computed as `cal_factor * V^2`. This matches the typical output characteristic of directional coupler detector diodes, where detected voltage is proportional to the square root of power.

- **SWR gating below minimum power.** SWR is only checked when forward power exceeds 0.1 W (`MIN_FWD_POWER_FOR_SWR_W`). At low or zero power, the reflected voltage is dominated by noise, which would produce wildly inaccurate SWR values and false faults.

- **SWR capped at 99.9.** When the reflection coefficient (gamma) approaches or exceeds 1.0 (open or short circuit), the SWR formula diverges. The cap prevents infinity or negative values from propagating.

- **Configuration snapshot at init.** The entire `app_config_t` is copied into a module-static variable during `monitor_init`. This avoids pointer-lifetime issues and means the monitor's thresholds and calibration values are fixed for the life of the task.

## Dependencies

| Dependency | Role |
|---|---|
| `ads1115` | Driver for ADS1115 16-bit ADC: init, single-shot trigger, read raw, convert to volts |
| `config` | Provides `app_config_t` with calibration factors and fault thresholds |
| `sequencer` | Provides `sequencer_get_event_queue()` for fault injection and defines `seq_event_t` / `seq_fault_t` |
| `system_state` | Blackboard for publishing live sensor readings to system-wide consumers |
| `hw_config` | Pin assignments and I2C bus parameters (`HW_I2C_*`, `HW_ADS1115_*`) |
| `driver` (ESP-IDF) | `gpio` for ALERT interrupt setup, `i2c_master` for bus initialization |
| `freertos` | Task infrastructure, queue, spinlock primitives |

## Usage Notes

- **Init order matters.** Call `monitor_init()` after `sequencer_init()` because the monitor needs the sequencer's event queue handle to exist before it can inject faults.

- **Task creation is the caller's responsibility.** The component exposes `monitor_task` as a FreeRTOS task function. The application main must call `xTaskCreate(monitor_task, "monitor", 4096, NULL, 7, NULL)` or equivalent.

- **No built-in fault suppression.** The monitor will send a fault event on every cycle where a threshold is breached. The sequencer is expected to latch the fault and ignore duplicates. If you change the sequencer's fault handling, be aware that the monitor may flood the queue under sustained fault conditions.

- **ADC timeout is 250 ms.** If the ADS1115 becomes unresponsive (e.g. I2C bus lockup), `read_channel` will log a warning and return `-1.0f` rather than blocking indefinitely. Four consecutive timeouts would stall the loop for ~1 second before it resumes.

- **I2C bus ownership.** The monitor creates and owns the I2C master bus. No other component should initialize the same I2C port. Other I2C devices on the same physical bus would need to be added as devices on `s_bus`, which currently requires modifying this component.
