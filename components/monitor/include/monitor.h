#pragma once

#include "esp_err.h"

#include "ads1115.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise monitor: I2C bus, both ADS1115s, ALERT GPIO ISRs.
 * Call after config_init() and sequencer_init() (needs the event queue).
 * Snapshots the config internally for the lifetime of the task.
 */
esp_err_t monitor_init(void);

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
esp_err_t monitor_read_channel(int chip, ads1115_channel_t ch, float *out_voltage);

#ifdef __cplusplus
}
#endif
