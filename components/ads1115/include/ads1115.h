#pragma once

/* =========================================================
 * ads1115.h — ADS1115 16-bit ADC I2C driver
 *
 * Uses the ESP-IDF v5.x i2c_master API.
 * Each handle wraps one ADS1115 device on a shared bus.
 *
 * ALERT/RDY is configured as a conversion-ready signal
 * (Hi_thresh MSB=1, Lo_thresh MSB=0). The caller owns the
 * GPIO ISR for the ALERT pin and uses it to synchronise
 * reads with ads1115_read_raw().
 * ========================================================= */

#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ads1115_dev *ads1115_handle_t;

typedef enum
{
    ADS1115_CHANNEL_0 = 0,   /* AIN0 vs GND */
    ADS1115_CHANNEL_1,        /* AIN1 vs GND */
    ADS1115_CHANNEL_2,        /* AIN2 vs GND */
    ADS1115_CHANNEL_3,        /* AIN3 vs GND */
} ads1115_channel_t;

typedef enum
{
    ADS1115_PGA_6144 = 0,   /* ±6.144 V — 187.5 µV/LSB */
    ADS1115_PGA_4096,        /* ±4.096 V — 125.0 µV/LSB */
    ADS1115_PGA_2048,        /* ±2.048 V —  62.5 µV/LSB */
    ADS1115_PGA_1024,        /* ±1.024 V —  31.3 µV/LSB */
    ADS1115_PGA_0512,        /* ±0.512 V —  15.6 µV/LSB */
    ADS1115_PGA_0256,        /* ±0.256 V —   7.8 µV/LSB */
} ads1115_pga_t;

/**
 * Initialise one ADS1115 on an existing I2C master bus.
 * Configures ALERT/RDY as a conversion-ready pin.
 *
 * @param bus        Shared I2C master bus handle (from i2c_new_master_bus)
 * @param addr       7-bit I2C address (e.g. HW_ADS1115_0_ADDR)
 * @param pga        PGA gain setting — use ADS1115_PGA_4096 for 0–3.3 V inputs
 * @param out_handle Receives the new handle on success
 */
esp_err_t ads1115_init(i2c_master_bus_handle_t bus, uint8_t addr,
                       ads1115_pga_t pga, ads1115_handle_t *out_handle);

/**
 * Trigger a single-shot conversion on the given channel.
 * The chip pulses ALERT/RDY when the result is ready.
 * Call ads1115_read_raw() after the ALERT fires.
 */
esp_err_t ads1115_start_single_shot(ads1115_handle_t handle,
                                    ads1115_channel_t channel);

/**
 * Read the last conversion result. Call only after the ALERT/RDY pin fires.
 */
esp_err_t ads1115_read_raw(ads1115_handle_t handle, int16_t *out_raw);

/**
 * Convert a raw ADC reading to volts using the handle's PGA setting.
 */
float ads1115_raw_to_voltage(ads1115_handle_t handle, int16_t raw);

/**
 * Remove the device from the bus and free resources.
 */
void ads1115_deinit(ads1115_handle_t handle);

#ifdef __cplusplus
}
#endif
