#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "config.h"

static const char *TAG = "config";

static SemaphoreHandle_t s_cfg_mutex;

void config_lock(void)
{
    xSemaphoreTake(s_cfg_mutex, portMAX_DELAY);
}

void config_unlock(void)
{
    xSemaphoreGive(s_cfg_mutex);
}

void config_defaults(app_config_t *cfg)
{
    config_lock();
    memset(cfg, 0, sizeof(*cfg));

    /* Default TX sequence: R3 → R1 → R2 (ON), 10ms inter-step */
    cfg->tx_steps[0] = (seq_step_t){ .relay_id = 3, .state = 1, .delay_ms = 1000 };
    cfg->tx_steps[1] = (seq_step_t){ .relay_id = 1, .state = 1, .delay_ms = 1000 };
    cfg->tx_steps[2] = (seq_step_t){ .relay_id = 2, .state = 1, .delay_ms = 0  };
    cfg->tx_num_steps = 3;

    /* Default RX sequence: R2 → R1 → R3 (OFF), 10ms inter-step */
    cfg->rx_steps[0] = (seq_step_t){ .relay_id = 2, .state = 0, .delay_ms = 1000 };
    cfg->rx_steps[1] = (seq_step_t){ .relay_id = 1, .state = 0, .delay_ms = 1000 };
    cfg->rx_steps[2] = (seq_step_t){ .relay_id = 3, .state = 0, .delay_ms = 0  };
    cfg->rx_num_steps = 3;

    /* Fault thresholds */
    cfg->swr_fault_threshold = 3.0f;
    cfg->temp1_fault_threshold_c = 65.0f;
    cfg->temp2_fault_threshold_c = 65.0f;
    cfg->pa_relay_id = 2;

    /* Power meter calibration */
    cfg->fwd_power_cal_factor = 1.0f;
    cfg->ref_power_cal_factor = 1.0f;

    /* Thermistor — NTC 100kΩ β=3950, 100kΩ series */
    cfg->thermistor_beta = 3950.0f;
    cfg->thermistor_r0_ohms = 100000.0f;
    cfg->thermistor_r_series_ohms = 100000.0f;

    config_unlock();
}

esp_err_t config_init(app_config_t *cfg)
{
    s_cfg_mutex = xSemaphoreCreateMutex();
    if (!s_cfg_mutex) {
        ESP_LOGE(TAG, "Failed to create config mutex");
        return ESP_ERR_NO_MEM;
    }

    /* Initialise NVS — erase and reinit if partition is truncated or has no pages */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition issue (%s), erasing and reinitialising", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return err;
    }

    nvs_handle_t handle;
    err = nvs_open(CFG_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    size_t required_size = sizeof(app_config_t);
    err = nvs_get_blob(handle, CFG_NVS_KEY, cfg, &required_size);

    if (err == ESP_ERR_NVS_NOT_FOUND || required_size != sizeof(app_config_t)) {
        /* First boot or struct size changed — write defaults */
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "No saved config found, writing defaults");
        } else {
            ESP_LOGW(TAG, "Config size mismatch (stored %d, expected %d), resetting to defaults",
                     (int)required_size, (int)sizeof(app_config_t));
        }
        config_defaults(cfg);
        err = nvs_set_blob(handle, CFG_NVS_KEY, cfg, sizeof(app_config_t));
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write defaults: %s", esp_err_to_name(err));
        }
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_blob failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Config loaded from NVS");
    }

    nvs_close(handle);
    return err;
}

const char *config_relay_label(const app_config_t *cfg, uint8_t relay_id,
                               char *buf, size_t buf_len)
{
    if (relay_id >= 1 && relay_id <= HW_RELAY_COUNT
        && cfg->relay_names[relay_id - 1][0] != '\0') {
        snprintf(buf, buf_len, "R%d/%s", relay_id, cfg->relay_names[relay_id - 1]);
    } else {
        snprintf(buf, buf_len, "R%d", relay_id);
    }
    return buf;
}

/* ---- key-value setter --------------------------------------------------- */

typedef struct {
    const char *key;
    size_t offset;
    float min;
    float max;
} config_float_key_t;

typedef struct {
    const char *key;
    size_t offset;
    int min;
    int max;
} config_int_key_t;

#define CFG_KEY(name, field, lo, hi) \
    { name, offsetof(app_config_t, field), lo, hi }

#define CFG_INT_KEY(name, field, lo, hi) \
    { name, offsetof(app_config_t, field), lo, hi }

static const config_float_key_t s_float_keys[] = {
    CFG_KEY("swr_threshold",  swr_fault_threshold,       1.0f,  99.0f),
    CFG_KEY("temp1_threshold", temp1_fault_threshold_c,    0.0f, 200.0f),
    CFG_KEY("temp2_threshold", temp2_fault_threshold_c,    0.0f, 200.0f),
    CFG_KEY("fwd_cal",        fwd_power_cal_factor,       0.001f, 1000.0f),
    CFG_KEY("ref_cal",        ref_power_cal_factor,       0.001f, 1000.0f),
    CFG_KEY("therm_beta",     thermistor_beta,            1.0f, 100000.0f),
    CFG_KEY("therm_r0",       thermistor_r0_ohms,         1.0f, 10000000.0f),
    CFG_KEY("therm_rseries",  thermistor_r_series_ohms,   1.0f, 10000000.0f),
};

#define NUM_FLOAT_KEYS (sizeof(s_float_keys) / sizeof(s_float_keys[0]))

static const config_int_key_t s_int_keys[] = {
    CFG_INT_KEY("pa_relay", pa_relay_id, 1, HW_RELAY_COUNT),
};

#define NUM_INT_KEYS (sizeof(s_int_keys) / sizeof(s_int_keys[0]))

esp_err_t config_set_by_key(app_config_t *cfg, const char *key,
                            const char *value_str, char *err_msg, size_t err_len)
{
    config_lock();

    /* Try float keys first */
    for (size_t i = 0; i < NUM_FLOAT_KEYS; i++) {
        if (strcmp(s_float_keys[i].key, key) == 0) {
            char *end;
            float val = strtof(value_str, &end);
            if (end == value_str || *end != '\0') {
                if (err_msg) {
                    snprintf(err_msg, err_len, "invalid number: %s", value_str);
                }
                config_unlock();
                return ESP_ERR_INVALID_ARG;
            }
            if (val < s_float_keys[i].min || val > s_float_keys[i].max) {
                if (err_msg) {
                    snprintf(err_msg, err_len, "out of range [%.3g .. %.3g]",
                             s_float_keys[i].min, s_float_keys[i].max);
                }
                config_unlock();
                return ESP_ERR_INVALID_ARG;
            }
            float *field = (float *)((uint8_t *)cfg + s_float_keys[i].offset);
            *field = val;
            config_unlock();
            return ESP_OK;
        }
    }

    /* Try integer keys */
    for (size_t i = 0; i < NUM_INT_KEYS; i++) {
        if (strcmp(s_int_keys[i].key, key) == 0) {
            char *end;
            long val = strtol(value_str, &end, 10);
            if (end == value_str || *end != '\0') {
                if (err_msg) {
                    snprintf(err_msg, err_len, "invalid integer: %s", value_str);
                }
                config_unlock();
                return ESP_ERR_INVALID_ARG;
            }
            if (val < s_int_keys[i].min || val > s_int_keys[i].max) {
                if (err_msg) {
                    snprintf(err_msg, err_len, "out of range [%d .. %d]",
                             s_int_keys[i].min, s_int_keys[i].max);
                }
                config_unlock();
                return ESP_ERR_INVALID_ARG;
            }
            uint8_t *field = (uint8_t *)cfg + s_int_keys[i].offset;
            *field = (uint8_t)val;
            config_unlock();
            return ESP_OK;
        }
    }

    config_unlock();
    if (err_msg) {
        snprintf(err_msg, err_len, "unknown key: %s", key);
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t config_save(const app_config_t *cfg)
{
    config_lock();

    nvs_handle_t handle;
    esp_err_t err = nvs_open(CFG_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        config_unlock();
        return err;
    }

    err = nvs_set_blob(handle, CFG_NVS_KEY, cfg, sizeof(app_config_t));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "config_save failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Config saved to NVS");
    }

    nvs_close(handle);
    config_unlock();
    return err;
}
