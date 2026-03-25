# system_state

## Overview

`system_state` is the central blackboard for the sequencer firmware. It holds a single `system_state_t` struct that aggregates live readings from every subsystem -- relays, PTT, sequencer FSM, sensor measurements, and Wi-Fi status. Producer subsystems push updates through domain-specific setter functions; consumers obtain an atomic, point-in-time snapshot via `system_state_get()`. The component owns no task and performs no I/O -- it is purely a thread-safe shared-memory rendezvous point.

## Key Data Structures

### `system_state_t`

The snapshot struct that consumers receive. Every field is plain-old-data so the entire struct can be copied with `memcpy`.

| Field | Type | Description |
|---|---|---|
| `relay_states` | `uint8_t` | Bitmask of relay on/off states. Bit 0 corresponds to relay 1, bit 5 to relay 6 (1-indexed IDs mapped to 0-indexed bits). |
| `ptt_active` | `bool` | `true` when the PTT input line is asserted (active-low GPIO is low). |
| `seq_state` | `uint8_t` | Current sequencer FSM state. Stored as `uint8_t` to avoid a header dependency on `sequencer.h`; cast to `seq_state_t` when reading (`SEQ_STATE_RX`, `SEQ_STATE_SEQUENCING_TX`, `SEQ_STATE_TX`, `SEQ_STATE_SEQUENCING_RX`, `SEQ_STATE_FAULT`). |
| `seq_fault` | `uint8_t` | Active fault code. Cast to `seq_fault_t` (`SEQ_FAULT_NONE`, `SEQ_FAULT_HIGH_SWR`, `SEQ_FAULT_OVER_TEMP1`, `SEQ_FAULT_OVER_TEMP2`, `SEQ_FAULT_EMERGENCY`). |
| `fwd_power_w` | `float` | Forward RF power in watts. |
| `ref_power_w` | `float` | Reflected RF power in watts. |
| `swr` | `float` | Standing wave ratio. Initialised to `1.0` (perfect match). |
| `temp1_c` | `float` | Temperature sensor 1 reading in degrees Celsius. |
| `temp2_c` | `float` | Temperature sensor 2 reading in degrees Celsius. |
| `wifi_connected` | `bool` | Whether the station is associated to an AP. |
| `wifi_ip_addr` | `uint32_t` | IPv4 address in network byte order. |
| `wifi_rssi` | `int8_t` | Received signal strength in dBm. |

### Internal state

A single file-scoped instance (`s_state`) of `system_state_t` protected by a FreeRTOS spinlock (`portMUX_TYPE s_mux`). There is no dynamic allocation and no initialisation function -- the module is ready at program startup via static initialisation.

## API

### Writers (one per subsystem)

Each setter enters a critical section, mutates only the fields it owns, and exits. Hold time is a handful of scalar assignments -- never more than a few microseconds.

| Function | Called by | Fields written |
|---|---|---|
| `system_state_set_relay(relay_id, on)` | `relays` | `relay_states` (sets or clears a single bit) |
| `system_state_set_relays_all_off()` | `relays` | `relay_states` (zeroed) |
| `system_state_set_ptt(active)` | `ptt` (ISR context) | `ptt_active` |
| `system_state_set_sequencer(state, fault)` | `sequencer` | `seq_state`, `seq_fault` |
| `system_state_set_sensors(fwd_w, ref_w, swr, temp1_c, temp2_c)` | `monitor` | `fwd_power_w`, `ref_power_w`, `swr`, `temp1_c`, `temp2_c` |
| `system_state_set_wifi(connected, ip_addr, rssi)` | `wifi_sta` | `wifi_connected`, `wifi_ip_addr`, `wifi_rssi` |

`relay_id` is validated (must be 1..`HW_RELAY_COUNT`); out-of-range calls are silently dropped.

### Reader

```c
void system_state_get(system_state_t *out);
```

Copies the entire `s_state` into the caller-provided buffer inside a critical section. The caller receives a consistent snapshot -- no field can be half-updated because the copy is atomic with respect to all writers.

## Event Flow

```
Producers                          Blackboard                Consumers
---------                          ----------                ---------
ptt ISR ----set_ptt--------------->|                |
sequencer task --set_sequencer---->|  s_state       |------> cli (status, monitor, relay, wifi)
monitor task --set_sensors-------->|  (spinlock)    |------> web_server (GET /api/state)
relays --------set_relay---------->|                |
wifi_sta ------set_wifi----------->|                |
```

1. A producer event occurs (GPIO interrupt, sensor poll, FSM transition, Wi-Fi event).
2. The owning subsystem calls the appropriate `system_state_set_*` function.
3. The setter acquires the spinlock, writes the relevant fields, and releases the spinlock.
4. When a consumer needs current state (CLI command, HTTP request, periodic display update), it calls `system_state_get()`, which copies the entire struct under the spinlock.
5. The consumer works with its local copy; no lock is held while it formats output or builds JSON.

There is no notification or callback mechanism -- consumers poll on demand.

## Architecture Decisions

- **Spinlock, not mutex.** A FreeRTOS `portMUX_TYPE` spinlock is used instead of a mutex because `system_state_set_ptt()` is called directly from an ISR (`ptt_isr_handler` is marked `IRAM_ATTR`). Mutexes cannot be taken from ISR context on ESP-IDF; spinlocks can. The critical sections are short enough (scalar assignments or a single `memcpy`) that spinlock contention is negligible.

- **Copy-on-read (snapshot pattern).** `system_state_get()` copies the full struct rather than returning a pointer. This lets consumers hold and inspect state without keeping the lock, and eliminates any risk of reading a partially updated struct.

- **Sequencer types stored as `uint8_t`.** The `seq_state` and `seq_fault` fields are raw integers rather than their enum types (`seq_state_t`, `seq_fault_t`). This breaks what would otherwise be a circular header dependency: `system_state.h` is included by nearly every component, but `sequencer.h` defines the enums and also depends on types from other components. Consumers that need symbolic names include `sequencer.h` separately and cast.

- **No initialisation function.** The module uses C static initialisation (`s_state` zeroed except `swr = 1.0f`, spinlock unlocked). This means the blackboard is available before `app_main` runs, which matters because the PTT ISR can fire as soon as GPIOs are configured.

- **Single writer per field group.** Each setter writes a disjoint set of fields. There is no merge logic or conflict resolution because ownership is partitioned by subsystem at design time.

- **Silent validation on relay ID.** `system_state_set_relay` range-checks `relay_id` against `HW_RELAY_COUNT` (currently 6) and silently returns on invalid input, matching the defensive style used throughout the relay subsystem.

## Dependencies

| Dependency | Role |
|---|---|
| `hw_config` | Provides `HW_RELAY_COUNT` for relay bitmask validation |
| FreeRTOS (`portMUX_TYPE`, `portENTER_CRITICAL`, `portEXIT_CRITICAL`) | Spinlock primitives |
| `string.h` (`memcpy`) | Snapshot copy in `system_state_get` |

The component has no dependency on any other application component. This is intentional -- it sits at the bottom of the dependency graph so that any subsystem can include it without pulling in unrelated headers.

## Usage Notes

- **Consumers must include `sequencer.h` to interpret FSM fields.** The `seq_state` and `seq_fault` values are opaque `uint8_t` from the blackboard's perspective. Cast them:
  ```c
  system_state_t ss;
  system_state_get(&ss);
  if ((seq_state_t)ss.seq_state == SEQ_STATE_FAULT) { ... }
  ```

- **Relay IDs are 1-indexed.** Bit 0 of `relay_states` is relay 1, matching the schematic convention used throughout the firmware. To test relay N: `(ss.relay_states >> (N - 1)) & 1`.

- **The snapshot is a point-in-time copy.** If you need multiple related fields to be consistent with each other (e.g., forward power and SWR), a single `system_state_get` call guarantees they were written in the same `set_sensors` invocation. Calling `system_state_get` twice may yield snapshots from different sensor cycles.

- **No change notification.** The blackboard is passive. Components that need to react to state changes (e.g., the sequencer reacting to PTT) use their own event queues. The blackboard is for observation, not coordination.

- **ISR safety.** All setter functions are safe to call from ISR context because they use `portENTER_CRITICAL` / `portEXIT_CRITICAL` (which map to the appropriate ISR-safe variants when called from interrupt context on Xtensa). The `set_ptt` path exercises this in production.
