# cli

## Overview

The CLI component provides an interactive serial REPL (Read-Eval-Print Loop) over UART0 for the ESP32-S3 RF PA sequencer. It is the primary operator interface during development and bench testing, exposing commands that span the full surface area of the system: live status inspection, relay sequencing, fault management, configuration editing, OTA updates, and WiFi provisioning. The component is built on ESP-IDF's `esp_console` framework and follows a one-file-per-command convention, keeping the registration spine thin and each command module self-contained.

## Architecture

### File Layout

| File | Registers | Purpose |
|---|---|---|
| `cli.c` | -- | Initialization spine: stores config pointer, creates UART REPL, calls all `cli_register_cmd_*()` functions |
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

### Initialization Flow

`cli_init()` is called from `app_main()` with a pointer to the live `app_config_t` owned by main. The function:

1. Stores the config pointer in a file-scoped static (`s_cfg`) so command handlers can reach it via `cli_get_config()`.
2. Creates a UART REPL with prompt `seq> `, a 256-byte command line buffer, and a 16 KB task stack (sized to accommodate TLS handshakes during OTA).
3. Calls each `cli_register_cmd_*()` function to register commands with `esp_console`.
4. Suppresses all ESP log output (`esp_log_level_set("*", ESP_LOG_NONE)`) so the interactive prompt stays clean. The user can re-enable logging at runtime via the `log` command.
5. Starts the REPL task.

### Command Pattern

Every command module follows the same structure:

- A static handler function matching `int handler(int argc, char **argv)`.
- A public `cli_register_cmd_<name>(void)` function that fills an `esp_console_cmd_t` struct and calls `esp_console_cmd_register()`.
- Subcommand dispatch via `strcmp()` on `argv[1]`.
- Return `0` for success, `1` for error (printed to the user inline).

No command uses `argtable3` structured argument parsing -- all commands do manual `argc`/`argv` parsing. This keeps things simple but means tab-completion hints are not provided beyond the top-level command name.

## Key Data Structures

### Config Pointer (`app_config_t *`)

The CLI holds a shared mutable pointer to the single `app_config_t` instance owned by `app_main()`. Multiple commands (`config`, `seq`, `relay name`) modify this struct in-place. The edit/apply/save workflow is:

1. **Edit** -- Commands like `config set` or `seq tx set` mutate the in-memory `app_config_t` immediately.
2. **Apply** -- `seq apply` calls `sequencer_update_config()` and `monitor_update_config()` to push the edited config to live subsystems. This is only permitted when the sequencer is in the RX idle state.
3. **Save** -- `config save` or `seq save` persists the blob to NVS. Without this step, changes are lost on reboot.

This three-phase design prevents half-edited config from reaching the sequencer, and avoids NVS wear from exploratory changes.

### System State Snapshot (`system_state_t`)

Commands `status`, `monitor`, `relay show`, and `wifi status` read an atomic snapshot of the system via `system_state_get()`. The snapshot includes PTT state, sequencer FSM state/fault, relay bitmask, sensor readings (power, SWR, temperatures), and WiFi connection info. The CLI is a pure consumer -- it never writes to the blackboard.

### Sequencer Events (`seq_event_t`)

Only `cmd_fault.c` writes to the sequencer event queue (via `xQueueSend` to `sequencer_get_event_queue()`). This is used exclusively for fault injection during testing.

## Command Reference

### status
```
status
```
Prints a single-line summary of PTT, sequencer state, fault status, relay states (with configured names), power/SWR readings, temperatures, and WiFi connectivity.

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
- `show` prints the full `app_config_t` contents (thresholds, calibration, thermistor params, relay names, TX/RX sequences).
- `set` delegates to `config_set_by_key()` which handles string-to-float/int conversion and range validation. Valid keys: `swr_threshold`, `temp1_threshold`, `temp2_threshold`, `fwd_cal`, `ref_cal`, `therm_beta`, `therm_r0`, `therm_rseries`, `pa_relay`.
- `save` persists to NVS.
- `defaults` resets the in-memory struct to factory values without touching NVS. Requires explicit `config save` to persist.

### relay
```
relay show
relay <1-6> <on|off>
relay name
relay name <1-6> [label]
```
- `show` reads the system state bitmask and displays each relay's state with its configured label.
- Direct on/off control bypasses the sequencer and prints a warning. Useful for bench testing but dangerous during live operation.
- `relay name` manages user-friendly relay labels (max 15 chars). Labels appear throughout the CLI wherever relay IDs are displayed. Omitting the label clears the name.

### fault
```
fault show
fault clear
fault inject <swr|temp1|temp2|emergency>
```
- `show` prints the current sequencer state and fault code.
- `clear` calls `sequencer_clear_fault()` to return from FAULT to RX. Fails if not in FAULT state.
- `inject` posts a `SEQ_EVENT_FAULT` (or `SEQ_EVENT_EMERGENCY_PA_OFF` for emergency) directly to the sequencer event queue. This is a testing/debugging tool.

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

`set` writes steps into the in-memory config. `apply` pushes to the live sequencer (only when in RX state). `save` persists to NVS. This three-step workflow is the core edit/apply/save pattern.

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
- `config` -- `app_config_t` type, NVS load/save, `config_set_by_key()`
- `sequencer` -- FSM state queries, fault clear, event queue access, config update
- `monitor` -- ADC channel reads, config update
- `ads1115` -- Channel enum types
- `system_state` -- Atomic state snapshot reads
- `hw_config` -- `HW_RELAY_COUNT` constant
- `relays` -- Direct relay set for bench testing
- `wifi_sta` -- Credential management, connect/disconnect, scan
- `ota` -- Update, rollback, validate, repo management

## Architecture Decisions

- **Logging suppressed at startup.** The REPL sets `ESP_LOG_NONE` globally to keep the interactive prompt usable. Without this, log output from other FreeRTOS tasks interleaves with the prompt and typed input. The `log` command allows surgical re-enablement per-tag when debugging.

- **Shared mutable config pointer rather than message passing.** The CLI operates on main's `app_config_t` directly rather than going through a queue or accessor API. This simplifies the edit workflow but means config mutations are not inherently thread-safe. In practice, CLI commands execute sequentially on the REPL task, and `seq apply` is the only point where the config crosses a task boundary (via `sequencer_update_config()`, which copies the data).

- **16 KB REPL task stack.** Explicitly oversized relative to a typical console task. The comment in `cli.c` explains this is to accommodate TLS handshake allocations during OTA updates, which run on the REPL task's stack.

- **Manual argv parsing instead of argtable3.** While ESP-IDF's console framework supports `argtable3` for structured argument definitions with auto-generated help, this component uses simple `strcmp`/`strtol` dispatch. The trade-off is less ceremony per command at the cost of no structured tab-completion hints.

- **Relay IDs are 1-indexed everywhere.** Consistent with schematic labels and the `relays` component convention. The CLI never exposes 0-indexed relay numbers to the user.

- **Monitor uses UART polling for exit detection.** Rather than a separate control mechanism, `cmd_monitor.c` polls `uart_read_bytes()` in 50 ms increments within the delay loop, checking for Enter to break out. This avoids needing a semaphore or separate task for the polling loop.

## Usage Notes

- After changing config values or sequences, remember the three-step workflow: **edit** (in-memory), **apply** (push to live subsystems), **save** (persist to NVS). Forgetting `apply` means changes sit in the config struct but the sequencer runs old values. Forgetting `save` means changes are lost on reboot.

- Direct relay control via `relay <n> on|off` bypasses the sequencer FSM. This is safe for bench testing with no RF power present, but can cause undefined sequencing behavior if used while the sequencer is active.

- Fault injection via `fault inject` posts directly to the sequencer event queue. The emergency fault type triggers `SEQ_EVENT_EMERGENCY_PA_OFF` which immediately de-energises the PA relay, while other fault types use the normal `SEQ_EVENT_FAULT` path.

- Adding a new command requires: (1) a new `cmd_<name>.c` file with handler and register functions, (2) a forward declaration and call in `cli.c`, and (3) adding the source file and any new component dependencies to `CMakeLists.txt`.
