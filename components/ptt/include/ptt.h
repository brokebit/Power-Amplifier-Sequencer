#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================
 * ptt.h — PTT GPIO interrupt driver
 *
 * Monitors GPIO 13 (active low, internal pull-up).
 * Falling edge → SEQ_EVENT_PTT_ASSERT sent to sequencer queue.
 * Rising edge  → SEQ_EVENT_PTT_RELEASE sent to sequencer queue.
 *
 * Call ptt_init() after sequencer_init() so the event queue
 * handle is available.
 * ========================================================= */

/**
 * Configure GPIO 13 with an interrupt on both edges.
 * Installs the GPIO ISR service if not already installed.
 * Must be called after sequencer_init().
 */
esp_err_t ptt_init(void);

/**
 * Return the current PTT state (true = PTT asserted / active).
 * Reads the GPIO level directly — useful for startup state detection.
 */
bool ptt_is_active(void);

#ifdef __cplusplus
}
#endif
