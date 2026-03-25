#pragma once

#include <stdint.h>

#include "esp_err.h"

#include "hw_config.h"

/* =========================================================
 * config.h — NVS-backed runtime configuration
 * ========================================================= */

#ifdef __cplusplus
extern "C" {
#endif

#define SEQ_MAX_STEPS 8    /* Max relay steps per TX or RX sequence */
#define CFG_RELAY_NAME_LEN 16   /* Max relay name length including null terminator */

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

    /* Power meter calibration — P = cal_factor × V² */
    float fwd_power_cal_factor;      /* default: 1.0  */
    float ref_power_cal_factor;      /* default: 1.0  */

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
 * Initialise NVS and load config into *cfg.
 * If no saved config exists, writes defaults and returns them.
 * Must be called once before config_save() or config_get().
 */
esp_err_t config_init(app_config_t *cfg);

/**
 * Write *cfg to NVS.
 */
esp_err_t config_save(const app_config_t *cfg);

/**
 * Fill *cfg with factory defaults without touching NVS.
 */
void config_defaults(app_config_t *cfg);

/**
 * Format relay label into buf. Returns buf.
 * With name set: "R2/PA". Without: "R2".
 */
const char *config_relay_label(const app_config_t *cfg, uint8_t relay_id,
                               char *buf, size_t buf_len);

/**
 * Set a config field by key name (string) and value (string).
 * Handles type conversion and range validation.
 * On error, writes a message into err_msg (if non-NULL).
 *
 * Valid keys: swr_threshold, temp1_threshold, temp2_threshold,
 *             fwd_cal, ref_cal, therm_beta, therm_r0, therm_rseries,
 *             pa_relay
 */
esp_err_t config_set_by_key(app_config_t *cfg, const char *key,
                            const char *value_str, char *err_msg, size_t err_len);

#ifdef __cplusplus
}
#endif
