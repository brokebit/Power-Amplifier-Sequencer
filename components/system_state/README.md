# system_state

## Overview

`system_state` implements a shared blackboard (observable state snapshot) for the ESP32-S3 RF PA sequencer. It aggregates live state from every subsystem -- relays, PTT, sequencer FSM, and sensor readings -- into a single `system_state_t` struct. Producers call fine-grained setter functions to update individual fields; consumers call `system_state_get()` to obtain a consistent, point-in-time copy of the entire struct. All access is serialised with a FreeRTOS spinlock (`portMUX_TYPE`), keeping critical sections down to a single field write or `memcpy`.

## Key Data Structures

### `system_state_t`

| Field           | Type      | Description |
|-----------------|-----------|-------------|
| `relay_states`  | `uint8_t` | Bitmask of relay hardware state. Bit 0 = Relay 1, up to bit 5 = Relay 6. Matches the 1-indexed relay IDs used elsewhere (bit position = `relay_id - 1`). |
| `ptt_active`    | `bool`    | `true` when the PTT input is asserted (active-low GPIO is driven low). |
| `seq_state`     | `uint8_t` | Current sequencer FSM state. Stored as `uint8_t` to avoid a header dependency on `sequencer.h`; cast to `seq_state_t` when reading. Values: `SEQ_STATE_RX`, `SEQ_STATE_SEQUENCING_TX`, `SEQ_STATE_TX`, `SEQ_STATE_SEQUENCING_RX`, `SEQ_STATE_FAULT`. |
| `seq_fault`     | `uint8_t` | Latched fault code. Cast to `seq_fault_t` when reading. `SEQ_FAULT_NONE` (0) when healthy. |
| `fwd_power_w`   | `float`   | Forward RF power in watts. |
| `ref_power_w`   | `float`   | Reflected RF power in watts. |
| `swr`           | `float`   | Standing wave ratio. Initialised to `1.0f` (perfect match / no reading). |
| `temp1_c`       | `float`   | Temperature sensor 1 reading in degrees Celsius. |
| `temp2_c`       | `float`   | Temperature sensor 2 reading in degrees Celsius. |

### Internal state

A single file-scoped `s_state` instance is the authoritative copy. There is no dynamic allocation and no initialisation function -- the struct is zero-initialised at load time (with `swr` defaulting to `1.0f`).

## Event Flow

```
 Producers                          Blackboard                   Consumers
 ─────────                          ──────────                   ─────────
 relays.c ──── set_relay() ────┐
               set_relays_     │
               all_off() ──────┤
                               │
 ptt.c ────── set_ptt() ──────┤    ┌──────────────┐
                               ├──▶│  s_state      │──▶ system_state_get()
 sequencer.c─ set_sequencer()─┤    │  (spinlock)   │      ├─ display (planned)
                               │    └──────────────┘      └─ console logger (planned)
 monitor.c ── set_sensors() ──┘
```

1. **Relay driver** (`relays.c`) calls `system_state_set_relay(id, on)` or `system_state_set_relays_all_off()` immediately after toggling GPIO levels. The bitmask is updated atomically inside the spinlock.
2. **PTT ISR** (`ptt.c`) calls `system_state_set_ptt()` in its GPIO interrupt handler, recording whether PTT is asserted before posting the event to the sequencer queue.
3. **Sequencer task** (`sequencer.c`) calls `system_state_set_sequencer(state, fault)` on every FSM state transition -- including entry into fault state during `emergency_shutdown()`.
4. **Monitor task** (`monitor.c`) calls `system_state_set_sensors()` each time it processes an ADC or temperature reading, pushing all five sensor values in a single critical section.
5. **Consumers** (not yet implemented) will call `system_state_get(&snap)` to obtain a full copy. Because the read side is a single `memcpy` inside a critical section, consumers are guaranteed a self-consistent snapshot -- they will never see, for example, a half-updated sensor set.

## Architecture Decisions

- **Spinlock over mutex.** The critical sections contain only a single field assignment or a `memcpy` of ~28 bytes. A FreeRTOS spinlock (`portENTER_CRITICAL` / `portEXIT_CRITICAL`) is the lightest-weight synchronisation primitive on ESP32 and is safe to call from ISR context, which is necessary because PTT publishes from a GPIO ISR.

- **Opaque sequencer types (`uint8_t` instead of enums).** `seq_state` and `seq_fault` are stored as raw `uint8_t` to break a compile-time dependency on `sequencer.h`. This keeps the dependency graph shallow: any component can include `system_state.h` without pulling in the sequencer's types. Consumers that need symbolic names cast on read.

- **No init function.** The module uses file-scope static initialisation, so it is ready to use from the moment the application starts. This avoids boot-order issues -- producers can begin writing before any consumer has registered.

- **Copy-on-read, not pointer sharing.** `system_state_get()` copies the entire struct rather than returning a pointer. This eliminates data races outside the critical section and allows consumers to work on a stable snapshot for as long as they need without holding a lock.

- **Per-domain setters rather than a single bulk write.** Each subsystem updates only its own fields. This means producers do not need to know about fields owned by other subsystems and cannot accidentally overwrite them.

- **Relay ID bounds check.** `system_state_set_relay()` silently rejects relay IDs outside `[1, HW_RELAY_COUNT]`, matching the same 1-indexed validation pattern used in `relays.c`.

## Dependencies

| Dependency   | Purpose |
|--------------|---------|
| `hw_config`  | Provides `HW_RELAY_COUNT` for relay bitmask bounds checking. |
| FreeRTOS     | `portMUX_TYPE` spinlock primitives (`portENTER_CRITICAL` / `portEXIT_CRITICAL`). |
| `<string.h>` | `memcpy` for the snapshot copy in `system_state_get()`. |

No dependency on `sequencer.h`, `relays.h`, `monitor.h`, or `ptt.h` -- this is intentional to keep the component at the bottom of the dependency graph so it can be included freely by any subsystem.

## Usage Notes

- **ISR safety.** All setter functions use `portENTER_CRITICAL` (not `portENTER_CRITICAL_ISR`). On ESP-IDF for ESP32-S3, `portENTER_CRITICAL` and `portENTER_CRITICAL_ISR` compile to the same spinlock operation, so this is safe from ISR context. If porting to a platform where they differ, the PTT path would need the ISR variant.

- **Snapshot freshness.** A snapshot is only as fresh as the moment `system_state_get()` returns. If a consumer needs real-time values (e.g., a fast protection loop), it should re-read frequently rather than caching a snapshot.

- **Extending the struct.** To add new fields: add them to `system_state_t`, write a new domain-specific setter, and update the relevant producer. No changes to existing setters or the reader are needed -- `memcpy` picks up the new fields automatically.

- **SWR initialisation.** `swr` defaults to `1.0f` (not `0.0f`) so that a display rendering before the first sensor reading shows a physically meaningful value rather than zero.
