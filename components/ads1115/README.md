# ads1115

## Overview

Low-level I2C driver for the Texas Instruments ADS1115 16-bit analog-to-digital converter, built on the ESP-IDF v5.x `i2c_master` API. Each driver instance (handle) wraps a single ADS1115 chip on a shared I2C bus and provides a simple three-step workflow: initialise, trigger a single-shot conversion, read the result. The driver configures the chip's ALERT/RDY pin as a hardware conversion-ready signal so that callers can synchronise reads to actual conversion completion rather than polling or guessing with delays.

In this project, the `monitor` component is the primary consumer. It uses `ads1115` to sample four analog channels on chip 1 (address 0x49) in a continuous loop: forward RF power, reflected RF power, and two PA temperature sensors. The `cli` and `web_server` components also read channels on demand through the `monitor` component's public API, which serialises access with a FreeRTOS mutex.

## Key Data Structures

### Opaque handle

```c
typedef struct ads1115_dev *ads1115_handle_t;
```

All public functions operate on this opaque pointer. Internally it holds two fields:

| Field      | Type                      | Description                                          |
|------------|---------------------------|------------------------------------------------------|
| `i2c_dev`  | `i2c_master_dev_handle_t` | ESP-IDF I2C device handle bound to a specific bus address |
| `pga`      | `ads1115_pga_t`           | Programmable gain setting, fixed at init time         |

### Channel selection

```c
typedef enum {
    ADS1115_CHANNEL_0 = 0,  // AIN0 vs GND
    ADS1115_CHANNEL_1,      // AIN1 vs GND
    ADS1115_CHANNEL_2,      // AIN2 vs GND
    ADS1115_CHANNEL_3       // AIN3 vs GND
} ads1115_channel_t;
```

All four channels are single-ended (measured against GND). The hardware supports differential configurations, but this driver only exposes single-ended mode since the sequencer's sensors are all ground-referenced.

### PGA (gain) selection

```c
typedef enum {
    ADS1115_PGA_6144 = 0,  // +/-6.144 V  187.5 uV/LSB
    ADS1115_PGA_4096,      // +/-4.096 V  125.0 uV/LSB
    ADS1115_PGA_2048,      // +/-2.048 V   62.5 uV/LSB
    ADS1115_PGA_1024,      // +/-1.024 V   31.3 uV/LSB
    ADS1115_PGA_0512,      // +/-0.512 V   15.6 uV/LSB
    ADS1115_PGA_0256       // +/-0.256 V    7.8 uV/LSB
} ads1115_pga_t;
```

The PGA setting is locked at init time and applied to every subsequent conversion. Both chips in this project use `ADS1115_PGA_4096` (full-scale +/-4.096 V, 125 uV/LSB), which comfortably covers the 0-3.3 V range of the ESP32-S3's analog inputs.

## Public API

| Function                    | Purpose                                                                     |
|-----------------------------|-----------------------------------------------------------------------------|
| `ads1115_init()`            | Attach to an I2C bus at the given address, configure ALERT/RDY, return a handle |
| `ads1115_start_single_shot()` | Write the config register to trigger one conversion on a chosen channel    |
| `ads1115_read_raw()`        | Read the 16-bit signed conversion register (call after ALERT/RDY fires)    |
| `ads1115_raw_to_voltage()`  | Convert a raw ADC count to volts using the handle's PGA full-scale range   |
| `ads1115_deinit()`          | Remove the device from the I2C bus and free the handle                      |

## Event Flow

The driver itself is stateless between calls. The conversion lifecycle is driven externally by the caller (the `monitor` component):

```
1. Caller calls ads1115_start_single_shot(handle, channel)
       |
       v
2. Driver writes config register over I2C:
       - Sets OS bit (start conversion)
       - Sets MUX to selected single-ended channel
       - Sets PGA, single-shot mode, 8 SPS data rate
       - Sets COMP_QUE = 1 (assert ALERT after 1 conversion)
       |
       v
3. ADS1115 hardware performs conversion (~125 ms at 8 SPS)
       |
       v
4. ADS1115 asserts ALERT/RDY pin (active-low edge)
       |
       v
5. GPIO ISR (owned by monitor, NOT by this driver) fires,
   posts chip index to a FreeRTOS queue
       |
       v
6. Caller receives queue event, calls ads1115_read_raw(handle, &raw)
       |
       v
7. Driver reads 16-bit conversion register over I2C
       |
       v
8. Caller converts raw to volts: ads1115_raw_to_voltage(handle, raw)
```

The critical design point is that the ALERT/RDY interrupt is not owned by this driver. The driver only configures the ADS1115's threshold registers so the chip will assert its ALERT pin on conversion completion (Hi_thresh MSB=1, Lo_thresh MSB=0 per datasheet section 9.3.8). The GPIO ISR setup, queue management, and synchronisation are all the caller's responsibility.

### Hardware configuration (from hw_config)

| Constant               | Value | Description                                        |
|-------------------------|-------|----------------------------------------------------|
| `HW_ADS1115_0_ADDR`   | 0x48  | Chip 0 I2C address (general-purpose channels)      |
| `HW_ADS1115_1_ADDR`   | 0x49  | Chip 1 I2C address (fwd, ref, temp1, temp2)        |
| `HW_ADS1115_0_ALRT_GPIO` | 16 | ALERT/RDY pin for chip 0 (falling-edge ISR)        |
| `HW_ADS1115_1_ALRT_GPIO` | 15 | ALERT/RDY pin for chip 1 (falling-edge ISR)        |
| `HW_I2C_SDA_GPIO`     | 1     | Shared I2C bus SDA                                 |
| `HW_I2C_SCL_GPIO`     | 2     | Shared I2C bus SCL                                 |

## Architecture Decisions

- **Single-shot mode at 8 SPS.** The driver always configures single-shot conversions at the slowest data rate (8 samples per second, ~125 ms per conversion). This maximises noise rejection (the ADS1115's digital filter averages more samples at lower rates) and minimises power consumption between conversions. The 125 ms per-channel latency is acceptable because the monitor task cycles all four channels sequentially, yielding a full sensor sweep roughly every 500 ms -- adequate for thermal and SWR fault detection.

- **ALERT/RDY as conversion-ready, not a comparator.** The ADS1115's ALERT pin can operate either as an analog comparator output or as a simple "conversion done" flag. This driver configures it as conversion-ready by writing specific threshold register values (Hi_thresh = 0x8000, Lo_thresh = 0x0000). This is a hardware handshake that eliminates the need for polling the config register's OS bit over I2C.

- **Separation of driver and interrupt ownership.** The driver does not install any GPIO ISR. The `monitor` component owns the ISR, the FreeRTOS queue, and the mutex that serialises ADC access between the monitor task and CLI/web requests. This keeps the driver reusable and avoids coupling it to a specific concurrency model.

- **Opaque handle pattern.** The internal `struct ads1115_dev` is only forward-declared in the header. Callers cannot access or modify internal state directly. This allows the struct layout to change without breaking consumers.

- **PGA fixed at init time.** The gain is set once during initialisation and reused for every conversion. This simplifies the API (no per-conversion gain parameter) and matches the use case where all channels on a given chip share the same voltage range.

- **I2C clock at 400 kHz (Fast Mode).** The device config sets `scl_speed_hz = 400000`, matching the bus frequency defined in `hw_config`. The ADS1115 supports up to 3.4 MHz, but 400 kHz is the standard fast-mode ceiling for the ESP32 I2C peripheral with external pull-ups.

## Dependencies

| Dependency   | Type              | Purpose                                        |
|--------------|-------------------|-------------------------------------------------|
| `driver`     | ESP-IDF component | Provides `i2c_master` API for bus communication |
| `hw_config`  | Project component | Supplies I2C addresses and GPIO pin assignments  |

The component has no dependency on FreeRTOS directly -- it performs blocking I2C transactions (100 ms timeout) but does not create tasks, queues, or semaphores.

## Thread Safety

The driver is **not thread-safe**. No internal locking exists. If multiple tasks need to access the same ADS1115 chip concurrently, the caller must provide external synchronisation. In this project, the `monitor` component uses `s_adc_mutex` (a FreeRTOS mutex) to serialise all ADC access between the monitor task and CLI/web-server requests.

## Usage Notes

- **Always wait for ALERT/RDY before reading.** Calling `ads1115_read_raw()` before the conversion completes will return the previous conversion's result (or 0x0000 on first read), silently producing stale data. The caller must set up a GPIO interrupt or poll the ALERT pin.

- **Both chips are actively sampled.** The `monitor` component creates handles for both chips, each with its own falling-edge ISR and FreeRTOS queue. Chip 1 (0x49) handles power and temperature sensing. Chip 0 (0x48) provides four general-purpose ADC channels with per-channel resistor divider correction.

- **Raw-to-voltage conversion assumes positive full-scale = 32767.** The formula `raw * FSR / 32767.0` maps the 16-bit signed range to +/- the PGA full-scale voltage. The monitor clamps negative values to zero with `fmaxf(..., 0.0f)` since all sensors produce positive voltages.

- **Input validation is minimal.** The current implementation does not validate `handle`, `channel`, or `pga` parameters for NULL or out-of-range values. The safety audit (`ads1115_safteyReport.md` in the project root) documents six findings related to this. Current callers pass valid arguments, but new consumers should be aware that invalid parameters will cause hard faults rather than returning error codes.
