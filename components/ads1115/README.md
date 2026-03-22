# ads1115

## Overview

ESP-IDF component providing a driver for the ADS1115 16-bit delta-sigma ADC over I2C. It wraps the ESP-IDF v5.x `i2c_master` API and operates in **single-shot conversion mode** with the ALERT/RDY pin configured as a conversion-ready signal. In this project, two ADS1115 chips are used by the `monitor` component: one reserved for future use (0x48) and one active (0x49) for forward/reflected RF power and NTC thermistor temperature sensing.

## Key Data Structures

### Opaque Handle

```c
typedef struct ads1115_dev *ads1115_handle_t;
```

All API functions take this handle. Internally it holds:

| Field      | Type                       | Purpose                                    |
|------------|----------------------------|--------------------------------------------|
| `i2c_dev`  | `i2c_master_dev_handle_t`  | ESP-IDF I2C device handle bound to the bus |
| `pga`      | `ads1115_pga_t`            | Stored PGA setting, used for voltage conversion |

### Enumerations

**`ads1115_channel_t`** -- Selects one of four single-ended inputs (AINx vs GND). Maps directly to the MUX field in the ADS1115 config register (`0x4 + channel`).

**`ads1115_pga_t`** -- Programmable Gain Amplifier setting. Determines the full-scale input range and therefore the voltage resolution per LSB:

| Enum Value          | Full-Scale Range | Resolution  |
|---------------------|------------------|-------------|
| `ADS1115_PGA_6144`  | +/-6.144 V       | 187.5 uV    |
| `ADS1115_PGA_4096`  | +/-4.096 V       | 125.0 uV    |
| `ADS1115_PGA_2048`  | +/-2.048 V       | 62.5 uV     |
| `ADS1115_PGA_1024`  | +/-1.024 V       | 31.3 uV     |
| `ADS1115_PGA_0512`  | +/-0.512 V       | 15.6 uV     |
| `ADS1115_PGA_0256`  | +/-0.256 V       | 7.8 uV      |

### Config Register Construction

The config word written by `ads1115_start_single_shot()` is assembled from bitfield macros:

```
OS_START | MUX_AIN(ch) | PGA(gain) | MODE_SINGLE | DR_8SPS | COMP_QUE_1
```

`OS_START` (bit 15) triggers a single conversion. `MODE_SINGLE` (bit 8 = 1) puts the chip into single-shot/power-down mode. The data rate is fixed at 8 SPS (~125 ms per conversion). The comparator queue is set to assert ALERT after every conversion. After the conversion completes, the chip returns to power-down.

## Event Flow

The driver uses a **trigger-and-read** pattern. The caller triggers a single-shot conversion, waits for ALERT/RDY to fire, then reads the result. The driver does not own any task or ISR; the caller is responsible for synchronisation.

```
Caller                      ADS1115 Driver                  Hardware
  |                              |                              |
  |-- start_single_shot(ch) --> |-- write config reg --------> |
  |                              |       (OS=1 triggers conversion)
  |                              |                              |
  |   (caller waits on GPIO     |                     conversion runs
  |    semaphore/queue)          |                     (~125 ms @ 8 SPS)
  |                              |                              |
  |                              |              ALERT/RDY pin fires (active-low)
  |   <-- GPIO ISR signals ---   |                              |
  |                              |                     chip enters power-down
  |                              |                              |
  |-- read_raw() -------------> |-- read conversion reg ------> |
  |  <-- int16_t raw ---------- |                              |
  |                              |                              |
  |-- raw_to_voltage(raw) ----> |-- computes float volts        |
  |  <-- float volts ---------- |                              |
```

### Channel Switching

Each call to `ads1115_start_single_shot()` specifies the channel, so switching channels is simply a matter of passing a different `ads1115_channel_t` value. Since each conversion is discrete (the chip powers down between conversions), there are no stale-data or mid-conversion abort concerns.

**Concrete usage in this project** (from the `monitor` component):

1. `monitor_init()` creates the I2C bus, initialises two ADS1115 handles (addresses 0x48 and 0x49), and installs a falling-edge GPIO ISR on chip 1's ALERT pin that posts to a queue. No conversions are started at init.
2. `monitor_task()` loops through all four channels sequentially using `read_channel()`, which triggers a single-shot conversion, waits for ALERT/RDY (250 ms timeout), and reads the result.
3. Results feed into power calibration (V-squared scaling) and Steinhart-Hart thermistor calculations.

## Architecture Decisions

- **Single-shot conversion mode.** Each conversion is explicitly triggered by setting the OS bit. The chip powers down between conversions, eliminating stale-data concerns when switching channels. The trade-off is one extra I2C write per conversion (to set OS), but at 8 SPS the bus overhead is negligible.

- **ALERT/RDY as conversion-ready, not a comparator.** During `ads1115_init()`, the threshold registers are written to special sentinel values (Hi_thresh = 0x8000, Lo_thresh = 0x0000) per datasheet section 9.3.8. This repurposes the ALERT pin to signal conversion completion rather than threshold crossing. The driver sets `COMP_QUE_1` so the pin asserts after every conversion.

- **Caller owns synchronisation.** The driver deliberately does not create tasks, semaphores, or ISRs. This keeps it a pure I2C peripheral driver with no RTOS coupling, making it testable and reusable. The `monitor` component wires up the ISR-to-queue pattern externally.

- **PGA is set once at init, not per-conversion.** The PGA gain is stored in the handle and applied to every subsequent `ads1115_start_single_shot()` call. This simplifies the API for the common case where all channels on a chip share the same voltage range.

- **8 SPS data rate.** The data rate is hardcoded at 8 SPS (~125 ms per conversion). The slower rate provides longer integration time and better noise rejection, which is well suited for the low-bandwidth RF power and thermistor measurements in this application.

- **I2C bus clock at 400 kHz (Fast Mode).** The device config specifies 400 kHz SCL, which is the maximum the ADS1115 supports.

## Dependencies

| Dependency   | Role                                                         |
|--------------|--------------------------------------------------------------|
| `driver`     | ESP-IDF I2C master driver (`i2c_master.h`)                   |
| `hw_config`  | Board-level pin and address definitions (`HW_ADS1115_*`)     |
| `esp_log`    | Logging via `ESP_LOGI` / `ESP_LOGE`                          |
| `stdlib.h`   | `calloc` / `free` for handle allocation                      |

The `hw_config` dependency is declared in `CMakeLists.txt` but is not directly included by the driver source -- it flows through the caller. The driver itself only depends on the I2C handle and address passed to `ads1115_init()`.

## Usage Notes

- **Always wait for ALERT/RDY before reading.** Calling `ads1115_read_raw()` without waiting for the conversion-ready signal will return a stale result from a previous conversion.

- **Each conversion must be explicitly triggered.** Unlike continuous mode, the chip does not auto-start the next conversion. Call `ads1115_start_single_shot()` for each reading.

- **Drain stale ALERT tokens before triggering.** If there is any chance a previous ALERT fired without being consumed, drain the semaphore/queue with a non-blocking take before starting a new conversion.

- **125 ms conversion time at 8 SPS.** Set ALERT/RDY wait timeouts accordingly (the monitor uses 250 ms to allow margin).

- **Voltage conversion assumes signed 16-bit range.** The formula `raw * FSR / 32767.0` means negative raw values (which can occur if the input goes below GND) produce negative voltages. The `monitor` component clamps to zero with `fmaxf()`.

- **One channel at a time per chip.** Only one MUX channel is active per conversion. Multiplexing across channels requires sequential trigger/wait/read cycles.

- **Thread safety.** The driver itself has no internal locking. If multiple tasks could call into the same handle concurrently, external synchronisation is required. In this project, each chip is accessed only from the single `monitor_task`.

- **Cleanup.** `ads1115_deinit()` removes the device from the I2C bus and frees the handle. It is NULL-safe.
