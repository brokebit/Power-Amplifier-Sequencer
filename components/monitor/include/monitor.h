#pragma once

#include "esp_err.h"

#include "ads1115.h"
#include "config.h"

/* =========================================================
 * monitor.h — ADC monitoring task
 *
 * Drives ADS1115 (0x49) in single-shot mode (8 SPS).
 *   AIN0 = forward power,   AIN1 = reflected power
 *   AIN2 = temp right PA,   AIN3 = temp left PA
 *
 * Thermistor: Steinhart-Hart beta equation, voltage divider
 *   VCC → R_series → NTC → GND, ADS1115 measures across NTC.
 *
 * Power/SWR: P = cal_factor × V²;  SWR = (1+Γ)/(1-Γ), Γ = Vr/Vf
 *
 * On threshold breach: sends SEQ_EVENT_FAULT to the sequencer queue.
 * Live readings are published to system_state each cycle (~500 ms).
 * ========================================================= */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise monitor: I2C bus, both ADS1115s, ALERT GPIO ISRs.
 * Call after sequencer_init() (needs the event queue).
 * Stores a copy of cfg for the lifetime of the task.
 */
esp_err_t monitor_init(const app_config_t *cfg);

/**
 * FreeRTOS task entry point.
 *   xTaskCreate(monitor_task, "monitor", 4096, NULL, 7, NULL);
 */
void monitor_task(void *arg);

/**
 * Update the monitor's config (thresholds, calibration, thermistor params).
 * Safe to call from any task.
 */
esp_err_t monitor_update_config(const app_config_t *cfg);

/**
 * Read a single ADC channel (blocking). Mutex-protected so it can be
 * called from any task without conflicting with monitor_task.
 * Returns ESP_OK on success, voltage written to *out_voltage.
 */
esp_err_t monitor_read_channel(ads1115_channel_t ch, float *out_voltage);

#ifdef __cplusplus
}
#endif
