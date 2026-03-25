# hw_config

## Overview

`hw_config` is a header-only ESP-IDF component that provides a single source of truth for all hardware pin assignments, peripheral addresses, and board-level constants for the ESP32-S3 23cm RF PA Sequencer. It contains no runtime code -- only `#define` macros consumed at compile time by nearly every other component in the system. Its purpose is to decouple physical board layout decisions from application logic so that a board revision requires changes in exactly one file.

## Component Structure

```
hw_config/
  CMakeLists.txt            # Registers include/ as a public header directory (no source files)
  include/
    hw_config.h             # All hardware definitions
```

Because there are no `.c` files, the `CMakeLists.txt` registers only `INCLUDE_DIRS`. The component produces no object code; it exists purely to make `hw_config.h` available on the include path for any component that declares a `REQUIRES hw_config` dependency.

## Key Data Structures

`hw_config.h` defines compile-time constants organized into five peripheral groups:

### PTT Input

| Macro | Value | Notes |
|---|---|---|
| `HW_PTT_GPIO` | 13 | Active-low, internal pull-up enabled by the `ptt` component |

### Relay Outputs

| Macro | Value | Notes |
|---|---|---|
| `HW_RELAY_COUNT` | 6 | Total relay channels on the board |
| `HW_RELAY1_GPIO` | 39 | RX/TX Path Select |
| `HW_RELAY2_GPIO` | 40 | PA On/Off |
| `HW_RELAY3_GPIO` | 41 | LNA Isolate |
| `HW_RELAY4_GPIO` | 42 | Spare |
| `HW_RELAY5_GPIO` | 11 | Spare |
| `HW_RELAY6_GPIO` | 12 | Spare |
| `HW_RELAY_GPIOS` | `{ 39, 40, 41, 42, 11, 12 }` | Initializer-list macro, index 0 = Relay 1 |

Relay IDs throughout the system are **1-indexed** to match schematic labels. The `HW_RELAY_GPIOS` array macro is 0-indexed, so consumers access it as `gpios[relay_id - 1]`.

### Button Inputs

| Macro | Value | Notes |
|---|---|---|
| `HW_BUTTON_COUNT` | 6 | Total button channels |
| `HW_BTN1_GPIO` | 4 | Emergency PA Off |
| `HW_BTN2_GPIO` -- `HW_BTN6_GPIO` | 5, 6, 7, 48, 47 | Spare |
| `HW_BUTTON_GPIOS` | `{ 4, 5, 6, 7, 48, 47 }` | Initializer-list macro, index 0 = Button 1 |

Buttons are active-low with internal pull-ups enabled by the `buttons` component.

### I2C Bus / ADS1115 ADCs

| Macro | Value | Notes |
|---|---|---|
| `HW_I2C_PORT` | `I2C_NUM_0` | ESP-IDF I2C peripheral instance |
| `HW_I2C_SDA_GPIO` | 1 | I2C data line |
| `HW_I2C_SCL_GPIO` | 2 | I2C clock line |
| `HW_I2C_FREQ_HZ` | 400000 | 400 kHz (Fast Mode) |
| `HW_ADS1115_0_ADDR` | `0x48` | Reserved for future use |
| `HW_ADS1115_1_ADDR` | `0x49` | Active: AIN0=fwd power, AIN1=ref power, AIN2=temp sensor 2, AIN3=temp sensor 1 |
| `HW_ADS1115_0_ALRT_GPIO` | 16 | ALERT/DRDY interrupt pin for ADC 0 |
| `HW_ADS1115_1_ALRT_GPIO` | 15 | ALERT/DRDY interrupt pin for ADC 1 |

### UART / Nextion Display

| Macro | Value | Notes |
|---|---|---|
| `HW_NEXTION_UART_PORT` | `UART_NUM_1` | ESP-IDF UART peripheral instance |
| `HW_NEXTION_TX_GPIO` | 17 | TX to display |
| `HW_NEXTION_RX_GPIO` | 18 | RX from display |
| `HW_NEXTION_BAUD_RATE` | 9600 | Nextion default baud rate |

## Dependency Graph

`hw_config` sits at the bottom of the component dependency tree. It has no dependencies on other project components and depends only on two ESP-IDF HAL type headers (`hal/i2c_types.h`, `hal/uart_types.h`) for the `I2C_NUM_0` and `UART_NUM_1` enum values.

The following components declare a direct `REQUIRES hw_config` dependency:

- **system_state** -- uses `HW_RELAY_COUNT` to size the relay bitmask
- **config** -- uses `HW_RELAY_COUNT` to size the `relay_names` array in `app_config_t`
- **relays** -- uses `HW_RELAY_COUNT` and `HW_RELAY_GPIOS` to initialize GPIO outputs
- **ptt** -- uses `HW_PTT_GPIO` to configure the interrupt-driven PTT input
- **buttons** -- uses `HW_BUTTON_COUNT` and `HW_BUTTON_GPIOS` to initialize GPIO inputs
- **ads1115** -- uses `HW_I2C_*` and `HW_ADS1115_*` for I2C bus and device addressing
- **monitor** -- uses I2C, ADS1115, and ALERT GPIO definitions for sensor reading
- **main (src/)** -- top-level application wiring

Additionally, `cli` and `web_server` include the header for relay/button count constants used in iteration bounds and input validation, though they take `hw_config` transitively through other dependencies.

## Architecture Decisions

- **Header-only, no runtime code.** All values are `#define` constants resolved at compile time. There is no initialization function, no state, and no RAM cost. This is deliberate: hardware pin assignments are fixed at board fabrication and never change at runtime.

- **Initializer-list macros (`HW_RELAY_GPIOS`, `HW_BUTTON_GPIOS`).** These expand to brace-enclosed initializer lists, allowing consumers to declare `static const` arrays in a single line (e.g., `static const int gpios[] = HW_RELAY_GPIOS;`). The macros cannot be used in expressions -- only in array/struct initializations.

- **1-indexed relay IDs, 0-indexed arrays.** Relay and button IDs visible to the user (CLI, web API, config) are 1-indexed to match schematic reference designators. The GPIO arrays are 0-indexed per C convention. Every consumer handles the `id - 1` offset individually, which is a conscious trade-off favoring schematic-aligned user-facing identifiers over code convenience.

- **Two ADS1115 addresses defined, one reserved.** `HW_ADS1115_0_ADDR` (0x48) is declared but marked "reserved for future use," establishing the address allocation now so a second ADC can be added without changing existing definitions. Only `HW_ADS1115_1_ADDR` (0x49) is actively wired.

- **`extern "C"` guards.** The header includes C++ linkage guards despite the project being pure C. This is forward-looking -- it allows the header to be included from C++ translation units (e.g., if a C++ display driver or test harness is added later) without modification.

## Usage Notes

- **Board revision changes belong here.** If a GPIO assignment changes on a new PCB revision, update the relevant `#define` in `hw_config.h`. No other file should contain hard-coded pin numbers.

- **Adding a new peripheral.** Follow the established pattern: add a comment block with a section header, define the GPIO(s) and any bus parameters, and document the signal polarity or protocol details in inline comments.

- **The GPIOS macros are not expressions.** `HW_RELAY_GPIOS` expands to `{ 39, 40, ... }` which is valid only in an initializer context. You cannot pass it as a function argument or use it in an `if` statement. Assign it to a `static const` array first.

- **Count macros are used for bounds checking everywhere.** `HW_RELAY_COUNT` and `HW_BUTTON_COUNT` appear in range checks across the CLI, web API, config, relays, and system_state components. If the relay or button count changes, the count macro is the only value that needs updating -- all downstream bounds checks adapt automatically.
