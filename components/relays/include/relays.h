#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================
 * relays.h — GPIO relay driver
 * Relay IDs are 1-indexed (1–6) matching schematic labels.
 * ========================================================= */

/**
 * Initialise all relay GPIOs as outputs and drive them LOW (relays off).
 * Must be called once before relay_set() or relays_all_off().
 */
esp_err_t relays_init(void);

/**
 * Set a single relay on or off.
 * relay_id: 1–6
 * on:       true = relay energised, false = relay released
 * Returns ESP_ERR_INVALID_ARG if relay_id is out of range.
 */
esp_err_t relay_set(uint8_t relay_id, bool on);

/**
 * De-energise all relays immediately (safe state).
 */
void relays_all_off(void);

#ifdef __cplusplus
}
#endif
