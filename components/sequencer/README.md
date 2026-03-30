# Sequencer

## Overview

The sequencer component implements the central TX/RX state machine for the 23cm RF PA controller. It owns the system's single event queue, which all interrupt-driven producers (PTT GPIO ISR, emergency button timer, monitor task, CLI, and web API) post events into. The `sequencer_task` is the sole consumer of this queue. Its job is to coordinate relay switching in the correct order and with the correct timing when the operator keys the transmitter, and to immediately shut down the PA and latch a fault state when a dangerous condition is detected (high SWR, over-temperature, or emergency button press).

The sequencer starts in the RX (idle) state at boot. A PTT assertion triggers a configurable multi-step relay sequence to transition into TX. A PTT release triggers the reverse sequence back to RX. Fault events from any state trigger an emergency shutdown -- the PA relay is de-energised first, then the full RX sequence runs, and the FSM latches in a FAULT state that ignores all further events until explicitly cleared.

## Key Data Structures

### States (`seq_state_t`)

| State | Meaning |
|---|---|
| `SEQ_STATE_RX` | Idle, receive path active. The only state that accepts PTT assertions or config updates. |
| `SEQ_STATE_SEQUENCING_TX` | Transient: relay steps are being executed to transition from RX to TX. |
| `SEQ_STATE_TX` | Transmit active, PA energised. Accepts PTT release and fault events. |
| `SEQ_STATE_SEQUENCING_RX` | Transient: relay steps are being executed to transition from TX to RX. |
| `SEQ_STATE_FAULT` | Latched fault. All events are silently dropped. Only `sequencer_clear_fault()` can exit this state. |

### Events (`seq_event_t`)

```c
typedef struct {
    seq_event_type_t type;
    uint32_t data;       // Carries seq_fault_t for FAULT events; unused for PTT events
} seq_event_t;
```

| Event Type | Producer(s) | Meaning |
|---|---|---|
| `SEQ_EVENT_PTT_ASSERT` | PTT ISR | PTT line went active (GPIO low) |
| `SEQ_EVENT_PTT_RELEASE` | PTT ISR | PTT line released (GPIO high) |
| `SEQ_EVENT_FAULT` | Monitor task, `sequencer_inject_fault()` | Sensor threshold breach; `data` field carries the specific `seq_fault_t` |
| `SEQ_EVENT_EMERGENCY_PA_OFF` | Button 1 debounce timer, `sequencer_inject_fault()` | Emergency PA off button pressed |
| `SEQ_EVENT_CONFIG_UPDATE` | `sequencer_update_config()` (via `config_apply()` callback chain) | New config staged; handled only in RX state via staging-area handshake |

### Fault Codes (`seq_fault_t`)

| Code | Trigger |
|---|---|
| `SEQ_FAULT_NONE` | No fault (initial/cleared state) |
| `SEQ_FAULT_HIGH_SWR` | SWR exceeds `swr_fault_threshold` (default 3.0) |
| `SEQ_FAULT_OVER_TEMP1` | Temperature sensor 1 exceeds `temp1_fault_threshold_c` (default 65 C) |
| `SEQ_FAULT_OVER_TEMP2` | Temperature sensor 2 exceeds `temp2_fault_threshold_c` (default 65 C) |
| `SEQ_FAULT_EMERGENCY` | Emergency button or explicit emergency injection |

### Relay Sequence Steps (`seq_step_t`)

Each TX and RX sequence is an ordered array of up to `SEQ_MAX_STEPS` (8) steps, stored in the `app_config_t` configuration blob:

```c
typedef struct {
    uint8_t  relay_id;   // 1-6, matching schematic labels
    uint8_t  state;      // 1 = energise, 0 = release
    uint16_t delay_ms;   // pause after this step before executing the next
} seq_step_t;
```

The configuration also identifies `pa_relay_id` (default: relay 2), which is the relay that emergency shutdown de-energises first, before running the full RX sequence.

### Module-level State

All state is file-scoped (`static`) within `sequencer.c`:

- `s_queue` -- FreeRTOS queue (depth 16) of `seq_event_t`, created once by `sequencer_init()`
- `s_state` -- current FSM state
- `s_fault` -- most recent fault code (or `SEQ_FAULT_NONE`)
- `s_cfg` -- local copy of `app_config_t`, snapshotted at init and updated only while in RX state via the config-update handshake

Config update staging area (written by caller, consumed by task):

- `s_cfg_pending` -- staged `app_config_t` written by `sequencer_update_config()`
- `s_cfg_pending_flag` -- volatile flag indicating a pending config is ready
- `s_cfg_ack_task` -- `TaskHandle_t` of the caller blocked waiting for acknowledgement

## Event Flow

### Normal TX/RX Cycle

```
PTT ISR (GPIO falling edge)
  |
  v
  xQueueSend(SEQ_EVENT_PTT_ASSERT)
  |
  v
sequencer_task receives event in SEQ_STATE_RX
  |
  +--> state = SEQ_STATE_SEQUENCING_TX
  |    system_state updated
  |
  +--> run_sequence(tx_steps)
  |      for each step:
  |        drain queue for urgent events (FAULT/EMERGENCY abort immediately)
  |        relay_set(relay_id, state)
  |        vTaskDelay(delay_ms)
  |
  +--> reconcile: re-read PTT GPIO to handle edges consumed mid-sequence
  |      if GPIO matches target state --> settle into TX or RX
  |      if mismatch --> run another sequence in the needed direction (loop)
  |
  +--> state = SEQ_STATE_TX
       system_state updated
```

The PTT release path (TX to RX) follows the same pattern with `SEQ_EVENT_PTT_RELEASE` and `rx_steps`.

### GPIO Reconciliation

After any sequence completes, the FSM reads the live PTT GPIO rather than trusting the event queue. `run_sequence()` drains the queue between relay steps, which may consume PTT edges. Without reconciliation the FSM could promote to a steady state with no matching event left to trigger the reverse transition -- the device would get stuck in TX or miss a TX request. The reconciliation loop runs one sequence per iteration in the direction needed to match the GPIO, re-checking after each, and is bounded by real time (each pass takes at least as long as the relay sequence delays).

### Fault / Emergency Shutdown

A fault event can arrive while the FSM is in any state (RX, TX, or mid-sequence):

```
Monitor / Button / sequencer_inject_fault()
  |
  v
  xQueueSend(SEQ_EVENT_FAULT or SEQ_EVENT_EMERGENCY_PA_OFF)
  |
  v
sequencer_task OR run_sequence() poll
  |
  +--> emergency_shutdown(fault_code):
  |      1. relay_set(pa_relay_id, false)   -- PA off immediately, no delay
  |      2. run_sequence(rx_steps)          -- restore all relays to safe state
  |      3. s_fault = fault_code
  |      4. s_state = SEQ_STATE_FAULT
  |      5. system_state updated
  |
  +--> All subsequent events silently dropped
  |
  v
  Only sequencer_clear_fault() can exit FAULT state:
    1. run_sequence(rx_steps)          -- restore relays using configured RX sequence
    2. s_fault = SEQ_FAULT_NONE
    3. s_state = SEQ_STATE_RX
    4. system_state updated
```

### Mid-Sequence Abort

`run_sequence()` polls the event queue (non-blocking `xQueueReceive`) before each relay step. If a FAULT or EMERGENCY event is found:

1. The event is pushed back onto the front of the queue via `xQueueSendToFront()`
2. `run_sequence()` returns `false`
3. The main task loop picks up the re-queued event and calls `emergency_shutdown()`

PTT events that arrive during a mid-sequence state are silently dropped to avoid re-entrant sequencing.

### Config Update Handshake

Config updates use a staging area with a synchronous acknowledgement handshake between the calling task (via `config_apply()` callback chain) and the sequencer task:

```
config_apply()
  |
  +--> calls sequencer_update_config(cfg)       [runs on caller's task]
  |      1. Reject if s_state != SEQ_STATE_RX   --> ESP_ERR_INVALID_STATE
  |      2. memcpy cfg into s_cfg_pending
  |      3. Set s_cfg_pending_flag = true
  |      4. Record caller's TaskHandle_t in s_cfg_ack_task
  |      5. Queue SEQ_EVENT_CONFIG_UPDATE
  |      6. Block on ulTaskNotifyTake (100 ms timeout)
  |
  v
sequencer_task receives SEQ_EVENT_CONFIG_UPDATE in SEQ_STATE_RX
  |      1. Check s_cfg_pending_flag
  |      2. memcpy s_cfg_pending into s_cfg (live config)
  |      3. Clear s_cfg_pending_flag
  |      4. xTaskNotifyGive(s_cfg_ack_task) --> unblocks caller
  |
  v
sequencer_update_config() returns ESP_OK to config_apply()
```

If the sequencer task does not ack within 100 ms (e.g., PTT raced in and the FSM left RX state), the caller gets `ESP_ERR_TIMEOUT` and the pending flag is cleared.

## Architecture Decisions

- **Single event queue, single consumer.** The sequencer owns the only event queue in the system. All producers (PTT ISR, button timer, monitor task, CLI commands, HTTP handlers) post `seq_event_t` messages to this queue. The `sequencer_task` is the sole consumer. This eliminates race conditions between subsystems -- all state transitions happen on one task's stack.

- **PA relay de-energised first in emergency.** `emergency_shutdown()` calls `relay_set(pa_relay_id, false)` before running the RX sequence. This ensures the PA is off within microseconds of fault detection, even if the RX sequence takes tens of milliseconds to complete. The PA relay ID is configurable (`pa_relay_id` in `app_config_t`).

- **Latching fault state requires explicit clear.** Once in `SEQ_STATE_FAULT`, all events are silently ignored. The operator must explicitly clear the fault via `sequencer_clear_fault()` (exposed through CLI `fault clear` and `POST /api/fault/clear`). This prevents automatic re-keying after a dangerous condition.

- **Config updates gated on RX state with synchronous ack.** `sequencer_update_config()` returns `ESP_ERR_INVALID_STATE` unless the FSM is in `SEQ_STATE_RX`. The staging area + task notification handshake ensures `config_apply()` does not return success until the sequencer task has actually committed the new config into its live copy. This makes `config_pending_apply()` reliable -- it can compare the draft against the last successfully applied config and know the answer is authoritative.

- **Parameterless init with internal config snapshot.** `sequencer_init()` takes no parameters; it calls `config_snapshot()` internally to obtain its initial `app_config_t` copy. This eliminates a class of caller errors (passing stale or partially-initialised config) and keeps the config ownership model clean.

- **Self-registration as config_apply callback.** `sequencer_init()` calls `config_register_apply_cb(sequencer_update_config)`, so the config component drives updates through the sequencer's own public API. This means the sequencer participates in the config component's all-or-nothing apply semantics: if the sequencer rejects an update (not in RX), the entire `config_apply()` fails and the draft remains pending.

- **Centralised fault injection.** `sequencer_inject_fault()` builds the correct event struct (`SEQ_EVENT_FAULT` or `SEQ_EVENT_EMERGENCY_PA_OFF` depending on fault type) and sends it to the queue. CLI and web handlers call this single function rather than duplicating event construction logic.

- **Fault clear uses configured RX sequence, not relays_all_off().** `sequencer_clear_fault()` runs `run_sequence(s_cfg.rx_steps, ...)` rather than calling `relays_all_off()`. A user-configured RX sequence may intentionally leave some relays energised, so a blanket all-off would be incorrect.

- **Local config copy.** The sequencer stores its own `memcpy` of `app_config_t` rather than holding a pointer to shared config. This decouples it from the CLI's edit/apply/save workflow and ensures the active sequence data cannot change mid-execution.

- **Queue depth of 16.** The queue is generously sized relative to the event rate. PTT transitions are human-speed (tens of milliseconds minimum). Monitor faults arrive at most once per ~500 ms sensor cycle. Overflow would only occur if multiple subsystems simultaneously flooded events, which the architecture prevents.

- **Transient sequencing states are observable.** `SEQ_STATE_SEQUENCING_TX` and `SEQ_STATE_SEQUENCING_RX` are published to `system_state` so that the display and web UI can show that a transition is in progress. These states only last for the duration of the relay step sequence (typically a few tens of milliseconds).

- **No mutex on `s_state` / `s_fault` reads.** The getter functions (`sequencer_get_state()`, `sequencer_get_fault()`) read `s_state` and `s_fault` directly without synchronization. These are single-byte values on a 32-bit architecture, so reads are atomic. Consumers needing a consistent pair should use `system_state_get()` instead.

## Dependencies

| Dependency | Role |
|---|---|
| `config` | Provides `app_config_t`, `seq_step_t`, `SEQ_MAX_STEPS`, `config_snapshot()`, and `config_register_apply_cb()`. The sequencer consumes but never modifies configuration. |
| `relays` | `relay_set()` to switch individual relays. Relay IDs are 1-indexed (1-6). |
| `system_state` | `system_state_set_sequencer()` publishes FSM state and fault code to the shared blackboard for display/CLI/web consumers. |
| `hw_config` | Provides `HW_PTT_GPIO` for the PTT reconciliation GPIO read. |
| `esp_timer` | Listed as a CMake dependency (required by FreeRTOS timing primitives used internally). |
| FreeRTOS | `xQueueCreate`, `xQueueReceive`, `xQueueSendToFront`, `vTaskDelay`, `xTaskNotifyGive`, `ulTaskNotifyTake` for the event-driven task loop, sequenced relay timing, and config update handshake. |

### Event Producers (not compile-time dependencies, but runtime collaborators)

| Producer | Events Sent |
|---|---|
| `ptt` (GPIO ISR) | `SEQ_EVENT_PTT_ASSERT`, `SEQ_EVENT_PTT_RELEASE` |
| `buttons` (Button 1 debounce timer) | `SEQ_EVENT_EMERGENCY_PA_OFF` |
| `monitor` (ADC sensor task) | `SEQ_EVENT_FAULT` with SWR / temperature fault codes |
| `cli` / `web_server` | Via `sequencer_inject_fault()` for fault/emergency injection |
| `config` (via `config_apply()`) | `SEQ_EVENT_CONFIG_UPDATE` via registered `sequencer_update_config` callback |

## Public API Summary

| Function | Purpose |
|---|---|
| `sequencer_init()` | Create event queue, snapshot config, register as config_apply callback. Parameterless. |
| `sequencer_task(void *arg)` | FreeRTOS task entry point (priority 10, stack 4096). |
| `sequencer_get_event_queue()` | Return queue handle for producers. Valid after init. |
| `sequencer_get_state()` | Read current FSM state (atomic, no lock). |
| `sequencer_get_fault()` | Read current fault code (atomic, no lock). |
| `sequencer_clear_fault()` | Clear latched fault, run RX sequence, return to RX. |
| `sequencer_update_config(cfg)` | Stage new config with synchronous ack handshake. RX-only. |
| `sequencer_inject_fault(fault)` | Queue a fault event. Maps `SEQ_FAULT_EMERGENCY` to `SEQ_EVENT_EMERGENCY_PA_OFF`. |
| `seq_state_name(state)` | State enum to display string. |
| `seq_fault_name(fault)` | Fault enum to display string. |
| `seq_fault_parse(str, out)` | Parse fault keyword string to enum. |

## Usage Notes

- **Initialization order matters.** `sequencer_init()` must be called after `config_init()` (it calls `config_snapshot()` and `config_register_apply_cb()`) and before `ptt_init()`, `buttons_init()`, or `monitor_init()` because those components call `sequencer_get_event_queue()` during their initialization to obtain the queue handle.

- **Task priority.** The sequencer task runs at priority 10 (highest among application tasks; monitor runs at 7). This ensures relay sequencing is not preempted by sensor reads.

- **Recommended task creation:**
  ```c
  xTaskCreate(sequencer_task, "sequencer", 4096, NULL, 10, NULL);
  ```

- **Injecting faults from application code:**
  ```c
  // Preferred: use the centralised injection function
  sequencer_inject_fault(SEQ_FAULT_HIGH_SWR);

  // For emergency faults, it automatically uses SEQ_EVENT_EMERGENCY_PA_OFF
  sequencer_inject_fault(SEQ_FAULT_EMERGENCY);
  ```

- **Clearing faults from application code:**
  ```c
  if (sequencer_get_state() == SEQ_STATE_FAULT) {
      sequencer_clear_fault();  // returns ESP_OK or ESP_ERR_INVALID_STATE
  }
  ```

- **Config updates are driven by config_apply(), not called directly.** Because `sequencer_update_config()` is registered as a `config_apply_cb_t`, callers should use `config_apply()` rather than calling `sequencer_update_config()` directly. The config component orchestrates the all-or-nothing apply across all registered consumers.

- **Re-init guard.** Calling `sequencer_init()` a second time returns `ESP_ERR_INVALID_STATE`. The component is designed to be initialized exactly once.

- **Relay step timing.** The `delay_ms` in each `seq_step_t` is implemented with `vTaskDelay`, so actual timing granularity is limited to the FreeRTOS tick period (typically 1 ms on ESP32-S3). During these delays the task yields, allowing lower-priority tasks to run.
