#include <string.h>

#include "cJSON.h"
#include "esp_http_server.h"

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
    seq_step_t *steps   = is_tx ? cfg->tx_steps     : cfg->rx_steps;
    uint8_t    *num_ptr = is_tx ? &cfg->tx_num_steps : &cfg->rx_num_steps;

    seq_step_t new_steps[SEQ_MAX_STEPS];
    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_GetArrayItem(steps_json, i);
        cJSON *rid  = cJSON_GetObjectItem(item, "relay_id");
        cJSON *st   = cJSON_GetObjectItem(item, "state");
        cJSON *dms  = cJSON_GetObjectItem(item, "delay_ms");

        if (!rid || !cJSON_IsNumber(rid) || !st || !cJSON_IsBool(st) ||
            !dms || !cJSON_IsNumber(dms)) {
            cJSON_Delete(body);
            return web_json_error(req, 400,
                "each step needs: relay_id (1-6), state (bool), delay_ms (0-10000)");
        }

        int relay_id = rid->valueint;
        if (relay_id < 1 || relay_id > 6) {
            cJSON_Delete(body);
            return web_json_error(req, 400, "relay_id must be 1-6");
        }
        int delay_ms = dms->valueint;
        if (delay_ms < 0 || delay_ms > 10000) {
            cJSON_Delete(body);
            return web_json_error(req, 400, "delay_ms must be 0-10000");
        }

        new_steps[i].relay_id = (uint8_t)relay_id;
        new_steps[i].state    = cJSON_IsTrue(st) ? 1 : 0;
        new_steps[i].delay_ms = (uint16_t)delay_ms;
    }

    memcpy(steps, new_steps, count * sizeof(seq_step_t));
    *num_ptr = (uint8_t)count;

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
        .uri     = "/api/seq",
        .method  = HTTP_POST,
        .handler = api_seq_handler,
    };
    httpd_register_uri_handler(server, &seq_uri);

    const httpd_uri_t apply_uri = {
        .uri     = "/api/seq/apply",
        .method  = HTTP_POST,
        .handler = api_seq_apply_handler,
    };
    httpd_register_uri_handler(server, &apply_uri);
}
