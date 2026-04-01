#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "hw_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SEQ_MAX_STEPS 8           /* Max relay steps per TX or RX sequence */
#define SEQ_MAX_DELAY_MS 10000    /* Max delay between sequence steps (ms) */
#define CFG_RELAY_NAME_LEN 16     /* Max relay name length including null terminator */
#define CONFIG_MAX_APPLY_CBS 4    /* Max config_apply() callbacks */

/* NVS namespace and blob key */
#define CFG_NVS_NAMESPACE "seq_cfg"
#define CFG_NVS_KEY "app_cfg"

/* ---------------------------------------------------------
 * seq_step_t — one step in a TX or RX relay sequence
 * relay_id: 1–6 matching schematic labels
 * state:    true = relay ON, false = relay OFF
 * delay_ms: pause after this step before the next
 * --------------------------------------------------------- */
typedef struct {
    uint8_t relay_id;
    uint8_t state;       /* bool — packed as uint8 for NVS blob portability */
    uint16_t delay_ms;
} seq_step_t;

/* ---------------------------------------------------------
 * app_config_t — full runtime configuration blob
 * Stored as a single NVS blob under CFG_NVS_NAMESPACE/CFG_NVS_KEY.
 * --------------------------------------------------------- */
typedef struct {
    /* Relay sequences */
    seq_step_t tx_steps[SEQ_MAX_STEPS];
    uint8_t tx_num_steps;

    seq_step_t rx_steps[SEQ_MAX_STEPS];
    uint8_t rx_num_steps;

    /* Fault thresholds */
    float swr_fault_threshold;       /* default: 3.0  */
    float temp1_fault_threshold_c;   /* default: 65.0 */
    float temp2_fault_threshold_c;   /* default: 65.0 */

    /* PA relay — used by emergency_shutdown() to immediately de-energise the PA */
    uint8_t pa_relay_id;             /* default: 2 (relay IDs are 1-6) */

    /* Power meter calibration — log-linear detector model
     * dBm = (V_det_mV / slope) + intercept + atten + abs(coupling) */
    float fwd_slope_mv_per_db;       /* default: -25.0 */
    float fwd_intercept_dbm;         /* default: 0.0   */
    float fwd_coupling_db;           /* default: 0.0   */
    float fwd_attenuator_db;         /* default: 0.0   */

    float ref_slope_mv_per_db;       /* default: -25.0 */
    float ref_intercept_dbm;         /* default: 0.0   */
    float ref_coupling_db;           /* default: 0.0   */
    float ref_attenuator_db;         /* default: 0.0   */

    /* ADC input resistor divider — ratio = R_bottom / (R_top + R_bottom) */
    float adc_r_top_ohms;            /* default: 10000  */
    float adc_r_bottom_ohms;         /* default: 15000  */

    /* Thermistor (Steinhart-Hart beta model) */
    float thermistor_beta;           /* default: 3950   */
    float thermistor_r0_ohms;        /* default: 100000 (at 25°C) */
    float thermistor_r_series_ohms;  /* default: 100000 */

    /* Relay display names — empty string means no alias */
    char relay_names[HW_RELAY_COUNT][CFG_RELAY_NAME_LEN];
} app_config_t;

/* ---------------------------------------------------------
 * API
 * --------------------------------------------------------- */

/**
 * Initialise NVS and load config into the internal draft.
 * If no saved config exists, writes defaults.
 * Must be called once before any other config function.
 */
esp_err_t config_init(void);

/**
 * Write the draft config to NVS.
 */
esp_err_t config_save(void);

/**
 * Reset the draft to factory defaults without touching NVS.
 */
void config_defaults(void);

/**
 * Format relay label into buf. Returns buf.
 * With name set: "R2/PA". Without: "R2".
 */
const char *config_relay_label(const app_config_t *cfg, uint8_t relay_id,
                               char *buf, size_t buf_len);

/**
 * Acquire/release the config mutex.  All code that reads or writes the
 * shared app_config_t must hold this lock.  Functions in this module
 * (config_set_by_key, config_defaults, config_save) lock internally;
 * callers doing direct struct writes (sequence steps, relay names)
 * must lock/unlock explicitly.
 *
 * The mutex is created by config_init() and must not be used before that.
 */
void config_lock(void);
void config_unlock(void);

/**
 * Set a config field by key name (string) and value (string).
 * Handles type conversion and range validation.
 * On error, writes a message into err_msg (if non-NULL).
 * Acquires the config mutex internally.
 *
 * Valid keys: swr_threshold, temp1_threshold, temp2_threshold,
 *             fwd_cal, ref_cal, therm_beta, therm_r0, therm_rseries,
 *             pa_relay
 */
esp_err_t config_set_by_key(const char *key, const char *value_str,
                            char *err_msg, size_t err_len);

/* ---------------------------------------------------------
 * Snapshot & service functions (Phase 1 — additive)
 * --------------------------------------------------------- */

/**
 * Locked copy of the draft config.  All read paths should use this
 * rather than accessing the shared struct through a raw pointer.
 */
void config_snapshot(app_config_t *out);

/**
 * Set a relay display name.  Validates relay ID (1–HW_RELAY_COUNT) and
 * name length (< CFG_RELAY_NAME_LEN).  NULL or empty name clears the alias.
 * Locks internally.
 */
esp_err_t config_set_relay_name(uint8_t relay_id, const char *name,
                                char *err_msg, size_t err_len);

/**
 * Write a complete sequence (TX or RX).  Validates count (1–SEQ_MAX_STEPS)
 * and each step (relay ID 1–HW_RELAY_COUNT, state 0/1, delay 0–SEQ_MAX_DELAY_MS).
 * Locks internally.
 */
esp_err_t config_set_sequence(bool is_tx, const seq_step_t *steps, uint8_t count,
                              char *err_msg, size_t err_len);

/**
 * Callback invoked by config_apply().  Must not return ESP_OK until the
 * consumer has actually committed the config — this is what makes
 * config_pending_apply() reliable.
 *
 * Failable callbacks must be registered before infallible ones to
 * preserve all-or-nothing semantics.
 */
typedef esp_err_t (*config_apply_cb_t)(const app_config_t *cfg);

/**
 * Register a callback invoked by config_apply().  Callbacks run in
 * registration order; first failure stops the chain.  Max CONFIG_MAX_APPLY_CBS.
 * Idempotent — registering the same function pointer again is a no-op.
 * Returns ESP_ERR_NO_MEM on overflow, ESP_ERR_INVALID_ARG on NULL.
 */
esp_err_t config_register_apply_cb(config_apply_cb_t cb);

/**
 * Push the draft to all live consumers (sequencer, monitor).
 * Synchronous: does not return ESP_OK until every consumer has committed.
 * Returns the first callback error on failure; draft remains pending.
 */
esp_err_t config_apply(void);

/**
 * True when the draft config differs from the last successfully applied
 * config.  At boot, draft and last-applied are identical (returns false).
 */
bool config_pending_apply(void);

#ifdef __cplusplus
}
#endif
