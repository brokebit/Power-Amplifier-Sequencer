#include <string.h>

#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "config.h"

static const char *TAG = "config";

void config_defaults(app_config_t *cfg)
{
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
    cfg->swr_fault_threshold     = 3.0f;
    cfg->temp1_fault_threshold_c = 65.0f;
    cfg->temp2_fault_threshold_c = 65.0f;

    /* Power meter calibration */
    cfg->fwd_power_cal_factor = 1.0f;
    cfg->ref_power_cal_factor = 1.0f;

    /* Thermistor — NTC 100kΩ β=3950, 100kΩ series */
    cfg->thermistor_beta          = 3950.0f;
    cfg->thermistor_r0_ohms       = 100000.0f;
    cfg->thermistor_r_series_ohms = 100000.0f;
}

esp_err_t config_init(app_config_t *cfg)
{
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

esp_err_t config_save(const app_config_t *cfg)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(CFG_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
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
    return err;
}
