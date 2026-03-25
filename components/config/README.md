# config

## Overview

The `config` component owns the persistent application configuration for the ESP32-S3 RF PA sequencer. It defines the `app_config_t` struct -- the single source of truth for all user-tunable parameters -- and provides functions to load, save, reset, and validate that configuration against ESP-IDF's Non-Volatile Storage (NVS). At boot, `main` allocates one `app_config_t` on the stack and passes its pointer to every component that needs it; the config component itself is stateless beyond the NVS partition.

## Key Data Structures

### `seq_step_t` -- one step in a relay sequence

| Field      | Type       | Description                                      |
|------------|------------|--------------------------------------------------|
| `relay_id` | `uint8_t`  | 1--6, matching schematic relay labels             |
| `state`    | `uint8_t`  | 1 = energise relay, 0 = de-energise (bool packed as uint8 for NVS blob portability) |
| `delay_ms` | `uint16_t` | Pause in milliseconds after this step before the next step executes |

### `app_config_t` -- full runtime configuration blob

The entire struct is persisted as a single NVS blob under namespace `seq_cfg`, key `app_cfg`.

| Field group          | Fields                                                        | Defaults                    | Purpose |
|----------------------|---------------------------------------------------------------|-----------------------------|---------|
| TX relay sequence    | `tx_steps[8]`, `tx_num_steps`                                 | R3 ON, R1 ON, R2 ON (3 steps, 1000ms inter-step) | Ordered relay operations when transitioning to transmit |
| RX relay sequence    | `rx_steps[8]`, `rx_num_steps`                                 | R2 OFF, R1 OFF, R3 OFF (3 steps, 1000ms inter-step) | Ordered relay operations when transitioning to receive |
| Fault thresholds     | `swr_fault_threshold`, `temp1_fault_threshold_c`, `temp2_fault_threshold_c` | 3.0 SWR, 65C, 65C | Limits that trigger the sequencer's fault/emergency shutdown path |
| PA relay             | `pa_relay_id`                                                 | 2                           | Which relay `emergency_shutdown()` de-energises immediately |
| Power calibration    | `fwd_power_cal_factor`, `ref_power_cal_factor`                | 1.0, 1.0                   | Scaling factors: P = cal_factor x V^2 |
| Thermistor model     | `thermistor_beta`, `thermistor_r0_ohms`, `thermistor_r_series_ohms` | 3950, 100k, 100k    | NTC Steinhart-Hart beta model parameters for temperature conversion |
| Relay display names  | `relay_names[6][16]`                                          | All empty (no aliases)      | Human-readable labels shown in CLI and web UI (e.g. "PA", "LNA") |

**Constants:**

- `SEQ_MAX_STEPS` = 8 -- maximum relay steps per TX or RX sequence
- `CFG_RELAY_NAME_LEN` = 16 -- maximum relay name length including null terminator
- `HW_RELAY_COUNT` = 6 -- sourced from the `hw_config` component

## API

```c
esp_err_t   config_init(app_config_t *cfg);
esp_err_t   config_save(const app_config_t *cfg);
void        config_defaults(app_config_t *cfg);
const char *config_relay_label(const app_config_t *cfg, uint8_t relay_id,
                               char *buf, size_t buf_len);
esp_err_t   config_set_by_key(app_config_t *cfg, const char *key,
                               const char *value_str, char *err_msg, size_t err_len);
```

### `config_init`

Initialises the NVS flash partition and attempts to load the saved blob into `*cfg`. Three outcomes are possible:

1. **Blob found, size matches** -- config is loaded as-is.
2. **Blob not found** (first boot) -- factory defaults are written to NVS and returned.
3. **Blob size mismatch** (struct changed between firmware versions) -- defaults are written, overwriting the stale blob. This is a destructive reset logged at WARN level.

If NVS itself is corrupt (`ESP_ERR_NVS_NO_FREE_PAGES` or `ESP_ERR_NVS_NEW_VERSION_FOUND`), the partition is erased and re-initialised before proceeding.

### `config_save`

Writes the current in-memory `app_config_t` to NVS. This is explicitly separated from modification so that callers control when persistence happens -- the CLI prints "in memory only" after a set/defaults and requires a separate `config save` command.

### `config_defaults`

Zeroes the struct and populates factory default values. Does not touch NVS. Callers must follow with `config_save` if persistence is desired.

### `config_relay_label`

Formats a relay identifier into a caller-supplied buffer. When the relay has a user-assigned name, the output is `R2/PA`; otherwise just `R2`. Returns `buf` for convenience in `printf` chains.

### `config_set_by_key`

A string-keyed setter that accepts both key and value as strings, performs type conversion and range validation, and writes the result into the corresponding `app_config_t` field. On failure, a human-readable error message is written into `err_msg`.

**Supported keys and their valid ranges:**

| Key               | Type  | Min     | Max        | Maps to                       |
|-------------------|-------|---------|------------|-------------------------------|
| `swr_threshold`   | float | 1.0     | 99.0       | `swr_fault_threshold`         |
| `temp1_threshold` | float | 0.0     | 200.0      | `temp1_fault_threshold_c`     |
| `temp2_threshold` | float | 0.0     | 200.0      | `temp2_fault_threshold_c`     |
| `fwd_cal`         | float | 0.001   | 1000.0     | `fwd_power_cal_factor`        |
| `ref_cal`         | float | 0.001   | 1000.0     | `ref_power_cal_factor`        |
| `therm_beta`      | float | 1.0     | 100000.0   | `thermistor_beta`             |
| `therm_r0`        | float | 1.0     | 10000000.0 | `thermistor_r0_ohms`          |
| `therm_rseries`   | float | 1.0     | 10000000.0 | `thermistor_r_series_ohms`    |
| `pa_relay`        | int   | 1       | 6          | `pa_relay_id`                 |

## Event Flow

The config component is passive -- it has no tasks, queues, or event handlers. All access is caller-driven.

### Boot sequence

```
app_main()
  |
  +-- config_init(&cfg)        <-- NVS init, load or write defaults
  |
  +-- sequencer_init(&cfg)     <-- sequencer copies cfg into its own static
  +-- monitor_init(&cfg)       <-- monitor copies cfg into its own static
  +-- cli_init(&cfg)           <-- CLI holds a mutable pointer to cfg
  +-- web_server_init(&cfg)    <-- web server holds a mutable pointer to cfg
```

### Runtime edit/apply/save workflow

Both the CLI (`config set/save/defaults` commands) and the web API (`POST /api/config`, `POST /api/config/save`, `POST /api/config/defaults`) follow the same three-phase pattern:

1. **Edit** -- `config_set_by_key()` or `config_defaults()` modifies the in-memory `app_config_t`. Changes are immediately visible to any component holding a pointer to it.
2. **Apply** -- Components that took a copy at init (sequencer, monitor) need an explicit `sequencer_update_config()` / `monitor_update_config()` call to pick up changes. The CLI and web server operate directly on the pointer and see changes immediately.
3. **Save** -- `config_save()` persists the in-memory state to NVS so it survives reboot.

This separation is deliberate: it lets the operator review and test parameter changes before committing them to flash, and avoids unnecessary NVS write cycles.

### Data ownership

`main.c` owns the single `app_config_t` instance (static local in `app_main`). The CLI and web server receive a raw pointer and mutate it in-place. The sequencer and monitor each hold a private copy (`memcpy` at init) and expose an `_update_config()` function to resynchronise.

There is no mutex protecting the shared `app_config_t`. This is safe in the current architecture because only the CLI task and the HTTP server task write to it, and both do so through `config_set_by_key()` which performs atomic field writes on naturally-aligned primitives.

## Architecture Decisions

- **Single NVS blob**: The entire `app_config_t` is stored as one binary blob rather than individual NVS key-value pairs. This simplifies versioning (a size mismatch triggers a full reset to defaults) and makes load/save atomic, but means any struct layout change wipes the user's saved configuration.

- **Table-driven key-value setter**: `config_set_by_key` uses static arrays of `config_float_key_t` and `config_int_key_t` descriptors with `offsetof`-based field access. Adding a new configurable parameter requires only a new table entry, not a new code path. The same string keys are used by both the CLI and the REST API, keeping them consistent.

- **Explicit save separation**: Modification and persistence are intentionally decoupled. The CLI and web API both require a separate save action. This avoids wearing out NVS flash during parameter tuning sessions and lets operators test values before committing.

- **Copy vs pointer for consumers**: Components that run in their own FreeRTOS task (sequencer, monitor) hold a private `memcpy` of the config to avoid data races. Components that run in a request-response model (CLI, web server) hold a pointer to the shared original. This is a pragmatic choice that avoids the overhead of a mutex for the common read path.

- **Size-mismatch reset**: When the stored blob size does not match `sizeof(app_config_t)`, the component assumes a firmware update changed the struct layout and resets to defaults. There is no migration path -- this is a conscious simplicity trade-off acceptable for a small embedded device where configuration has few fields.

## Dependencies

| Dependency     | Type            | Purpose |
|----------------|-----------------|---------|
| `nvs_flash`    | ESP-IDF (REQUIRES) | NVS initialisation, read/write blob operations |
| `hw_config`    | Internal (REQUIRES) | `HW_RELAY_COUNT` constant used to size the `relay_names` array |
| `esp_log`      | ESP-IDF (implicit) | Diagnostic logging at INFO/WARN/ERROR levels |

The component has no FreeRTOS dependencies and no runtime allocations (all storage is caller-provided).

## Usage Notes

- **Call `config_init` exactly once**, before any other config function and before passing the config to other components. It initialises the NVS flash partition globally.

- **Struct layout changes reset all saved config.** If you add, remove, or reorder fields in `app_config_t`, all devices will revert to factory defaults on their next boot. There is no versioned migration. Plan field additions at the end of the struct to minimise churn, though this does not prevent the reset.

- **Relay sequences are not covered by `config_set_by_key`.** The TX/RX step arrays are modified through separate CLI commands (`seq`) and web API endpoints (`/api/seq`), not through the key-value setter. Only scalar threshold/calibration parameters use the key-value interface.

- **Relay IDs are 1-indexed everywhere** in this component, matching schematic labels. The `relay_names` array is 0-indexed internally (`relay_names[0]` = Relay 1), but all user-facing APIs use 1-based IDs.

- **The `config_relay_label` function requires a caller-supplied buffer.** A 24-byte buffer is sufficient for the longest possible output (`R6/` + 15 chars of name + null).
