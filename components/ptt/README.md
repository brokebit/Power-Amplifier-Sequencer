# ptt

## Overview

The `ptt` component is a GPIO interrupt driver that monitors a Push-To-Talk (PTT) input line on GPIO 13. It detects both press and release edges and forwards them as events to the sequencer's FreeRTOS queue. The component is entirely interrupt-driven with no task of its own -- it acts as a lightweight bridge between a hardware input and the sequencer state machine.

## Key Data Structures

### Hardware Configuration

| Item | Value | Source |
|------|-------|--------|
| GPIO pin | 13 | `HW_PTT_GPIO` from `hw_config.h` |
| Active level | Low (0) | Internal pull-up enabled; external switch pulls to ground |
| Interrupt trigger | Both edges (`GPIO_INTR_ANYEDGE`) | Captures both assert and release |

### Events Produced

The ISR constructs a `seq_event_t` (defined in `sequencer.h`) and sends it to the sequencer queue:

```c
typedef struct {
    seq_event_type_t type;
    int              data;   /* always 0 for PTT events */
} seq_event_t;
```

| GPIO Level | Event Type | Meaning |
|------------|-----------|---------|
| 0 (low) | `SEQ_EVENT_PTT_ASSERT` | PTT line went active -- switch pressed |
| 1 (high) | `SEQ_EVENT_PTT_RELEASE` | PTT line released -- switch open |

## Event Flow

1. An external switch pulls GPIO 13 low (press) or releases it high (via internal pull-up).
2. The ESP-IDF GPIO ISR framework invokes `ptt_isr_handler` on the edge transition.
3. The handler reads the current GPIO level to determine assert vs. release (this handles both edges with a single ISR).
4. A `seq_event_t` is constructed and pushed onto the sequencer's event queue using `xQueueSendFromISR`.
5. `portYIELD_FROM_ISR` is called to context-switch immediately if the sequencer task was blocked waiting on the queue.

```
  GPIO 13 (HW)        ptt ISR              Sequencer Queue          Sequencer Task
  ────────────    ─────────────────    ──────────────────────    ───────────────────
  Falling edge ──> ptt_isr_handler() ──> SEQ_EVENT_PTT_ASSERT ──> handles assert
  Rising edge  ──> ptt_isr_handler() ──> SEQ_EVENT_PTT_RELEASE ──> handles release
```

## Architecture Decisions

- **ISR-only, no task**: The component has no dedicated FreeRTOS task. The ISR reads the GPIO level directly and posts an event in a single step. This keeps RAM and CPU overhead minimal for what is fundamentally a simple edge-detect-and-forward operation.

- **Level-read inside ISR instead of edge direction**: Rather than tracking which edge triggered the interrupt, the handler reads the actual GPIO level. This is more robust against electrical bounce -- if two edges fire in quick succession, the last ISR invocation will reflect the true settled state.

- **Tolerant ISR service installation**: `gpio_install_isr_service` is called during init but `ESP_ERR_INVALID_STATE` (already installed) is treated as success. This allows `ptt_init()` to be called regardless of whether another component has already installed the ISR service.

- **Startup state logging**: `ptt_init()` logs whether the PTT is currently active at boot time, providing immediate diagnostic visibility without requiring a state change.

- **Polling escape hatch**: `ptt_is_active()` provides a direct GPIO read for callers that need to check the current state synchronously (e.g., at startup before any edge has fired).

## Dependencies

| Dependency | Role |
|------------|------|
| `driver` (ESP-IDF) | GPIO configuration, ISR service, and interrupt handling |
| `hw_config` | Provides `HW_PTT_GPIO` pin assignment (GPIO 13) |
| `sequencer` | Provides `seq_event_t`, event type enums, and `sequencer_get_event_queue()` |

## Usage Notes

- **Init order matters**: `ptt_init()` must be called after `sequencer_init()` because it immediately uses `sequencer_get_event_queue()` to register the ISR. Calling it earlier will post events to an invalid queue handle.

- **No software debounce**: The component does not implement debounce. If the external PTT switch bounces, multiple assert/release events may arrive in rapid succession. The sequencer must be tolerant of this, or external hardware debounce should be provided.

- **Active-low convention**: The PTT line is active-low. A logic 0 means the PTT is asserted (pressed). This matches a typical normally-open switch to ground with an internal pull-up.
