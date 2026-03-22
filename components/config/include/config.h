#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================
 * config.h — NVS-backed runtime configuration
 * ========================================================= */

#define SEQ_MAX_STEPS       8    /* Max relay steps per TX or RX sequence */

/* NVS namespace and blob key */
#define CFG_NVS_NAMESPACE   "seq_cfg"
#define CFG_NVS_KEY         "app_cfg"

/* ---------------------------------------------------------
 * seq_step_t — one step in a TX or RX relay sequence
 * relay_id: 1–6 matching schematic labels
 * state:    true = relay ON, false = relay OFF
 * delay_ms: pause after this step before the next
 * --------------------------------------------------------- */
typedef struct {
    uint8_t  relay_id;
    uint8_t  state;       /* bool — packed as uint8 for NVS blob portability */
    uint16_t delay_ms;
} seq_step_t;

/* ---------------------------------------------------------
 * app_config_t — full runtime configuration blob
 * Stored as a single NVS blob under CFG_NVS_NAMESPACE/CFG_NVS_KEY.
 * --------------------------------------------------------- */
typedef struct {
    /* Relay sequences */
    seq_step_t tx_steps[SEQ_MAX_STEPS];
    uint8_t    tx_num_steps;

    seq_step_t rx_steps[SEQ_MAX_STEPS];
    uint8_t    rx_num_steps;

    /* Fault thresholds */
    float      swr_fault_threshold;       /* default: 3.0  */
    float      temp_fault_threshold_c;   /* default: 65.0 */

    /* Power meter calibration — P = cal_factor × V² */
    float      fwd_power_cal_factor;     /* default: 1.0  */
    float      ref_power_cal_factor;     /* default: 1.0  */

    /* Thermistor (Steinhart-Hart beta model) */
    float      thermistor_beta;          /* default: 3950   */
    float      thermistor_r0_ohms;       /* default: 100000 (at 25°C) */
    float      thermistor_r_series_ohms; /* default: 100000 */
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

#ifdef __cplusplus
}
#endif
