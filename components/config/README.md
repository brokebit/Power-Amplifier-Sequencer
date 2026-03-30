# config

## Overview

The `config` component owns the persistent application configuration for the ESP32-S3 RF PA sequencer. It defines the `app_config_t` struct -- the single source of truth for all user-tunable parameters -- and provides functions to load, save, reset, mutate, and apply that configuration against ESP-IDF's Non-Volatile Storage (NVS). The component internally owns a single draft `app_config_t` instance protected by a FreeRTOS mutex; external code never holds a raw pointer to it. Instead, callers obtain thread-safe snapshots via `config_snapshot()` and push changes to live consumers via `config_apply()`.

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

### Internal state (not directly accessible)

The component maintains three static variables behind the mutex:

| Variable         | Type              | Purpose |
|------------------|-------------------|---------|
| `s_draft`        | `app_config_t`    | The authoritative runtime config. All mutation functions write here. |
| `s_last_applied` | `app_config_t`    | Snapshot captured at the last successful `config_apply()`. Used by `config_pending_apply()` to detect unsaved drift. Initialised to match `s_draft` at boot so `config_pending_apply()` returns false until something changes. |
| `s_apply_cbs[]`  | `config_apply_cb_t[4]` | Registered callback functions invoked by `config_apply()`. |

**Constants:**

- `SEQ_MAX_STEPS` = 8 -- maximum relay steps per TX or RX sequence
- `SEQ_MAX_DELAY_MS` = 10000 -- maximum inter-step delay in milliseconds
- `CFG_RELAY_NAME_LEN` = 16 -- maximum relay name length including null terminator
- `HW_RELAY_COUNT` = 6 -- sourced from the `hw_config` component
- `CONFIG_MAX_APPLY_CBS` = 4 -- maximum number of apply callbacks

## API

```c
/* Lifecycle */
esp_err_t   config_init(void);
esp_err_t   config_save(void);
void        config_defaults(void);

/* Read */
void        config_snapshot(app_config_t *out);
const char *config_relay_label(const app_config_t *cfg, uint8_t relay_id,
                               char *buf, size_t buf_len);

/* Mutate */
esp_err_t   config_set_by_key(const char *key, const char *value_str,
                              char *err_msg, size_t err_len);
esp_err_t   config_set_relay_name(uint8_t relay_id, const char *name,
                                  char *err_msg, size_t err_len);
esp_err_t   config_set_sequence(bool is_tx, const seq_step_t *steps, uint8_t count,
                                char *err_msg, size_t err_len);

/* Apply (push to live consumers) */
esp_err_t   config_register_apply_cb(config_apply_cb_t cb);
esp_err_t   config_apply(void);
bool        config_pending_apply(void);

/* Locking (for direct struct access -- prefer the functions above) */
void        config_lock(void);
void        config_unlock(void);
```

### `config_init`

Initialises the NVS flash partition, creates the config mutex, and attempts to load the saved blob into the internal draft. Three outcomes are possible:

1. **Blob found, size matches** -- config is loaded as-is.
2. **Blob not found** (first boot) -- factory defaults are written to NVS and stored in the draft.
3. **Blob size mismatch** (struct changed between firmware versions) -- defaults are written, overwriting the stale blob. This is a destructive reset logged at WARN level.

If NVS itself is corrupt (`ESP_ERR_NVS_NO_FREE_PAGES` or `ESP_ERR_NVS_NEW_VERSION_FOUND`), the partition is erased and re-initialised before proceeding.

After loading, `s_last_applied` is set to match `s_draft` so that `config_pending_apply()` returns `false` at boot.

### `config_save`

Writes the current internal draft to NVS. Acquires the config mutex internally. This is explicitly separated from mutation so that callers control when persistence happens -- the CLI prints "in memory only" after a set/defaults and requires a separate `config save` command.

### `config_defaults`

Zeroes the draft and populates factory default values. Acquires the mutex internally. Does not touch NVS. Callers must follow with `config_save` if persistence is desired.

### `config_snapshot`

Copies the internal draft into a caller-supplied `app_config_t` under the mutex. This is the standard read path -- all external code that needs to inspect config values should call this rather than holding a raw pointer.

### `config_relay_label`

Formats a relay identifier into a caller-supplied buffer. When the relay has a user-assigned name, the output is `R2/PA`; otherwise just `R2`. Takes an `app_config_t` pointer (typically from a snapshot) and returns `buf` for convenience in `printf` chains.

### `config_set_by_key`

A string-keyed setter that accepts both key and value as strings, performs type conversion and range validation, and writes the result into the corresponding draft field. On failure, a human-readable error message is written into `err_msg`. Acquires the mutex internally.

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

### `config_set_relay_name`

Sets a relay display name in the draft. Validates relay ID (1--`HW_RELAY_COUNT`) and name length (< `CFG_RELAY_NAME_LEN`). Passing `NULL` or an empty string clears the alias. Acquires the mutex internally.

### `config_set_sequence`

Writes a complete TX or RX sequence into the draft. Validates step count (1--`SEQ_MAX_STEPS`) and each step individually: relay ID within range, state 0 or 1, delay within `SEQ_MAX_DELAY_MS`. Acquires the mutex internally.

### `config_register_apply_cb`

Registers a callback function to be invoked by `config_apply()`. Callbacks run in registration order. The function is idempotent -- registering the same function pointer again is a no-op. Returns `ESP_ERR_NO_MEM` if the callback array is full (max `CONFIG_MAX_APPLY_CBS`), `ESP_ERR_INVALID_ARG` on `NULL`.

Callback signature: `esp_err_t (*config_apply_cb_t)(const app_config_t *cfg)`

The callback receives a snapshot of the draft. It must not return `ESP_OK` until the consumer has actually committed the new config internally -- this contract is what makes `config_pending_apply()` reliable.

### `config_apply`

Pushes the current draft to all registered consumers. Takes a snapshot of the draft under the mutex, then calls each registered callback in order, passing the snapshot. If any callback returns an error, the chain stops and `config_apply()` returns that error -- the `s_last_applied` snapshot is not updated, so `config_pending_apply()` continues to return `true`.

On success, `s_last_applied` is updated to match the snapshot, and `config_pending_apply()` returns `false`.

### `config_pending_apply`

Returns `true` when the draft differs from the last successfully applied config (byte-level `memcmp`). Returns `false` at boot (before any mutations) and immediately after a successful `config_apply()`.

### `config_lock` / `config_unlock`

Expose the config mutex for external code that needs direct struct access. All public mutation and read functions in this component lock internally; these are only needed by callers doing something the typed setters do not cover.

## Event Flow

The config component is passive -- it has no tasks, queues, or event handlers. All access is caller-driven.

### Boot sequence

```
app_main()
  |
  +-- config_init()                         <-- NVS init, load or write defaults
  |
  +-- sequencer_init()
  |     +-- config_snapshot(&s_cfg)         <-- reads initial config
  |     +-- config_register_apply_cb(...)   <-- subscribes to live updates
  |
  +-- monitor_init()
  |     +-- config_snapshot(&s_cfg)
  |     +-- config_register_apply_cb(...)
  |
  +-- cli_init()                            <-- CLI uses snapshot/set/apply/save calls
  +-- web_server_init()                     <-- web server uses snapshot/set/apply/save calls
```

### Runtime edit/apply/save workflow

Both the CLI (`config set/save/defaults` commands) and the web API (`POST /api/config`, `POST /api/config/save`, `POST /api/seq`) follow the same three-phase pattern:

1. **Edit** -- `config_set_by_key()`, `config_set_relay_name()`, `config_set_sequence()`, or `config_defaults()` mutates the internal draft. The change is invisible to live consumers until applied.
2. **Apply** -- `config_apply()` pushes a snapshot of the draft to all registered callbacks (sequencer, monitor). `config_pending_apply()` can be polled to show the user whether unapplied changes exist.
3. **Save** -- `config_save()` persists the draft to NVS so it survives reboot.

This separation is deliberate: it lets the operator review and test parameter changes before committing them to flash, and avoids unnecessary NVS write cycles. The web UI uses `config_pending_apply()` to display a visual indicator when the draft has drifted from the running config.

### Data ownership

The config component owns the single `app_config_t` draft internally as a static variable. No external code holds a mutable pointer to it. All reads go through `config_snapshot()`, and all writes go through the typed setter functions. This eliminates the data-race hazard that existed in the prior design where `main.c` owned the struct and multiple tasks shared a raw pointer.

The FreeRTOS mutex (`s_cfg_mutex`) is acquired internally by every public function that touches the draft. External code should never need to call `config_lock()`/`config_unlock()` unless doing something not covered by the existing API.

## Architecture Decisions

- **Internally-owned draft**: The config component now owns its `app_config_t` as a module-level static rather than operating on an externally-provided pointer. This centralises all mutation behind validated, mutex-protected functions and eliminates the class of bugs where multiple tasks write to the same struct without synchronisation.

- **Snapshot-based reads**: `config_snapshot()` copies the entire struct under the lock. This is a deliberate trade-off: the 100-odd byte copy is cheap on ESP32-S3, and it means readers hold a consistent, immutable view with zero contention on the mutex after the copy completes.

- **Observer pattern for apply**: `config_register_apply_cb()` / `config_apply()` implements a simple observer pattern so the config component does not need compile-time knowledge of its consumers. Sequencer and monitor register themselves at init; config just calls the function pointers. This replaces the prior design where CLI and web server had to explicitly call `sequencer_update_config()` and `monitor_update_config()`.

- **Fail-fast apply chain**: `config_apply()` stops at the first callback failure and does not update `s_last_applied`. This means `config_pending_apply()` remains `true`, signalling to the UI that the running system is still on the old config. Failable callbacks should be registered before infallible ones to preserve all-or-nothing semantics.

- **Single NVS blob**: The entire `app_config_t` is stored as one binary blob rather than individual NVS key-value pairs. This simplifies versioning (a size mismatch triggers a full reset to defaults) and makes load/save atomic, but means any struct layout change wipes the user's saved configuration.

- **Table-driven key-value setter**: `config_set_by_key` uses static arrays of `config_float_key_t` and `config_int_key_t` descriptors with `offsetof`-based field access. Adding a new configurable parameter requires only a new table entry, not a new code path. The same string keys are used by both the CLI and the REST API.

- **Explicit save separation**: Mutation and persistence are intentionally decoupled. The CLI and web API both require a separate save action. This avoids wearing out NVS flash during parameter tuning sessions and lets operators test values before committing.

- **Size-mismatch reset**: When the stored blob size does not match `sizeof(app_config_t)`, the component assumes a firmware update changed the struct layout and resets to defaults. There is no migration path -- this is a conscious simplicity trade-off acceptable for a small embedded device where configuration has few fields.

## Dependencies

| Dependency     | Type            | Purpose |
|----------------|-----------------|---------|
| `nvs_flash`    | ESP-IDF (REQUIRES) | NVS initialisation, read/write blob operations |
| `hw_config`    | Internal (REQUIRES) | `HW_RELAY_COUNT` constant used to size the `relay_names` array and validate relay IDs |
| `freertos`     | ESP-IDF (implicit) | Mutex for thread-safe access to the internal draft |
| `esp_log`      | ESP-IDF (implicit) | Diagnostic logging at INFO/WARN/ERROR levels |

The component has no runtime heap allocations. All storage is module-level static.

## Usage Notes

- **Call `config_init` exactly once**, before any other config function and before initialising other components. It creates the mutex and initialises the NVS flash partition globally.

- **Use `config_snapshot` for all reads.** Do not attempt to access the draft directly. The snapshot gives you a consistent, lock-free copy to work with.

- **Use the typed setters for all writes.** `config_set_by_key`, `config_set_relay_name`, and `config_set_sequence` validate input and acquire the mutex internally. Only fall back to `config_lock()`/`config_unlock()` if you truly need to do something the existing API does not support.

- **Struct layout changes reset all saved config.** If you add, remove, or reorder fields in `app_config_t`, all devices will revert to factory defaults on their next boot. There is no versioned migration. Plan field additions at the end of the struct to minimise churn, though this does not prevent the reset.

- **Relay IDs are 1-indexed everywhere** in this component, matching schematic labels. The `relay_names` array is 0-indexed internally (`relay_names[0]` = Relay 1), but all user-facing APIs use 1-based IDs.

- **The `config_relay_label` function requires a caller-supplied buffer.** A 24-byte buffer is sufficient for the longest possible output (`R6/` + 15 chars of name + null).

- **Register apply callbacks early.** Consumers (sequencer, monitor) should call `config_register_apply_cb` during their init, before any runtime config changes can occur. The callback array is fixed-size (`CONFIG_MAX_APPLY_CBS` = 4) and does not grow dynamically.

- **Failable callbacks should be registered first.** `config_apply()` stops on the first error. If an infallible callback runs before a failable one and the failable one fails, the infallible consumer has already committed the new config while the failable one has not -- breaking the all-or-nothing invariant.
