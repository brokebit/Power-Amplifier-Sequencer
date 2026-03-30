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
static app_config_t      s_draft;                            /* the authoritative runtime config */
static app_config_t      s_last_applied;                     /* snapshot at last successful config_apply() */
static config_apply_cb_t s_apply_cbs[CONFIG_MAX_APPLY_CBS];
static int               s_num_apply_cbs;

void config_lock(void)
{
    xSemaphoreTake(s_cfg_mutex, portMAX_DELAY);
}

void config_unlock(void)
{
    xSemaphoreGive(s_cfg_mutex);
}

void config_defaults(void)
{
    config_lock();
    memset(&s_draft, 0, sizeof(s_draft));

    /* Default TX sequence: R3 → R1 → R2 (ON), 1000ms inter-step */
    s_draft.tx_steps[0] = (seq_step_t){ .relay_id = 3, .state = 1, .delay_ms = 1000 };
    s_draft.tx_steps[1] = (seq_step_t){ .relay_id = 1, .state = 1, .delay_ms = 1000 };
    s_draft.tx_steps[2] = (seq_step_t){ .relay_id = 2, .state = 1, .delay_ms = 0  };
    s_draft.tx_num_steps = 3;

    /* Default RX sequence: R2 → R1 → R3 (OFF), 1000ms inter-step */
    s_draft.rx_steps[0] = (seq_step_t){ .relay_id = 2, .state = 0, .delay_ms = 1000 };
    s_draft.rx_steps[1] = (seq_step_t){ .relay_id = 1, .state = 0, .delay_ms = 1000 };
    s_draft.rx_steps[2] = (seq_step_t){ .relay_id = 3, .state = 0, .delay_ms = 0  };
    s_draft.rx_num_steps = 3;

    /* Fault thresholds */
    s_draft.swr_fault_threshold = 3.0f;
    s_draft.temp1_fault_threshold_c = 65.0f;
    s_draft.temp2_fault_threshold_c = 65.0f;
    s_draft.pa_relay_id = 2;

    /* Power meter calibration */
    s_draft.fwd_power_cal_factor = 1.0f;
    s_draft.ref_power_cal_factor = 1.0f;

    /* Thermistor — NTC 100kΩ β=3950, 100kΩ series */
    s_draft.thermistor_beta = 3950.0f;
    s_draft.thermistor_r0_ohms = 100000.0f;
    s_draft.thermistor_r_series_ohms = 100000.0f;

    config_unlock();
}

esp_err_t config_init(void)
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
    err = nvs_get_blob(handle, CFG_NVS_KEY, &s_draft, &required_size);

    if (err == ESP_ERR_NVS_NOT_FOUND || required_size != sizeof(app_config_t)) {
        /* First boot or struct size changed — write defaults */
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "No saved config found, writing defaults");
        } else {
            ESP_LOGW(TAG, "Config size mismatch (stored %d, expected %d), resetting to defaults",
                     (int)required_size, (int)sizeof(app_config_t));
        }
        config_defaults();
        err = nvs_set_blob(handle, CFG_NVS_KEY, &s_draft, sizeof(app_config_t));
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

    /* Initialise last-applied snapshot so config_pending_apply() returns false at boot */
    memcpy(&s_last_applied, &s_draft, sizeof(s_last_applied));

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

esp_err_t config_set_by_key(const char *key, const char *value_str,
                            char *err_msg, size_t err_len)
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
            float *field = (float *)((uint8_t *)&s_draft + s_float_keys[i].offset);
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
            uint8_t *field = (uint8_t *)&s_draft + s_int_keys[i].offset;
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

esp_err_t config_save(void)
{
    config_lock();

    nvs_handle_t handle;
    esp_err_t err = nvs_open(CFG_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        config_unlock();
        return err;
    }

    err = nvs_set_blob(handle, CFG_NVS_KEY, &s_draft, sizeof(app_config_t));
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

/* ---- snapshot & service functions ---------------------------------------- */

void config_snapshot(app_config_t *out)
{
    config_lock();
    memcpy(out, &s_draft, sizeof(*out));
    config_unlock();
}

esp_err_t config_set_relay_name(uint8_t relay_id, const char *name,
                                char *err_msg, size_t err_len)
{
    if (relay_id < 1 || relay_id > HW_RELAY_COUNT) {
        if (err_msg) {
            snprintf(err_msg, err_len, "relay ID %d out of range [1..%d]",
                     relay_id, HW_RELAY_COUNT);
        }
        return ESP_ERR_INVALID_ARG;
    }

    config_lock();
    char *dst = s_draft.relay_names[relay_id - 1];
    if (name && name[0] != '\0') {
        strncpy(dst, name, CFG_RELAY_NAME_LEN - 1);
        dst[CFG_RELAY_NAME_LEN - 1] = '\0';
    } else {
        dst[0] = '\0';
    }
    config_unlock();
    return ESP_OK;
}

esp_err_t config_set_sequence(bool is_tx, const seq_step_t *steps, uint8_t count,
                              char *err_msg, size_t err_len)
{
    if (count < 1 || count > SEQ_MAX_STEPS) {
        if (err_msg) {
            snprintf(err_msg, err_len, "step count %d out of range [1..%d]",
                     count, SEQ_MAX_STEPS);
        }
        return ESP_ERR_INVALID_ARG;
    }

    for (uint8_t i = 0; i < count; i++) {
        if (steps[i].relay_id < 1 || steps[i].relay_id > HW_RELAY_COUNT) {
            if (err_msg) {
                snprintf(err_msg, err_len, "step %d: relay ID %d out of range [1..%d]",
                         i + 1, steps[i].relay_id, HW_RELAY_COUNT);
            }
            return ESP_ERR_INVALID_ARG;
        }
        if (steps[i].state > 1) {
            if (err_msg) {
                snprintf(err_msg, err_len, "step %d: state must be 0 or 1", i + 1);
            }
            return ESP_ERR_INVALID_ARG;
        }
        if (steps[i].delay_ms > SEQ_MAX_DELAY_MS) {
            if (err_msg) {
                snprintf(err_msg, err_len, "step %d: delay %d exceeds max %d ms",
                         i + 1, steps[i].delay_ms, SEQ_MAX_DELAY_MS);
            }
            return ESP_ERR_INVALID_ARG;
        }
    }

    config_lock();
    seq_step_t *dst = is_tx ? s_draft.tx_steps : s_draft.rx_steps;
    uint8_t    *cnt = is_tx ? &s_draft.tx_num_steps : &s_draft.rx_num_steps;
    memcpy(dst, steps, count * sizeof(seq_step_t));
    *cnt = count;
    config_unlock();
    return ESP_OK;
}

/* ---- callback registry & apply ------------------------------------------ */

esp_err_t config_register_apply_cb(config_apply_cb_t cb)
{
    if (!cb) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Idempotent — same pointer is a no-op */
    for (int i = 0; i < s_num_apply_cbs; i++) {
        if (s_apply_cbs[i] == cb) {
            return ESP_OK;
        }
    }

    if (s_num_apply_cbs >= CONFIG_MAX_APPLY_CBS) {
        ESP_LOGE(TAG, "apply callback overflow (max %d)", CONFIG_MAX_APPLY_CBS);
        return ESP_ERR_NO_MEM;
    }

    s_apply_cbs[s_num_apply_cbs++] = cb;
    return ESP_OK;
}

esp_err_t config_apply(void)
{
    /* Snapshot the draft under the lock */
    app_config_t snap;
    config_lock();
    memcpy(&snap, &s_draft, sizeof(snap));
    config_unlock();

    /* Call each registered callback in order; stop on first failure */
    for (int i = 0; i < s_num_apply_cbs; i++) {
        esp_err_t err = s_apply_cbs[i](&snap);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "config_apply: callback %d failed: %s", i, esp_err_to_name(err));
            return err;
        }
    }

    /* All callbacks succeeded — update the last-applied snapshot */
    config_lock();
    memcpy(&s_last_applied, &snap, sizeof(s_last_applied));
    config_unlock();

    return ESP_OK;
}

bool config_pending_apply(void)
{
    config_lock();
    bool pending = memcmp(&s_draft, &s_last_applied, sizeof(app_config_t)) != 0;
    config_unlock();
    return pending;
}
