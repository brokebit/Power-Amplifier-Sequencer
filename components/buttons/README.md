# Buttons

## Overview

The `buttons` component is a debounced GPIO button driver for ESP-IDF. It manages six physical buttons: BTN1 is hardwired to emit the `SEQ_EVENT_EMERGENCY_PA_OFF` event into the sequencer's FreeRTOS queue, while BTN2 through BTN6 are spare inputs that accept user-registered callbacks. Debouncing is handled entirely in hardware-interrupt + software-timer cooperation, keeping ISR work minimal and deferring all logic to the `esp_timer` task context.

## Key Data Structures

### `button_ctx_t` (internal, per-button state)

| Field   | Type                  | Description                                                      |
|---------|-----------------------|------------------------------------------------------------------|
| `gpio`  | `int`                 | GPIO pin number for this button                                  |
| `id`    | `uint8_t`             | 1-indexed button identifier (1 through 6)                        |
| `timer` | `esp_timer_handle_t`  | Handle to the one-shot debounce timer                            |
| `cb`    | `button_cb_t`         | Optional press callback; always `NULL` for BTN1                  |

A static array `s_btns[HW_BUTTON_COUNT]` (size 6) holds one context struct per button. The GPIO-to-index mapping comes from `HW_BUTTON_GPIOS` defined in `hw_config.h`.

### `button_cb_t` (public callback type)

```c
typedef void (*button_cb_t)(uint8_t button_id);
```

Invoked from the `esp_timer` task context (not from an ISR), so it is safe to call most ESP-IDF APIs inside the callback.

### GPIO Pin Assignments

| Button | GPIO | Role               |
|--------|------|--------------------|
| BTN1   | 4    | Emergency PA Off   |
| BTN2   | 5    | Spare              |
| BTN3   | 6    | Spare              |
| BTN4   | 7    | Spare              |
| BTN5   | 48   | Spare              |
| BTN6   | 47   | Spare              |

All GPIOs are configured as inputs with internal pull-ups enabled (active-low buttons).

## Event Flow

The debounce mechanism follows a two-stage pattern that prevents both bounce noise and ISR-context limitations:

```
Physical press (falling edge)
        |
        v
  btn_isr_handler()  [IRAM, ISR context]
    1. Disable this GPIO's interrupt
    2. Start (or restart) a 50 ms one-shot esp_timer
        |
        v
  debounce_timer_cb()  [esp_timer task context]
    3. Re-read GPIO level
    4a. If GPIO is HIGH -> spurious press, skip action
    4b. If GPIO is LOW  -> confirmed press:
        - BTN1: enqueue SEQ_EVENT_EMERGENCY_PA_OFF to sequencer
        - BTN2-6: invoke registered callback (if any)
    5. Re-enable GPIO interrupt for the next press
```

Key details:

- The ISR disables its own GPIO interrupt before starting the timer, so bounce edges during the 50 ms window are ignored entirely rather than queuing up repeated timer restarts.
- The timer callback re-reads the GPIO, providing a second confirmation that the button is genuinely held low and not a transient glitch.
- BTN1 sends a `seq_event_t` with `.type = SEQ_EVENT_EMERGENCY_PA_OFF` and `.data = 0` to the sequencer's FreeRTOS queue via `xQueueSend` with zero timeout (non-blocking).
- Spare button callbacks are invoked synchronously within the `esp_timer` task, so long-running work in a callback will block other esp_timer operations.

## Architecture Decisions

- **ISR + one-shot timer debounce**: The ISR does the absolute minimum (disable interrupt, start timer) to stay fast and IRAM-safe. All decision logic runs in the timer callback at task level, where logging and queue operations are safe.

- **BTN1 hardcoded to sequencer queue**: Rather than using the callback mechanism, BTN1 directly posts to the sequencer event queue. This is deliberate for a safety-critical path -- the emergency PA off does not depend on any external code registering a callback, so it cannot be accidentally left unconnected.

- **Interrupt disable/re-enable as the lock**: Instead of using a software flag or semaphore, the component disables the GPIO interrupt itself to prevent re-entrant debounce. This is simpler and avoids any shared-state concurrency issues between the ISR and the timer callback.

- **Tolerant ISR service installation**: `gpio_install_isr_service()` is called defensively -- if it returns `ESP_ERR_INVALID_STATE` (already installed), the error is silently accepted. This allows the component to work regardless of initialization order with other GPIO-interrupt users like the PTT component.

- **Static allocation**: All button contexts are statically allocated in a module-scoped array. No heap allocation occurs at runtime, which is appropriate for a safety-related peripheral driver.

## Dependencies

| Dependency    | Role                                                                 |
|---------------|----------------------------------------------------------------------|
| `hw_config`   | Provides `HW_BUTTON_COUNT`, `HW_BUTTON_GPIOS`, and per-button GPIO defines |
| `sequencer`   | Provides `seq_event_t`, `SEQ_EVENT_EMERGENCY_PA_OFF`, and `sequencer_get_event_queue()` |
| `driver`      | ESP-IDF GPIO driver (`driver/gpio.h`)                                |
| `esp_timer`   | ESP-IDF high-resolution timer for debounce one-shots                 |

## Usage Notes

- **Initialization order**: Call `buttons_init()` after `sequencer_init()` so the event queue exists when BTN1 is pressed. The GPIO ISR service will be installed automatically if not already present.

- **Callback context**: Spare-button callbacks run in the `esp_timer` task, not in an ISR. They are safe for most operations but should avoid blocking for extended periods, as this would delay all other esp_timer callbacks system-wide.

- **BTN1 is not configurable**: Attempting to register a callback for `button_id == 1` returns `ESP_ERR_INVALID_ARG`. The emergency PA off behavior is intentionally not overridable.

- **No release detection**: The component only detects presses (falling edges). It does not report button releases or support long-press / double-press patterns.

- **Debounce period**: Fixed at 50 ms via `BUTTONS_DEBOUNCE_MS`. Changing this value requires recompilation.
