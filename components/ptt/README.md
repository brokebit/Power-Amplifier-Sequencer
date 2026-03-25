# PTT (Push-to-Talk) Component

## Overview

The `ptt` component is an interrupt-driven GPIO driver that detects push-to-talk input for the RF PA sequencer. It monitors a single active-low GPIO pin (GPIO 13) and translates edge transitions into sequencer events, enabling the system to begin TX relay sequencing when the operator keys up and return to RX when the key is released. It is one of three event producers (alongside `buttons` and `monitor`) that feed the central sequencer event queue.

## Key Data Structures

The component is intentionally stateless -- it holds no internal variables beyond the ESP-IDF ISR registration. All state it produces is written to two external stores:

| Destination | What is written | How |
|---|---|---|
| **Sequencer event queue** | `seq_event_t` with type `SEQ_EVENT_PTT_ASSERT` or `SEQ_EVENT_PTT_RELEASE` | `xQueueSendFromISR()` to the queue returned by `sequencer_get_event_queue()` |
| **System state blackboard** | `ptt_active` boolean | `system_state_set_ptt(level == 0)` via spinlock-protected setter |

The `seq_event_t` struct sent to the sequencer:

```c
typedef struct {
    seq_event_type_t type;  // SEQ_EVENT_PTT_ASSERT or SEQ_EVENT_PTT_RELEASE
    uint32_t data;          // Always 0 for PTT events
} seq_event_t;
```

## Event Flow

```
Physical PTT line (active low, pulled high internally)
    |
    | GPIO edge detected (falling or rising)
    v
ptt_isr_handler()   [runs in ISR context, placed in IRAM]
    |
    |--- (1) Reads GPIO level
    |--- (2) Updates system_state blackboard (spinlock-safe from ISR)
    |--- (3) Constructs seq_event_t
    |--- (4) Sends event to sequencer queue via xQueueSendFromISR()
    |--- (5) Yields to higher-priority task if woken
    v
sequencer_task()    [sole consumer of the event queue]
    Drives relay sequencing FSM based on event type
```

**Startup path:**

1. `sequencer_init()` creates the event queue (must happen first).
2. `ptt_init()` configures GPIO 13, installs the ISR service, and registers the edge-triggered handler.
3. On init, `ptt_is_active()` is called to log whether the PTT line is currently asserted. This allows the startup log to reflect pre-existing PTT state (e.g., if the operator is already keyed when the board boots).

## API

| Function | Returns | Description |
|---|---|---|
| `ptt_init()` | `esp_err_t` | Configure GPIO 13, install ISR service (idempotent), register edge-triggered ISR handler. Must be called after `sequencer_init()`. |
| `ptt_is_active()` | `bool` | Synchronous GPIO level read. Returns `true` when the PTT line is low (asserted). Useful for polling outside the ISR path, e.g., startup state detection. |

## Architecture Decisions

- **ISR-direct, no debounce task.** The PTT line is expected to be a clean logic-level signal from a transceiver or keying interface, not a mechanical switch. There is no software debounce; edge transitions are forwarded to the sequencer immediately. This keeps latency to a minimum for RF sequencing where timing matters.

- **Dual-write on every edge.** The ISR writes to both the system state blackboard and the sequencer queue. These serve different consumers: the sequencer FSM reacts to the event, while display/CLI consumers read the blackboard snapshot. The blackboard write uses `portENTER_CRITICAL` / `portEXIT_CRITICAL`, which compile to spinlock operations that are safe from ISR context on the ESP32.

- **ISR service installation is idempotent.** `gpio_install_isr_service(0)` is called during init. If another component has already installed it, the `ESP_ERR_INVALID_STATE` return is treated as success. This means `ptt_init()` is responsible for installing the shared ISR service, and downstream components (notably `buttons`) rely on it already being present.

- **`IRAM_ATTR` on the ISR handler.** The handler is placed in IRAM so it can execute even when flash is being accessed (e.g., during OTA writes or NVS operations). This is standard practice for GPIO ISRs on ESP32.

- **Active-low convention.** The hardware uses an internal pull-up on GPIO 13 with an active-low PTT signal. The `ptt_is_active()` function and ISR both invert the GPIO level so callers work with positive logic (`true` = PTT asserted).

## Dependencies

| Dependency | Role |
|---|---|
| `driver` (ESP-IDF) | GPIO configuration, ISR service, interrupt handling |
| `hw_config` | Provides `HW_PTT_GPIO` (GPIO 13) pin definition |
| `sequencer` | Provides the event queue handle and `seq_event_t` / `seq_event_type_t` types |
| `system_state` | Provides `system_state_set_ptt()` for blackboard updates |

## Usage Notes

- **Initialization order matters.** `ptt_init()` must be called after `sequencer_init()` because it sends events to the sequencer's queue via `sequencer_get_event_queue()`. It must also be called before `buttons_init()`, which expects the GPIO ISR service to already be installed.

- **No runtime reconfiguration.** The GPIO pin is fixed at compile time via `HW_PTT_GPIO`. There is no mechanism to change the pin or disable/re-enable the interrupt at runtime.

- **No debounce protection.** If the PTT input is connected to a source that produces contact bounce (e.g., a mechanical switch without external filtering), the sequencer will receive rapid assert/release event pairs. The sequencer FSM should be robust to this, but adding hardware debounce (RC filter) on the input is recommended for noisy sources.

- **ISR service shared with buttons.** The call to `gpio_install_isr_service()` in `ptt_init()` establishes the shared ISR service for the entire application. If you reorder initialization to call `buttons_init()` before `ptt_init()`, you must ensure the ISR service is installed by some other means.
