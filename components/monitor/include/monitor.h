#pragma once

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

#include "esp_err.h"
#include "config.h"

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

#ifdef __cplusplus
}
#endif
