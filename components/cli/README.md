# CLI

## Overview

The CLI component provides an interactive serial console (REPL) for the RF PA sequencer, built on ESP-IDF's `esp_console` framework over UART0. It registers nine command groups that cover the full operational surface of the system: live status inspection, relay and sequence control, configuration management, fault injection/clearance, ADC diagnostics, log-level tuning, and continuous monitoring. All commands are available immediately after boot at the `seq> ` prompt.

The CLI operates on a shared `app_config_t` pointer owned by `main`. Changes made through `config set` or `seq tx/rx set` modify the live config struct in memory; persistence to NVS and activation in the sequencer/monitor are explicit, separate steps.

## Key Data Structures

### Shared Config Pointer

The CLI stores a single module-static `app_config_t *s_cfg`, set once by `cli_init()` and exposed to all command handlers via `cli_get_config()`. This is the same struct owned by `main` -- command handlers read and write it directly.

### Config Key Table (`cmd_config.c`)

A data-driven table maps human-readable key names to `app_config_t` field offsets with validation ranges:

| CLI Key | Config Field | Min | Max |
|---|---|---|---|
| `swr_threshold` | `swr_fault_threshold` | 1.0 | 99.0 |
| `temp_threshold` | `temp_fault_threshold_c` | 0.0 | 200.0 |
| `fwd_cal` | `fwd_power_cal_factor` | 0.001 | 1000.0 |
| `ref_cal` | `ref_power_cal_factor` | 0.001 | 1000.0 |
| `therm_beta` | `thermistor_beta` | 1.0 | 100000.0 |
| `therm_r0` | `thermistor_r0_ohms` | 1.0 | 10000000.0 |
| `therm_rseries` | `thermistor_r_series_ohms` | 1.0 | 10000000.0 |

Field writes use `offsetof`-based pointer arithmetic, so adding a new float config parameter requires only a new table entry.

### Sequence Step Token Format (`cmd_seq.c`)

Steps are parsed from the format `R<n>:<on|off>:<delay_ms>` with validation:
- Relay ID: 1--6
- State: `on` or `off`
- Delay: 0--10000 ms
- Maximum steps per sequence: `SEQ_MAX_STEPS` (8)

## Command Reference

| Command | Source File | Description |
|---|---|---|
| `status` | `cmd_status.c` | One-shot display of PTT, state, fault, relays, power, SWR, temperatures |
| `version` | `cmd_system.c` | Firmware name, version, build date, IDF version, chip info |
| `reboot` | `cmd_system.c` | Restart the ESP32 (100 ms delay before `esp_restart()`) |
| `log <level> [tag]` | `cmd_log.c` | Set ESP-IDF log level globally or per-tag (`on`/`off` aliases for `info`/`none`) |
| `config show` | `cmd_config.c` | Print all configuration values including sequences |
| `config set <key> <value>` | `cmd_config.c` | Modify a config field in memory (see key table above) |
| `config save` | `cmd_config.c` | Persist current config to NVS |
| `config defaults` | `cmd_config.c` | Reset config to factory defaults in memory (not persisted) |
| `relay show` | `cmd_relay.c` | Display current relay states |
| `relay <1-6> <on\|off>` | `cmd_relay.c` | Directly set a relay (bypasses sequencer -- prints warning) |
| `fault show` | `cmd_fault.c` | Show current sequencer state and fault code |
| `fault clear` | `cmd_fault.c` | Clear a latched fault, return to RX |
| `fault inject <type>` | `cmd_fault.c` | Inject a fault event (`swr`, `temp1`, `temp2`, `emergency`) into the sequencer queue |
| `seq tx show` / `seq rx show` | `cmd_seq.c` | Display the TX or RX relay sequence |
| `seq tx set ...` / `seq rx set ...` | `cmd_seq.c` | Define a new sequence from step tokens |
| `seq save` | `cmd_seq.c` | Persist sequences to NVS (delegates to `config_save`) |
| `seq apply` | `cmd_seq.c` | Push current config to the live sequencer and monitor |
| `adc scan` | `cmd_adc.c` | Read all four ADS1115 channels and print voltages |
| `adc read <0-3>` | `cmd_adc.c` | Read a single ADS1115 channel |
| `monitor [interval_ms]` | `cmd_monitor.c` | Continuous status output (default 1000 ms, range 100--60000); press Enter to stop |

## Event Flow

### Initialization Sequence

```
main
  |
  |-- cli_init(cfg)
  |     |
  |     |-- Store cfg pointer in s_cfg
  |     |-- Create UART0 REPL (prompt "seq> ", max line 256 chars)
  |     |-- Register all 9 command groups
  |     |-- esp_log_level_set("*", ESP_LOG_NONE)    -- suppress logs for clean prompt
  |     '-- esp_console_start_repl()                 -- spawns REPL task
  |
  '-- (REPL task runs independently, CLI is live)
```

### Config Edit / Apply / Save Flow

The CLI uses a three-phase workflow for configuration changes. This prevents accidental activation of untested settings and gives the operator a review step.

```
1. Edit in memory     config set swr_threshold 2.5
                      seq tx set R3:on:50 R1:on:50 R2:on:0

2. Review             config show / seq tx show

3a. Activate live     seq apply        -- pushes to sequencer + monitor
3b. Persist to NVS    config save      -- survives reboot
                      seq save         -- (equivalent, both call config_save)
```

Steps 3a and 3b are independent. You can apply without saving (test a setting, reboot to revert), or save without applying (prepare a config for next boot).

### Fault Injection Path

`fault inject` constructs a `seq_event_t` and posts it to the sequencer's event queue via `xQueueSend`. For emergency faults, the event type is `SEQ_EVENT_EMERGENCY_PA_OFF`; for all others, `SEQ_EVENT_FAULT` with the fault code in the `data` field.

### Monitor Loop

`monitor` runs a blocking loop on the REPL task. Each iteration reads a `system_state` snapshot, prints a compact one-line status, then polls UART0 in 50 ms increments for a keypress (Enter) to exit. This UART polling approach means the REPL is unresponsive to other commands while monitoring is active.

## Architecture Decisions

- **Shared mutable config pointer.** The CLI receives a raw pointer to `main`'s `app_config_t` and modifies it in-place. This avoids copy overhead and gives immediate feedback (`config show` reflects changes), but means the CLI and any other config consumer (sequencer, monitor) share the same memory. The explicit `seq apply` step is what makes this safe -- the sequencer copies the config atomically when `sequencer_update_config()` is called.

- **Logging suppressed at init.** `esp_log_level_set("*", ESP_LOG_NONE)` is called immediately after command registration. Without this, ESP-IDF log output would interleave with the REPL prompt, making the console unusable. The `log` command allows the operator to selectively re-enable logging per-tag or globally when debugging.

- **Data-driven config key table.** The `offsetof`-based key table in `cmd_config.c` avoids a long if/else chain for each configurable field. Adding a new `float` parameter to the CLI requires only a one-line table entry. The trade-off is that only `float` fields are supported by this mechanism; the sequence arrays require their own parsing logic.

- **One file per command group.** Each `cmd_*.c` file registers its own commands via a `register_cmd_*()` function. This keeps the main `cli.c` file minimal (just init and dispatch) and allows command groups to be added or removed by editing `CMakeLists.txt` and adding/removing a forward declaration and register call.

- **Direct relay control warns but does not block.** `relay <n> <on|off>` bypasses the sequencer intentionally -- it is a diagnostic escape hatch. The warning printed to the console is the only safeguard. This is useful for bench testing relay wiring but dangerous during live operation.

- **Monitor blocks the REPL.** The `monitor` command runs in the REPL task's context with a blocking loop. This is the simplest approach given `esp_console`'s single-threaded command dispatch model. The trade-off is that no other commands can be entered while monitoring. Pressing Enter exits cleanly.

- **Fault injection goes through the normal event path.** Rather than directly mutating sequencer state, `fault inject` posts an event to the sequencer's queue. This means the injected fault follows the exact same code path as a real fault from the monitor, making it a faithful test of the fault handling logic.

## Dependencies

| Dependency | Role |
|---|---|
| `console` (ESP-IDF) | `esp_console` REPL framework: command registration, UART REPL lifecycle |
| `config` | `app_config_t` type, `config_save()`, `config_defaults()` |
| `sequencer` | State/fault types, `sequencer_get_state()`, `sequencer_get_fault()`, `sequencer_clear_fault()`, `sequencer_get_event_queue()`, `sequencer_update_config()` |
| `monitor` | `monitor_read_channel()` for ADC reads, `monitor_update_config()` for live config push |
| `ads1115` | `ads1115_channel_t` enum for channel identifiers |
| `system_state` | `system_state_get()` for atomic state snapshots |
| `hw_config` | `HW_RELAY_COUNT` constant |
| `relays` | `relay_set()` for direct relay control |
| `esp_app_format` (private) | `esp_app_get_description()` for firmware version info |
| `driver` (private) | `uart_read_bytes()` for monitor keypress detection |

## Usage Notes

- **Init order.** Call `cli_init()` after all other components are initialized (sequencer, monitor, relays, config). The CLI immediately starts accepting commands, and the command handlers assume all subsystem APIs are functional.

- **Config lifecycle.** `config set` and `seq set` only modify the in-memory config struct. To activate changes in the running sequencer and monitor, use `seq apply` (which requires the sequencer to be in RX state). To persist across reboots, use `config save` or `seq save`. These are independent operations -- you can apply without saving for temporary testing.

- **Relay safety.** `relay <n> <on|off>` bypasses the sequencer entirely. Using this command while the sequencer is active (especially during TX) can create unsafe relay states. It is intended for bench testing only.

- **Monitor exit.** The `monitor` command blocks the REPL. Press Enter to return to the prompt. The interval can be tuned from 100 ms to 60 seconds.

- **Log management.** Logging is suppressed globally at startup. Use `log on` to restore INFO-level output, or `log debug sequencer` to enable per-tag debug output. Use `log off` to re-suppress when done.

- **Adding a new command.** Create a `cmd_<name>.c` file with a `register_cmd_<name>(void)` function. Add the file to `CMakeLists.txt` SRCS, add a forward declaration and call in `cli.c`. No other changes are needed.
