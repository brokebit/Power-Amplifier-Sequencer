# cli

## Overview

The CLI component provides an interactive serial REPL (Read-Eval-Print Loop) over UART0 for the ESP32-S3 RF PA sequencer. It is the primary operator interface during development and bench testing, exposing commands that span the full surface area of the system: live status inspection, relay sequencing, fault management, configuration editing, OTA updates, and WiFi provisioning. The component is built on ESP-IDF's `esp_console` framework and follows a one-file-per-command convention, keeping the registration spine thin and each command module self-contained.

The CLI has no direct ownership of configuration state. All config reads go through `config_snapshot()` (a mutex-protected copy) and all config writes go through dedicated service functions in the `config` component. This decouples the CLI from config storage and makes every command handler stateless with respect to configuration.

## Architecture

### File Layout

| File | Registers | Purpose |
|---|---|---|
| `cli.c` | -- | Initialization spine: creates UART REPL, calls all `cli_register_cmd_*()` functions |
| `cmd_status.c` | `status` | One-shot snapshot of full system state |
| `cmd_system.c` | `version`, `reboot` | Firmware/chip info and device restart |
| `cmd_log.c` | `log` | Runtime ESP log level control (globally or per-tag) |
| `cmd_config.c` | `config` | Show, set, save, and factory-reset the `app_config_t` blob |
| `cmd_relay.c` | `relay` | Direct relay control, relay naming |
| `cmd_fault.c` | `fault` | Show, clear, and inject faults into the sequencer FSM |
| `cmd_seq.c` | `seq` | TX/RX relay sequence editor with edit/apply/save workflow |
| `cmd_adc.c` | `adc` | Raw ADS1115 ADC channel reads |
| `cmd_monitor.c` | `monitor` | Continuous polling loop with human-readable or CSV output |
| `cmd_wifi.c` | `wifi` | Credential management, connect/disconnect, scan, enable/disable |
| `cmd_ota.c` | `ota` | GitHub-based OTA: status, repo config, update, rollback, validate |

### Public API

The CLI's public interface is a single parameterless init function declared in `include/cli.h`:

```c
esp_err_t cli_init(void);
```

There are no other public functions. All configuration access happens through the `config` and `sequencer` component APIs.

### Initialization Flow

`cli_init()` is called from `app_main()` with no arguments. The function:

1. Creates a UART REPL with prompt `seq> `, a 256-byte command line buffer, and a 16 KB task stack (sized to accommodate TLS handshakes during OTA).
2. Calls each `cli_register_cmd_*()` function to register commands with `esp_console`.
3. Suppresses all ESP log output (`esp_log_level_set("*", ESP_LOG_NONE)`) so the interactive prompt stays clean. The user can re-enable logging at runtime via the `log` command.
4. Starts the REPL task.

### Command Pattern

Every command module follows the same structure:

- A static handler function matching `int handler(int argc, char **argv)`.
- A public `cli_register_cmd_<name>(void)` function that fills an `esp_console_cmd_t` struct and calls `esp_console_cmd_register()`.
- Subcommand dispatch via `strcmp()` on `argv[1]`.
- Return `0` for success, `1` for error (printed to the user inline).

No command uses `argtable3` structured argument parsing -- all commands do manual `argc`/`argv` parsing. This keeps things simple but means tab-completion hints are not provided beyond the top-level command name.

## Key Data Structures

### Config Access Pattern

The CLI does not hold any pointer or reference to the `app_config_t` struct. Instead, command handlers interact with config through service functions provided by the `config` component:

**Reads** -- any handler that needs config data calls `config_snapshot(&snap)` to obtain a mutex-protected copy of the current draft into a stack-local `app_config_t`. This is used by `cmd_status.c`, `cmd_config.c` (`config show`), `cmd_relay.c` (`relay show`, `relay name`, direct control feedback), and `cmd_seq.c` (`seq tx/rx show`, post-set confirmation).

**Writes** -- handlers never mutate config directly. Each write path calls a specific service function that acquires the config mutex internally:

| Service function | Called by | Purpose |
|---|---|---|
| `config_set_by_key(key, val, err, len)` | `cmd_config.c` (`config set`) | Set a scalar config field by string key with type conversion and range validation |
| `config_set_relay_name(id, name, err, len)` | `cmd_relay.c` (`relay name`) | Set or clear a relay display name |
| `config_set_sequence(is_tx, steps, n, err, len)` | `cmd_seq.c` (`seq tx/rx set`) | Replace an entire TX or RX relay sequence |
| `config_apply()` | `cmd_seq.c` (`seq apply`) | Push draft config to live consumers via registered callbacks |
| `config_save()` | `cmd_config.c`, `cmd_seq.c` | Persist draft config to NVS |
| `config_defaults()` | `cmd_config.c` (`config defaults`) | Reset draft to factory values without touching NVS |

The edit/apply/save workflow remains three-phase:

1. **Edit** -- Service functions like `config_set_by_key()` or `config_set_sequence()` modify the internal draft. The draft is protected by a mutex; CLI handlers never touch it directly.
2. **Apply** -- `config_apply()` pushes the draft to all live consumers (sequencer, monitor) through a registered-callback chain. First callback failure stops the chain and returns an error.
3. **Save** -- `config_save()` persists the draft to NVS. Without this step, changes are lost on reboot.

This three-phase design prevents half-edited config from reaching the sequencer, and avoids NVS wear from exploratory changes.

### System State Snapshot (`system_state_t`)

Commands `status`, `monitor`, `relay show`, and `wifi status` read an atomic snapshot of the system via `system_state_get()`. The snapshot includes PTT state, sequencer FSM state/fault, relay bitmask, sensor readings (power, SWR, temperatures), and WiFi connection info. The CLI is a pure consumer -- it never writes to the blackboard.

### Fault Injection

`cmd_fault.c` uses `sequencer_inject_fault(fault)` to post faults into the sequencer FSM. This is a dedicated API that encapsulates event queue access -- the CLI does not interact with event queues directly. The fault type is parsed from user input via `seq_fault_parse()`, which validates the string and returns the corresponding `seq_fault_t` enum value.

## Command Reference

### status
```
status
```
Prints a multi-line summary of PTT, sequencer state, fault status, relay states (with configured names via `config_relay_label()`), power/SWR readings, temperatures, and WiFi connectivity.

### version / reboot
```
version
reboot
```
`version` prints firmware name, version, build date, IDF version, and chip info. `reboot` triggers `esp_restart()` after a 100 ms delay to allow the message to flush.

### log
```
log <on|off|none|error|warn|info|debug|verbose> [tag]
```
Wraps `esp_log_level_set()`. Without a tag argument, applies to all tags (`"*"`). Since the REPL suppresses all logging at startup, `log on` (or a more specific level) is needed to see runtime log output.

### config
```
config show
config set <key> <value>
config save
config defaults
```
- `show` takes a `config_snapshot()` and prints the full `app_config_t` contents (thresholds, calibration, thermistor params, relay names, TX/RX sequences).
- `set` delegates to `config_set_by_key()` which handles string-to-float/int conversion and range validation. Valid keys: `swr_threshold`, `temp1_threshold`, `temp2_threshold`, `fwd_cal`, `ref_cal`, `therm_beta`, `therm_r0`, `therm_rseries`, `pa_relay`.
- `save` calls `config_save()` to persist to NVS.
- `defaults` calls `config_defaults()` to reset the in-memory draft to factory values without touching NVS. Requires explicit `config save` to persist.

### relay
```
relay show
relay <1-6> <on|off>
relay name
relay name <1-6> [label]
```
- `show` reads the system state bitmask and a `config_snapshot()`, displaying each relay's state with its configured label.
- Direct on/off control bypasses the sequencer via `relay_set()` and prints a warning. Useful for bench testing but dangerous during live operation.
- `relay name` calls `config_set_relay_name()` to manage user-friendly relay labels (max 15 chars). Labels appear throughout the CLI wherever relay IDs are displayed. Omitting the label clears the name.

### fault
```
fault show
fault clear
fault inject <swr|temp1|temp2|emergency>
```
- `show` queries `sequencer_get_state()` and `sequencer_get_fault()` to print the current sequencer state and fault code.
- `clear` calls `sequencer_clear_fault()` to return from FAULT to RX. Fails if not in FAULT state.
- `inject` parses the fault type via `seq_fault_parse()`, then calls `sequencer_inject_fault()` to post the fault into the sequencer FSM. This is a testing/debugging tool.

### seq
```
seq tx show
seq rx show
seq tx set R<n>:<on|off>:<ms> [...]
seq rx set R<n>:<on|off>:<ms> [...]
seq apply
seq save
```
The sequence editor. Steps are specified in a compact token format, e.g., `R3:on:50` means "turn relay 3 ON, then wait 50 ms." Up to 8 steps per sequence (`SEQ_MAX_STEPS`). Relay IDs are 1-indexed and delays are capped at 10,000 ms.

`set` calls `config_set_sequence()` to write steps into the config draft. `apply` calls `config_apply()` to push the draft to live consumers through the registered callback chain. `save` calls `config_save()` to persist to NVS. This three-step workflow is the core edit/apply/save pattern.

### adc
```
adc scan
adc read <0-3>
```
Raw voltage reads from the ADS1115 ADC (chip at address 0x49). `scan` reads all four channels with descriptive labels (fwd power, ref power, temp1, temp2). Calls `monitor_read_channel()` which performs a synchronous I2C transaction.

### monitor
```
monitor [interval_ms] [csv]
```
Continuous polling loop that prints system state every `interval_ms` (default 1000, range 100-60000). Exits on Enter keypress (detected by polling UART0 in 50 ms increments). In CSV mode, outputs machine-parseable rows: `ptt,state,fault,r1,r2,...,r6,fwd,ref,swr,t1,t2`. The human-readable format uses a compact single-line layout.

### wifi
```
wifi status
wifi config <ssid> [password]
wifi connect
wifi disconnect
wifi scan
wifi enable
wifi disable
wifi erase
```
Full WiFi lifecycle management. Credentials are stored in NVS by the `wifi_sta` component. `enable`/`disable` controls auto-connect behavior across reboots. `erase` removes stored credentials.

### ota
```
ota status
ota repo [owner/repo]
ota update <latest|vX.Y.Z|https://...>
ota rollback
ota validate
```
GitHub Releases-based OTA. `repo` configures/displays the GitHub repository. `update` downloads and flashes a firmware binary (successful update reboots). `rollback` reverts to the previous OTA partition (also reboots). `validate` marks the current image as good, preventing automatic rollback.

## Dependencies

### ESP-IDF Components (REQUIRES)
- `console` -- REPL framework, command registration
- `driver` -- UART byte polling in `cmd_monitor.c`
- `esp_app_format` -- firmware version metadata

### Internal Components
- `config` -- `app_config_t` type, `config_snapshot()`, `config_set_by_key()`, `config_set_relay_name()`, `config_set_sequence()`, `config_apply()`, `config_save()`, `config_defaults()`, `config_relay_label()`
- `sequencer` -- FSM state queries (`sequencer_get_state()`, `sequencer_get_fault()`), fault management (`sequencer_clear_fault()`, `sequencer_inject_fault()`), name helpers (`seq_state_name()`, `seq_fault_name()`, `seq_fault_parse()`)
- `monitor` -- ADC channel reads (`monitor_read_channel()`)
- `ads1115` -- Channel enum types (`ads1115_channel_t`)
- `system_state` -- Atomic state snapshot reads (`system_state_get()`)
- `hw_config` -- `HW_RELAY_COUNT` constant, `SEQ_MAX_STEPS` (via config)
- `relays` -- Direct relay set for bench testing (`relay_set()`)
- `wifi_sta` -- Credential management, connect/disconnect, scan, enable/disable
- `ota` -- Update, rollback, validate, repo management

## Architecture Decisions

- **Stateless command handlers with snapshot reads.** Command handlers do not cache or hold references to the config struct. Every read path calls `config_snapshot()` to obtain a stack-local copy. This eliminates shared mutable state between the CLI and other tasks, and means the CLI component has zero file-scoped mutable state related to configuration.

- **Service-function writes instead of direct struct mutation.** All config modifications go through dedicated functions (`config_set_by_key`, `config_set_relay_name`, `config_set_sequence`) that lock internally and perform validation. This means the CLI never calls `config_lock()`/`config_unlock()` and cannot produce partially-written config state.

- **Callback-based config_apply().** Rather than the CLI knowing which subsystems consume config (previously it called `sequencer_update_config()` and `monitor_update_config()` directly), `config_apply()` invokes a chain of registered callbacks. The CLI just calls `config_apply()` and gets back success or the first failure. Adding a new config consumer requires no CLI changes.

- **Fault injection via sequencer_inject_fault().** The CLI no longer accesses the sequencer event queue directly. `sequencer_inject_fault()` encapsulates the event construction and queue posting, keeping the queue handle private to the sequencer component.

- **Parameterless cli_init().** The initialization function takes no arguments. Since the CLI accesses config through `config_snapshot()` and service functions (which operate on the config component's internal state), there is no pointer to pass. This makes the init call site simpler and removes a coupling between `app_main()` and the CLI's internal config access pattern.

- **Logging suppressed at startup.** The REPL sets `ESP_LOG_NONE` globally to keep the interactive prompt usable. Without this, log output from other FreeRTOS tasks interleaves with the prompt and typed input. The `log` command allows surgical re-enablement per-tag when debugging.

- **16 KB REPL task stack.** Explicitly oversized relative to a typical console task. The comment in `cli.c` explains this is to accommodate TLS handshake allocations during OTA updates, which run on the REPL task's stack.

- **Manual argv parsing instead of argtable3.** While ESP-IDF's console framework supports `argtable3` for structured argument definitions with auto-generated help, this component uses simple `strcmp`/`strtol` dispatch. The trade-off is less ceremony per command at the cost of no structured tab-completion hints.

- **Relay IDs are 1-indexed everywhere.** Consistent with schematic labels and the `relays` component convention. The CLI never exposes 0-indexed relay numbers to the user.

- **Monitor uses UART polling for exit detection.** Rather than a separate control mechanism, `cmd_monitor.c` polls `uart_read_bytes()` in 50 ms increments within the delay loop, checking for Enter to break out. This avoids needing a semaphore or separate task for the polling loop.

## Usage Notes

- After changing config values or sequences, remember the three-step workflow: **edit** (in-memory draft), **apply** (push to live subsystems), **save** (persist to NVS). Forgetting `apply` means changes sit in the config draft but the sequencer runs old values. Forgetting `save` means changes are lost on reboot.

- Direct relay control via `relay <n> on|off` bypasses the sequencer FSM. This is safe for bench testing with no RF power present, but can cause undefined sequencing behavior if used while the sequencer is active.

- Fault injection via `fault inject` calls `sequencer_inject_fault()` which encapsulates the event queue posting. The fault type string is validated by `seq_fault_parse()` before injection.

- Adding a new command requires: (1) a new `cmd_<name>.c` file with handler and register functions, (2) a forward declaration and call in `cli.c`, and (3) adding the source file and any new component dependencies to `CMakeLists.txt`.

- Adding a new config consumer that should respond to `seq apply` / `config apply` does **not** require CLI changes. Register a callback via `config_register_apply_cb()` in the consumer's init function instead.
