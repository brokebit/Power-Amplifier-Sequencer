# Sequencer

## Overview

The sequencer component is the central state machine for an RF power amplifier TX/RX relay controller running on ESP-IDF / FreeRTOS. It owns a single event queue that serves as the system's coordination point: ISRs (PTT, emergency button) and background tasks (monitor) post events to the queue, while a dedicated `sequencer_task` is the sole consumer. The sequencer orchestrates relay switching in a deterministic, timed sequence to safely transition between receive and transmit states, and enforces a latching fault mode that requires explicit external clearance.

This component also hosts the `system_state` module -- a shared blackboard that aggregates observable state from all subsystems for display and logging consumers.

## Key Data Structures

### `seq_event_t` -- Event Message

Every interaction with the sequencer passes through this structure, sent via a FreeRTOS queue (depth 16).

| Field  | Type               | Description |
|--------|--------------------|-------------|
| `type` | `seq_event_type_t` | Discriminator: `PTT_ASSERT`, `PTT_RELEASE`, `FAULT`, `EMERGENCY_PA_OFF` |
| `data` | `uint32_t`         | Payload -- carries the `seq_fault_t` code when `type == FAULT`; unused otherwise |

### `seq_state_t` -- State Machine States

| State              | Meaning |
|--------------------|---------|
| `SEQ_STATE_RX`            | Idle. Receive path active, safe state. |
| `SEQ_STATE_SEQUENCING_TX` | Transient. Executing the TX relay step list. |
| `SEQ_STATE_TX`            | Transmit path active, PA energised. |
| `SEQ_STATE_SEQUENCING_RX` | Transient. Executing the RX relay step list. |
| `SEQ_STATE_FAULT`         | Latched. All events ignored until `sequencer_clear_fault()` is called. |

### `seq_fault_t` -- Fault Codes

| Code                  | Value | Trigger |
|-----------------------|-------|---------|
| `SEQ_FAULT_NONE`      | 0     | No fault (cleared state) |
| `SEQ_FAULT_HIGH_SWR`  | 1     | SWR exceeded threshold |
| `SEQ_FAULT_OVER_TEMP1`| 2     | Temperature sensor 1 over limit |
| `SEQ_FAULT_OVER_TEMP2`| 3     | Temperature sensor 2 over limit |
| `SEQ_FAULT_EMERGENCY` | 4     | Physical emergency button pressed |

### `system_state_t` -- Observable System Snapshot

Defined in `system_state.h`. Aggregates live state from all subsystems into a single struct for read-only consumers (display, console logger). See `components/system_state/README.md` for full details.

| Field | Type | Source |
|---|---|---|
| `relay_states` | `uint8_t` (bitmask) | Relay component |
| `ptt_active` | `bool` | PTT ISR |
| `seq_state` | `seq_state_t` | Sequencer task |
| `seq_fault` | `seq_fault_t` | Sequencer task |
| `fwd_power_w`, `ref_power_w`, `swr` | `float` | Monitor task |
| `temp1_c`, `temp2_c` | `float` | Monitor task |

### `seq_step_t` / `app_config_t` (from `config` component)

Relay sequences are defined as arrays of `seq_step_t` (up to `SEQ_MAX_STEPS = 8` per direction). Each step specifies a `relay_id` (1--6), a boolean `state`, and a `delay_ms` to pause before the next step. The full TX and RX step arrays live inside `app_config_t`, which is copied into the sequencer at init time.

### Module-Level Static State

| Variable   | Type            | Description |
|------------|-----------------|-------------|
| `s_queue`  | `QueueHandle_t` | The central event queue |
| `s_state`  | `seq_state_t`   | Current state machine state |
| `s_fault`  | `seq_fault_t`   | Last latched fault code |
| `s_cfg`    | `app_config_t`  | Snapshot of configuration taken at init |

## Event Flow

### Normal TX/RX Cycle

```
PTT ISR                     sequencer_task              system_state
  |                              |                           |
  |--- PTT_ASSERT ------------->|                           |
  |                              |---> state = SEQUENCING_TX |
  |                              |     set_sequencer() ----->|
  |                              |     run_sequence(tx_steps)|
  |                              |       relay_set() + delay |
  |                              |---> state = TX            |
  |                              |     set_sequencer() ----->|
  |                              |                           |
  |--- PTT_RELEASE ------------>|                           |
  |                              |---> state = SEQUENCING_RX |
  |                              |     set_sequencer() ----->|
  |                              |     run_sequence(rx_steps)|
  |                              |       relay_set() + delay |
  |                              |---> state = RX            |
  |                              |     set_sequencer() ----->|
```

The sequencer publishes to the `system_state` blackboard on every state transition, including transient sequencing states. This gives consumers full FSM visibility.

### Fault During Sequence

`run_sequence()` polls the event queue (non-blocking) before every relay step. If a `FAULT` or `EMERGENCY_PA_OFF` event is found mid-sequence:

1. The urgent event is pushed back to the front of the queue via `xQueueSendToFront`.
2. `run_sequence()` returns `false`.
3. The task loop reads the re-queued event and enters the `SEQUENCING_TX` / `SEQUENCING_RX` case, which calls `emergency_shutdown()`.

PTT events that arrive during a sequence are silently dropped -- the sequencer does not allow re-entrant transitions.

### Emergency Shutdown Path

```
emergency_shutdown(fault_code):
  1. relay_set(2, false)           -- PA relay off immediately (hardcoded relay 2)
  2. run_sequence(rx_steps)        -- restore all relays to RX-safe positions
  3. s_fault = fault_code
  4. s_state = FAULT               -- latched; all events ignored
  5. system_state_set_sequencer()  -- publish fault state to blackboard
```

### Runtime Config Update

`sequencer_update_config()` allows hot-swapping the relay sequences and thresholds without a restart:

1. Verifies the current state is `SEQ_STATE_RX` (returns `ESP_ERR_INVALID_STATE` otherwise).
2. Overwrites the module-static `s_cfg` with a full `memcpy` of the new `app_config_t`.
3. Logs the new TX/RX step counts for traceability.

The state guard ensures that relay sequences are never modified while a transition is in progress. Callers should check the return value -- if the sequencer is mid-sequence, in TX, or faulted, the update is silently rejected.

### Fault Clearance

`sequencer_clear_fault()` can be called from any task context. It:

1. Verifies the current state is `SEQ_STATE_FAULT` (returns `ESP_ERR_INVALID_STATE` otherwise).
2. Calls `relays_all_off()` to ensure a known-safe relay state.
3. Resets `s_fault` to `NONE` and `s_state` to `RX`.
4. Publishes the cleared state to `system_state`.

## Architecture Decisions

- **Single-consumer queue pattern.** All event sources (ISRs, monitor task) post to one queue; only `sequencer_task` reads from it. This eliminates the need for mutexes around relay control and state transitions, since all mutation happens on a single thread.

- **Mid-sequence abort via queue peek-ahead.** Rather than using a separate abort flag or semaphore, `run_sequence()` drains the queue between relay steps. Urgent events are pushed back to the front so the main loop's `switch` handles the state transition. This keeps fault-handling logic in one place (the main event loop) rather than duplicating it inside the sequence runner.

- **Hardcoded PA relay ID (relay 2) in emergency shutdown.** The PA relay is turned off with a direct `relay_set(2, false)` call before running the full RX sequence. This is a deliberate safety choice: even if the RX sequence configuration is malformed, the PA is guaranteed to be de-energised first. The trade-off is that changing the PA relay assignment requires a code change, not just a config change.

- **Configuration snapshot, hot-swappable in RX.** `sequencer_init` copies the entire `app_config_t` into a module-static variable. The config can be replaced at runtime via `sequencer_update_config()`, but only while the sequencer is in `SEQ_STATE_RX` -- the function returns `ESP_ERR_INVALID_STATE` in any other state. This prevents sequences from being swapped while a relay transition or fault is in progress, avoiding partial-sequence hazards.

- **Latching fault state.** Once in `SEQ_STATE_FAULT`, all incoming events are discarded. Recovery requires an explicit call to `sequencer_clear_fault()` from outside the sequencer (e.g., a UI button handler). This prevents automatic re-transmission after a fault condition.

- **Dual state-read paths.** `sequencer_get_state()` and `sequencer_get_fault()` are direct reads of static variables (safe on ESP32 for 32-bit aligned reads). For cross-component consumers that need a consistent multi-field snapshot (state + fault + relays + sensors), the preferred path is `system_state_get()` which provides spinlock-protected atomic copies.

- **Publish on every transition, not just stable states.** The sequencer publishes to `system_state` on transient states (SEQUENCING_TX, SEQUENCING_RX) as well as stable ones. This gives display and logging consumers full visibility into the FSM progression.

## Dependencies

| Dependency  | Role |
|-------------|------|
| `config`    | Provides `app_config_t` and `seq_step_t` -- relay sequence definitions and fault thresholds |
| `relays`    | GPIO driver for relay control (`relay_set`, `relays_all_off`) |
| `system_state` | Shared blackboard for publishing FSM state and fault code to system-wide consumers |
| `esp_timer` | Listed as a build dependency in CMakeLists.txt (likely for transitive FreeRTOS timing support) |
| FreeRTOS    | Queue, task, and delay primitives (`xQueueCreate`, `xQueueReceive`, `vTaskDelay`) |
| `esp_log`   | Logging via `ESP_LOGx` macros |

## Usage Notes

- **Init order matters.** Call `relays_init()` and `config_init()` before `sequencer_init()`. The sequencer assumes relays are already configured and the config struct is fully populated. `system_state` requires no explicit init (static initialization).

- **Task creation.** Create the task at priority 10 with a 4096-byte stack: `xTaskCreate(sequencer_task, "sequencer", 4096, NULL, 10, NULL)`. The `arg` parameter is unused.

- **Posting events from ISRs.** Producers should use `xQueueSendFromISR()` (not `xQueueSend()`) when posting from interrupt context. Obtain the queue handle via `sequencer_get_event_queue()`.

- **Relay 2 is special.** Emergency shutdown hardcodes relay 2 as the PA relay for immediate de-energisation. If the hardware mapping changes, `emergency_shutdown()` must be updated.

- **Fault recovery requires external action.** The sequencer will not self-recover from a fault. The caller (UI, serial console, etc.) must invoke `sequencer_clear_fault()` after the fault condition is resolved.

- **Runtime config updates.** Call `sequencer_update_config()` to change relay sequences or fault thresholds without a restart. The function only succeeds in `SEQ_STATE_RX` -- check the return value. A typical pattern is to verify `sequencer_get_state() == SEQ_STATE_RX` before calling, or simply call and handle the `ESP_ERR_INVALID_STATE` return. The update is a full `memcpy` replacement, so the caller must provide a complete, valid `app_config_t`.

- **Reading sequencer state.** For single-field reads from within the sequencer's own logic, use `sequencer_get_state()` / `sequencer_get_fault()`. For cross-component consumers (display, logger) that need a consistent snapshot of multiple fields, prefer `system_state_get()`.
