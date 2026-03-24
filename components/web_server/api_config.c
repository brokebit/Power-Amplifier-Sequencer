#include <string.h>

#include "cJSON.h"
#include "esp_http_server.h"

#include "config.h"
#include "hw_config.h"

#include "web_json.h"
#include "web_server.h"

/* ---- GET /api/config ---------------------------------------------------- */

static esp_err_t api_config_get_handler(httpd_req_t *req)
{
    const app_config_t *cfg = web_get_config();

    cJSON *data = cJSON_CreateObject();
    if (!data) {
        return web_json_error(req, 500, "out of memory");
    }

    /* Thresholds */
    cJSON_AddNumberToObject(data, "swr_threshold", cfg->swr_fault_threshold);
    cJSON_AddNumberToObject(data, "temp1_threshold", cfg->temp1_fault_threshold_c);
    cJSON_AddNumberToObject(data, "temp2_threshold", cfg->temp2_fault_threshold_c);
    cJSON_AddNumberToObject(data, "pa_relay", cfg->pa_relay_id);

    /* Calibration */
    cJSON_AddNumberToObject(data, "fwd_cal", cfg->fwd_power_cal_factor);
    cJSON_AddNumberToObject(data, "ref_cal", cfg->ref_power_cal_factor);

    /* Thermistor */
    cJSON_AddNumberToObject(data, "therm_beta", cfg->thermistor_beta);
    cJSON_AddNumberToObject(data, "therm_r0", cfg->thermistor_r0_ohms);
    cJSON_AddNumberToObject(data, "therm_rseries", cfg->thermistor_r_series_ohms);

    /* Relay names */
    cJSON *names = cJSON_CreateArray();
    for (int i = 0; i < HW_RELAY_COUNT; i++) {
        cJSON_AddItemToArray(names,
            cJSON_CreateString(cfg->relay_names[i][0] ? cfg->relay_names[i] : ""));
    }
    cJSON_AddItemToObject(data, "relay_names", names);

    /* TX sequence */
    cJSON *tx = cJSON_CreateArray();
    for (int i = 0; i < cfg->tx_num_steps; i++) {
        cJSON *step = cJSON_CreateObject();
        cJSON_AddNumberToObject(step, "relay_id", cfg->tx_steps[i].relay_id);
        cJSON_AddBoolToObject(step, "state", cfg->tx_steps[i].state);
        cJSON_AddNumberToObject(step, "delay_ms", cfg->tx_steps[i].delay_ms);
        cJSON_AddItemToArray(tx, step);
    }
    cJSON_AddItemToObject(data, "tx_steps", tx);

    /* RX sequence */
    cJSON *rx = cJSON_CreateArray();
    for (int i = 0; i < cfg->rx_num_steps; i++) {
        cJSON *step = cJSON_CreateObject();
        cJSON_AddNumberToObject(step, "relay_id", cfg->rx_steps[i].relay_id);
        cJSON_AddBoolToObject(step, "state", cfg->rx_steps[i].state);
        cJSON_AddNumberToObject(step, "delay_ms", cfg->rx_steps[i].delay_ms);
        cJSON_AddItemToArray(rx, step);
    }
    cJSON_AddItemToObject(data, "rx_steps", rx);

    return web_json_ok(req, data);
}

/* ---- POST /api/config --------------------------------------------------- */

static esp_err_t api_config_set_handler(httpd_req_t *req)
{
    cJSON *body = web_parse_body(req);
    if (!body) {
        return ESP_OK;  /* error already sent */
    }

    cJSON *key_json = cJSON_GetObjectItem(body, "key");
    cJSON *val_json = cJSON_GetObjectItem(body, "value");
    if (!key_json || !cJSON_IsString(key_json) || !val_json) {
        cJSON_Delete(body);
        return web_json_error(req, 400, "missing 'key' (string) and 'value' fields");
    }

    /* Convert value to string for config_set_by_key */
    char val_str[32];
    if (cJSON_IsNumber(val_json)) {
        snprintf(val_str, sizeof(val_str), "%g", val_json->valuedouble);
    } else if (cJSON_IsString(val_json)) {
        snprintf(val_str, sizeof(val_str), "%s", val_json->valuestring);
    } else {
        cJSON_Delete(body);
        return web_json_error(req, 400, "value must be a number or string");
    }

    app_config_t *cfg = web_get_config();
    char err_msg[64] = {0};
    esp_err_t err = config_set_by_key(cfg, key_json->valuestring, val_str,
                                       err_msg, sizeof(err_msg));
    cJSON_Delete(body);

    if (err != ESP_OK) {
        return web_json_error(req, 400, err_msg[0] ? err_msg : "invalid key or value");
    }

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "key", key_json->valuestring);
    cJSON_AddStringToObject(data, "value", val_str);
    return web_json_ok(req, data);
}

/* ---- POST /api/config/save ---------------------------------------------- */

static esp_err_t api_config_save_handler(httpd_req_t *req)
{
    app_config_t *cfg = web_get_config();
    esp_err_t err = config_save(cfg);
    if (err != ESP_OK) {
        return web_json_error(req, 500, "failed to save config to NVS");
    }
    return web_json_ok(req, NULL);
}

/* ---- POST /api/config/defaults ------------------------------------------ */

static esp_err_t api_config_defaults_handler(httpd_req_t *req)
{
    app_config_t *cfg = web_get_config();
    config_defaults(cfg);
    return web_json_ok(req, NULL);
}

/* ---- registration ------------------------------------------------------- */

void web_register_api_config(httpd_handle_t server)
{
    const httpd_uri_t get_uri = {
        .uri     = "/api/config",
        .method  = HTTP_GET,
        .handler = api_config_get_handler,
    };
    httpd_register_uri_handler(server, &get_uri);

    const httpd_uri_t set_uri = {
        .uri     = "/api/config",
        .method  = HTTP_POST,
        .handler = api_config_set_handler,
    };
    httpd_register_uri_handler(server, &set_uri);

    const httpd_uri_t save_uri = {
        .uri     = "/api/config/save",
        .method  = HTTP_POST,
        .handler = api_config_save_handler,
    };
    httpd_register_uri_handler(server, &save_uri);

    const httpd_uri_t defaults_uri = {
        .uri     = "/api/config/defaults",
        .method  = HTTP_POST,
        .handler = api_config_defaults_handler,
    };
    httpd_register_uri_handler(server, &defaults_uri);
}
