#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUTTONS_DEBOUNCE_MS 50

/** Callback type for spare buttons. button_id is 1-indexed (2–6). */
typedef void (*button_cb_t)(uint8_t button_id);

/**
 * Initialise all button GPIOs and arm interrupts.
 * Must be called after sequencer_init().
 */
esp_err_t buttons_init(void);

/**
 * Register a callback for a spare button (button_id 2–6).
 * The callback is invoked from the esp_timer task context (not an ISR).
 * Returns ESP_ERR_INVALID_ARG if button_id is 1 or > 6.
 */
esp_err_t button_register_cb(uint8_t button_id, button_cb_t cb);

#ifdef __cplusplus
}
#endif
