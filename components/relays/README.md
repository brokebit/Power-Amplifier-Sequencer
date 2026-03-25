# Relays

## Overview

The `relays` component is a thin GPIO driver that controls six physical relay outputs on the ESP32-S3 sequencer board. It translates 1-indexed relay IDs (matching the hardware schematic labels R1--R6) into GPIO level changes and publishes every state change to the `system_state` blackboard so that consumers (display, CLI, web UI) always have a consistent view of relay positions.

The component itself contains no sequencing logic, timing, or safety policy. It is a pure actuator: callers tell it which relay to energise or release, and it does so immediately. All TX/RX sequencing, fault handling, and interlock logic live in the `sequencer` component, which is the primary consumer of this API.

## Key Data Structures

### Public API

| Function | Signature | Description |
|---|---|---|
| `relays_init` | `esp_err_t relays_init(void)` | Configures all six relay GPIOs as push-pull outputs (no pull-up/down) and drives them LOW. Must be called once at boot before any other relay function. |
| `relay_set` | `esp_err_t relay_set(uint8_t relay_id, bool on)` | Sets a single relay. `relay_id` is 1-indexed (1--6). Returns `ESP_ERR_INVALID_ARG` for out-of-range IDs. |
| `relays_all_off` | `void relays_all_off(void)` | Immediately de-energises all six relays. Used as the safe-state fallback during fault recovery. |

### Internal State

The component holds a single static array mapping relay index to GPIO number:

```c
static const int s_relay_gpios[HW_RELAY_COUNT] = HW_RELAY_GPIOS;
```

This is populated from `hw_config.h` at compile time. There is no dynamic configuration or runtime remapping of pins.

### GPIO Pin Assignments (from `hw_config.h`)

| Relay ID | GPIO | Schematic Function |
|---|---|---|
| 1 | 39 | RX/TX Path Select |
| 2 | 40 | PA On/Off |
| 3 | 41 | LNA Isolate |
| 4 | 42 | Spare |
| 5 | 11 | Spare |
| 6 | 12 | Spare |

All relay outputs are active-high: GPIO HIGH = relay energised, GPIO LOW = relay released.

## Event Flow

The relays component does not generate or consume events. It is a synchronous, call-and-return driver. The flow through the system is:

1. **Boot** -- `app_main()` calls `relays_init()` early, before the sequencer or PTT components are started. All relays begin in the OFF state.

2. **Normal TX/RX sequencing** -- The `sequencer` component calls `relay_set()` step-by-step as it walks through the configured TX or RX relay sequences (`seq_step_t` arrays from `app_config_t`). Each step specifies a relay ID, target state, and inter-step delay.

3. **Emergency shutdown** -- On fault detection, the sequencer calls `relay_set(pa_relay_id, false)` to kill the PA relay immediately, then runs the RX sequence to restore all relays to a safe state. `relays_all_off()` is also called during fault-clear to guarantee a clean baseline.

4. **Manual override** -- The CLI (`relay <N> on|off`) and the web API (`POST /api/relay`) can call `relay_set()` directly, bypassing the sequencer. The CLI prints a warning when this happens.

5. **State publication** -- Every `relay_set()` call updates the `system_state` blackboard via `system_state_set_relay()`, which sets/clears the corresponding bit in `system_state_t.relay_states` (bit 0 = relay 1). `relays_all_off()` zeroes the entire bitmask in one spinlock-protected write via `system_state_set_relays_all_off()`.

```
  PTT ISR ──> sequencer_task ──> relay_set(id, on/off)
                                    ├── gpio_set_level()
                                    └── system_state_set_relay()
                                            └── blackboard (spinlock)
                                                    └── display / CLI / web read snapshot
```

## Architecture Decisions

- **1-indexed relay IDs.** The public API uses 1-based numbering (`relay_id` 1--6) to match the schematic labels (R1--R6). Internally the component subtracts 1 to index the GPIO array. This eliminates off-by-one confusion when cross-referencing code against hardware documentation.

- **No internal state tracking beyond GPIO levels.** The component does not maintain its own `bool relay_on[6]` shadow array. Instead it delegates observable state to the `system_state` blackboard, which is the single source of truth for the rest of the system. This avoids dual bookkeeping.

- **Stateless safe-state design.** `relays_all_off()` iterates all GPIOs unconditionally rather than checking current state. This makes it reliable as a panic-path function -- it will always reach the correct output level regardless of any inconsistency between software state and hardware.

- **No mutex or ISR safety.** The GPIO writes themselves are atomic register operations on the ESP32-S3. The component does not add any FreeRTOS synchronisation beyond what `system_state` already provides. This is safe because the sequencer task is the only caller during normal operation, and the CLI/web override paths are understood to be advisory/debug tools.

- **Batch GPIO configuration.** `relays_init()` builds a single `pin_bit_mask` across all six GPIOs and calls `gpio_config()` once, rather than configuring each pin individually. This is both more efficient and ensures all pins get identical electrical configuration.

## Dependencies

| Dependency | Role |
|---|---|
| `driver` (ESP-IDF) | `gpio_config()`, `gpio_set_level()` -- hardware GPIO access |
| `hw_config` | `HW_RELAY_COUNT`, `HW_RELAY_GPIOS` -- compile-time pin assignments |
| `system_state` | `system_state_set_relay()`, `system_state_set_relays_all_off()` -- blackboard publication |

## Usage Notes

- **Always call `relays_init()` before `sequencer_init()`.** The sequencer assumes relay GPIOs are already configured as outputs. The boot order in `app_main()` enforces this, but it matters if the initialization sequence is ever refactored.

- **Direct `relay_set()` calls bypass sequencer safety logic.** The CLI and web API can toggle relays independently, which is useful for hardware debugging but dangerous during live operation. The CLI warns about this; the web API does not.

- **The `pa_relay_id` is configurable.** The sequencer's emergency shutdown path calls `relay_set(s_cfg.pa_relay_id, false)` to kill the PA relay first. Which relay ID this is comes from `app_config_t`, not from a hardcoded constant. The relays component itself has no concept of which relay is "the PA relay."

- **Adding a 7th relay** would require updating `HW_RELAY_COUNT` and `HW_RELAY_GPIOS` in `hw_config.h`, adding a GPIO define, and updating the `system_state_t.relay_states` field if it needs more than 8 bits (currently `uint8_t`, supporting up to 8 relays).
