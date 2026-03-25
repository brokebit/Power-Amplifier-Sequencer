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
| `SEQ_EVENT_PTT_ASSERT` | PTT ISR | PTT line went active (GPIO 13 low) |
| `SEQ_EVENT_PTT_RELEASE` | PTT ISR | PTT line released (GPIO 13 high) |
| `SEQ_EVENT_FAULT` | Monitor task, CLI `fault inject`, Web API `POST /api/fault/inject` | Sensor threshold breach; `data` field carries the specific `seq_fault_t` |
| `SEQ_EVENT_EMERGENCY_PA_OFF` | Button 1 debounce timer, CLI, Web API | Emergency PA off button pressed |

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
- `s_cfg` -- local copy of `app_config_t`, updated only while in RX state via `sequencer_update_config()`

## Event Flow

### Normal TX/RX Cycle

```
PTT ISR (GPIO 13 falling edge)
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
  +--> state = SEQ_STATE_TX
       system_state updated

PTT ISR (GPIO 13 rising edge)
  |
  v
  xQueueSend(SEQ_EVENT_PTT_RELEASE)
  |
  v
sequencer_task receives event in SEQ_STATE_TX
  |
  +--> state = SEQ_STATE_SEQUENCING_RX
  |    system_state updated
  |
  +--> run_sequence(rx_steps)
  |
  +--> state = SEQ_STATE_RX
       system_state updated
```

### Fault / Emergency Shutdown

A fault event can arrive while the FSM is in any state (RX, TX, or mid-sequence):

```
Monitor / Button / CLI / Web API
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
    1. relays_all_off()
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

## Architecture Decisions

- **Single event queue, single consumer.** The sequencer owns the only event queue in the system. All producers (PTT ISR, button timer, monitor task, CLI commands, HTTP handlers) post `seq_event_t` messages to this queue. The `sequencer_task` is the sole consumer. This eliminates race conditions between subsystems -- all state transitions happen on one task's stack.

- **PA relay de-energised first in emergency.** `emergency_shutdown()` calls `relay_set(pa_relay_id, false)` before running the RX sequence. This ensures the PA is off within microseconds of fault detection, even if the RX sequence takes tens of milliseconds to complete. The PA relay ID is configurable (`pa_relay_id` in `app_config_t`).

- **Latching fault state requires explicit clear.** Once in `SEQ_STATE_FAULT`, all events are silently ignored. The operator must explicitly clear the fault via `sequencer_clear_fault()` (exposed through CLI `fault clear` and `POST /api/fault/clear`). This prevents automatic re-keying after a dangerous condition.

- **Config updates gated on RX state.** `sequencer_update_config()` returns `ESP_ERR_INVALID_STATE` unless the FSM is in `SEQ_STATE_RX`. This avoids changing relay sequences mid-transmission.

- **Local config copy.** The sequencer stores its own `memcpy` of `app_config_t` rather than holding a pointer to shared config. This decouples it from the CLI's edit/apply/save workflow and ensures the active sequence data cannot change mid-execution.

- **Queue depth of 16.** The queue is generously sized relative to the event rate. PTT transitions are human-speed (tens of milliseconds minimum). Monitor faults arrive at most once per ~500 ms sensor cycle. Overflow would only occur if multiple subsystems simultaneously flooded events, which the architecture prevents.

- **Transient sequencing states are observable.** `SEQ_STATE_SEQUENCING_TX` and `SEQ_STATE_SEQUENCING_RX` are published to `system_state` so that the display and web UI can show that a transition is in progress. These states only last for the duration of the relay step sequence (typically a few tens of milliseconds).

- **No mutex on `s_state` / `s_fault` reads.** The getter functions (`sequencer_get_state()`, `sequencer_get_fault()`) read `s_state` and `s_fault` directly without synchronization. These are single-byte values on a 32-bit architecture, so reads are atomic. Consumers needing a consistent pair should use `system_state_get()` instead.

## Dependencies

| Dependency | Role |
|---|---|
| `config` | Provides `app_config_t`, `seq_step_t`, and `SEQ_MAX_STEPS`. The sequencer consumes but never modifies configuration. |
| `relays` | `relay_set()` to switch individual relays; `relays_all_off()` during fault clear. Relay IDs are 1-indexed (1-6). |
| `system_state` | `system_state_set_sequencer()` publishes FSM state and fault code to the shared blackboard for display/CLI/web consumers. |
| `esp_timer` | Listed as a CMake dependency (required by FreeRTOS timing primitives used internally). |
| FreeRTOS | `xQueueCreate`, `xQueueReceive`, `xQueueSendToFront`, `vTaskDelay` for the event-driven task loop and sequenced relay timing. |

### Event Producers (not compile-time dependencies, but runtime collaborators)

| Producer | Events Sent |
|---|---|
| `ptt` (GPIO 13 ISR) | `SEQ_EVENT_PTT_ASSERT`, `SEQ_EVENT_PTT_RELEASE` |
| `buttons` (Button 1 debounce timer) | `SEQ_EVENT_EMERGENCY_PA_OFF` |
| `monitor` (ADC sensor task) | `SEQ_EVENT_FAULT` with SWR / temperature fault codes |
| `cli` (`fault inject` command) | `SEQ_EVENT_FAULT` or `SEQ_EVENT_EMERGENCY_PA_OFF` |
| `web_server` (`POST /api/fault/inject`) | `SEQ_EVENT_FAULT` or `SEQ_EVENT_EMERGENCY_PA_OFF` |

## Usage Notes

- **Initialization order matters.** `sequencer_init()` must be called before `ptt_init()`, `buttons_init()`, or `monitor_init()` because those components call `sequencer_get_event_queue()` during their initialization to obtain the queue handle.

- **Task priority.** The sequencer task runs at priority 10 (highest among application tasks; monitor runs at 7). This ensures relay sequencing is not preempted by sensor reads.

- **Recommended task creation:**
  ```c
  xTaskCreate(sequencer_task, "sequencer", 4096, NULL, 10, NULL);
  ```

- **Clearing faults from application code:**
  ```c
  if (sequencer_get_state() == SEQ_STATE_FAULT) {
      sequencer_clear_fault();  // returns ESP_OK or ESP_ERR_INVALID_STATE
  }
  ```

- **Re-init guard.** Calling `sequencer_init()` a second time returns `ESP_ERR_INVALID_STATE`. The component is designed to be initialized exactly once.

- **Relay step timing.** The `delay_ms` in each `seq_step_t` is implemented with `vTaskDelay`, so actual timing granularity is limited to the FreeRTOS tick period (typically 1 ms on ESP32-S3). During these delays the task yields, allowing lower-priority tasks to run.
