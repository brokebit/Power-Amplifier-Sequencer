#include <string.h>

#include "esp_http_server.h"

#include "cJSON.h"
#include "config.h"
#include "monitor.h"
#include "sequencer.h"

#include "web_json.h"
#include "web_server.h"

/* ---- POST /api/seq ------------------------------------------------------ */

static esp_err_t api_seq_handler(httpd_req_t *req)
{
    cJSON *body = web_parse_body(req);
    if (!body) {
        return ESP_OK;
    }

    cJSON *dir_json   = cJSON_GetObjectItem(body, "direction");
    cJSON *steps_json = cJSON_GetObjectItem(body, "steps");
    if (!dir_json || !cJSON_IsString(dir_json) ||
        !steps_json || !cJSON_IsArray(steps_json)) {
        cJSON_Delete(body);
        return web_json_error(req, 400,
            "missing 'direction' (\"tx\"|\"rx\") and 'steps' (array)");
    }

    const char *dir = dir_json->valuestring;
    bool is_tx;
    if (strcmp(dir, "tx") == 0) {
        is_tx = true;
    } else if (strcmp(dir, "rx") == 0) {
        is_tx = false;
    } else {
        cJSON_Delete(body);
        return web_json_error(req, 400, "direction must be \"tx\" or \"rx\"");
    }

    int count = cJSON_GetArraySize(steps_json);
    if (count < 1 || count > SEQ_MAX_STEPS) {
        cJSON_Delete(body);
        char msg[48];
        snprintf(msg, sizeof(msg), "steps count must be 1-%d", SEQ_MAX_STEPS);
        return web_json_error(req, 400, msg);
    }

    app_config_t *cfg = web_get_config();
    seq_step_t *steps = is_tx ? cfg->tx_steps : cfg->rx_steps;
    uint8_t *num_ptr = is_tx ? &cfg->tx_num_steps : &cfg->rx_num_steps;

    seq_step_t new_steps[SEQ_MAX_STEPS];
    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_GetArrayItem(steps_json, i);
        cJSON *relay_id_json = cJSON_GetObjectItem(item, "relay_id");
        cJSON *state_json = cJSON_GetObjectItem(item, "state");
        cJSON *delay_json = cJSON_GetObjectItem(item, "delay_ms");

        if (!relay_id_json || !cJSON_IsNumber(relay_id_json) ||
            !state_json || !cJSON_IsBool(state_json) ||
            !delay_json || !cJSON_IsNumber(delay_json)) {
            cJSON_Delete(body);
            char msg[80];
            snprintf(msg, sizeof(msg),
                     "each step needs: relay_id (1-%d), state (bool), delay_ms (0-%d)",
                     HW_RELAY_COUNT, SEQ_MAX_DELAY_MS);
            return web_json_error(req, 400, msg);
        }

        int relay_id = relay_id_json->valueint;
        if (relay_id < 1 || relay_id > HW_RELAY_COUNT) {
            cJSON_Delete(body);
            char msg[48];
            snprintf(msg, sizeof(msg), "relay_id must be 1-%d", HW_RELAY_COUNT);
            return web_json_error(req, 400, msg);
        }
        int delay_ms = delay_json->valueint;
        if (delay_ms < 0 || delay_ms > SEQ_MAX_DELAY_MS) {
            cJSON_Delete(body);
            char msg[48];
            snprintf(msg, sizeof(msg), "delay_ms must be 0-%d", SEQ_MAX_DELAY_MS);
            return web_json_error(req, 400, msg);
        }

        new_steps[i].relay_id = (uint8_t)relay_id;
        new_steps[i].state = cJSON_IsTrue(state_json) ? 1 : 0;
        new_steps[i].delay_ms = (uint16_t)delay_ms;
    }

    config_lock();
    memcpy(steps, new_steps, count * sizeof(seq_step_t));
    *num_ptr = (uint8_t)count;
    config_unlock();

    cJSON_Delete(body);
    return web_json_ok(req, NULL);
}

/* ---- POST /api/seq/apply ------------------------------------------------ */

static esp_err_t api_seq_apply_handler(httpd_req_t *req)
{
    app_config_t *cfg = web_get_config();

    esp_err_t err = sequencer_update_config(cfg);
    if (err == ESP_ERR_INVALID_STATE) {
        return web_json_error(req, 409, "sequencer not in RX state");
    }
    if (err != ESP_OK) {
        return web_json_error(req, 500, "failed to update sequencer config");
    }

    monitor_update_config(cfg);
    return web_json_ok(req, NULL);
}

/* ---- registration ------------------------------------------------------- */

void web_register_api_seq(httpd_handle_t server)
{
    const httpd_uri_t seq_uri = {
        .uri ="/api/seq",
        .method =HTTP_POST,
        .handler = api_seq_handler,
    };
    httpd_register_uri_handler(server, &seq_uri);

    const httpd_uri_t apply_uri = {
        .uri ="/api/seq/apply",
        .method =HTTP_POST,
        .handler = api_seq_apply_handler,
    };
    httpd_register_uri_handler(server, &apply_uri);
}
