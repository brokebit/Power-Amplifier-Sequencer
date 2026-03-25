#pragma once

#include <stdint.h>

#include "esp_err.h"

/* =========================================================
 * buttons.h — Debounced GPIO button driver
 *
 * BTN1 (GPIO 4): Emergency PA Off — wired directly to the
 *   sequencer event queue (SEQ_EVENT_EMERGENCY_PA_OFF).
 * BTN2–6 (GPIO 5,6,7,48,47): Spare — optional callbacks.
 *
 * Debounce: falling edge ISR starts a 50 ms one-shot esp_timer.
 * On expiry the GPIO level is re-read; if still low the press
 * is confirmed. The ISR is re-armed after the timer fires.
 *
 * Call buttons_init() after sequencer_init() and after the
 * GPIO ISR service has been installed (ptt_init installs it,
 * or call gpio_install_isr_service() yourself beforehand).
 * ========================================================= */

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
