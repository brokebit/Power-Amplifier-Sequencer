# Buttons

## Overview

The `buttons` component is a debounced GPIO button driver for the ESP32-S3 sequencer. It manages six physical buttons wired active-low with internal pull-ups. Button 1 is hard-wired to trigger an **Emergency PA Off** event on the sequencer's event queue -- a safety-critical function that bypasses any callback registration. Buttons 2 through 6 are spare inputs that accept optional user-registered callbacks.

The debounce mechanism is interrupt-driven: a falling-edge ISR starts a 50 ms one-shot `esp_timer`, and the timer callback re-reads the GPIO level to confirm the press before dispatching. This keeps ISR execution minimal (disable interrupt, start timer) while the confirmation and dispatch logic runs in the `esp_timer` task context.

## Key Data Structures

### `button_ctx_t` (internal)

Per-button state held in a static array (`s_btns[HW_BUTTON_COUNT]`):

| Field   | Type                 | Description                                                        |
|---------|----------------------|--------------------------------------------------------------------|
| `gpio`  | `int`                | GPIO number for this button, sourced from `HW_BUTTON_GPIOS`       |
| `id`    | `uint8_t`            | 1-indexed button identifier (1--6)                                 |
| `timer` | `esp_timer_handle_t` | One-shot debounce timer handle, unique per button                  |
| `cb`    | `button_cb_t`        | User-registered callback; always `NULL` for BTN1 (handled internally) |

### `button_cb_t` (public)

```c
typedef void (*button_cb_t)(uint8_t button_id);
```

Callback signature for spare buttons. The `button_id` argument is 1-indexed (values 2--6). Invoked from the `esp_timer` task context, not from an ISR -- so it is safe to call FreeRTOS API functions, log, or do moderate work inside the callback.

### GPIO Assignments (from `hw_config.h`)

| Button | GPIO | Purpose          |
|--------|------|------------------|
| BTN1   | 4    | Emergency PA Off |
| BTN2   | 5    | Spare            |
| BTN3   | 6    | Spare            |
| BTN4   | 7    | Spare            |
| BTN5   | 48   | Spare            |
| BTN6   | 47   | Spare            |

All GPIOs are configured as inputs with internal pull-ups enabled, triggering on falling edge (active-low).

## Event Flow

### Debounce and Dispatch Sequence

```
1. Physical button press pulls GPIO low (falling edge)
       |
2. btn_isr_handler (IRAM):
       - Disables this GPIO's interrupt (prevents retriggering)
       - Starts/restarts a 50 ms one-shot esp_timer
       |
3. debounce_timer_cb (esp_timer task context, 50 ms later):
       - Re-reads GPIO level
       - If GPIO is HIGH (released / spurious): re-arms interrupt, returns
       - If GPIO is still LOW (confirmed press):
            - BTN1: posts SEQ_EVENT_EMERGENCY_PA_OFF to sequencer queue
            - BTN2-6: invokes registered callback (if any)
       - Re-arms GPIO interrupt for next press
```

### BTN1 Emergency PA Off Path

BTN1 has a dedicated, non-overridable path. On confirmed press, it constructs a `seq_event_t` with type `SEQ_EVENT_EMERGENCY_PA_OFF` and sends it to the sequencer's event queue via `xQueueSend` with zero timeout (non-blocking). The sequencer FSM handles this event by transitioning to a safe state and de-energizing the PA relay. This path cannot be intercepted or replaced by registering a callback -- `button_register_cb()` rejects `button_id == 1` with `ESP_ERR_INVALID_ARG`.

## Architecture Decisions

- **ISR-to-timer debounce pattern**: The ISR does the absolute minimum (disable interrupt, start timer) to stay IRAM-safe and fast. All confirmation logic and dispatching runs in the `esp_timer` task context, which avoids ISR-context restrictions (no logging, no FreeRTOS queue calls from ISR, etc.).

- **Per-button timers instead of a single shared timer**: Each button has its own `esp_timer` instance. This avoids race conditions when multiple buttons are pressed simultaneously and removes the need for any locking or arbitration.

- **Interrupt disable/re-enable as the debounce gate**: Rather than using a boolean flag or timestamp comparison, the ISR disables its own GPIO interrupt and the timer callback re-enables it. This is a clean pattern that guarantees exactly one timer firing per edge, with no window for retriggering.

- **BTN1 hard-wired to emergency off**: The Emergency PA Off function is not exposed through the callback registration API. This is a deliberate safety decision -- the critical shutdown path cannot be accidentally overwritten or left unregistered.

- **1-indexed button IDs**: Button IDs (1--6) match physical labeling and schematic conventions. The internal array is 0-indexed; the conversion happens at the API boundary (`button_id - 1`).

- **Tolerant ISR service installation**: `buttons_init()` calls `gpio_install_isr_service()` but treats `ESP_ERR_INVALID_STATE` (already installed) as success. This allows flexible initialization ordering -- if `ptt_init()` has already installed the service, buttons gracefully reuses it.

## Dependencies

| Dependency   | Role                                                                 |
|--------------|----------------------------------------------------------------------|
| `driver`     | ESP-IDF GPIO driver (`gpio_config`, `gpio_isr_handler_add`, etc.)    |
| `esp_timer`  | One-shot timer for debounce confirmation                             |
| `hw_config`  | GPIO pin assignments (`HW_BUTTON_GPIOS`) and count (`HW_BUTTON_COUNT`) |
| `sequencer`  | Event queue access via `sequencer_get_event_queue()` and `seq_event_t` type |

## Public API

```c
esp_err_t buttons_init(void);
esp_err_t button_register_cb(uint8_t button_id, button_cb_t cb);
```

- `buttons_init()` -- Configures all button GPIOs, creates per-button debounce timers, and registers ISR handlers. Must be called **after** `sequencer_init()` (needs the event queue) and after the GPIO ISR service is installed (typically by `ptt_init()`).

- `button_register_cb(button_id, cb)` -- Registers a press callback for a spare button. Valid `button_id` values are 2--6. Returns `ESP_ERR_INVALID_ARG` for button 1 or IDs above `HW_BUTTON_COUNT`. Callbacks run in the `esp_timer` task context.

## Usage Notes

- **Initialization order matters**: The sequencer event queue must exist before `buttons_init()` is called. In `main.c`, the call order is: `sequencer_init()` -> `ptt_init()` -> `buttons_init()`.

- **Callbacks are not currently used**: As of the current codebase, `button_register_cb()` is defined but never called from application code. Buttons 2--6 are wired but inert until callbacks are registered.

- **No release detection**: The driver only detects button presses (falling edge). It does not track button release, long-press, or repeat events. If those are needed, the debounce timer callback would need to be extended.

- **Thread safety of callback registration**: `button_register_cb()` performs a simple pointer assignment with no locking. It is safe to call from any task before or during operation, but do not call it concurrently from multiple tasks for the same button ID.

- **Debounce period**: The `BUTTONS_DEBOUNCE_MS` constant is defined in `buttons.h` as 50 ms. Changing it affects all buttons uniformly.
