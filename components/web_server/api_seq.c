#include <string.h>

#include "esp_http_server.h"

#include "cJSON.h"
#include "config.h"
#include "sequencer.h"

#include "web_json.h"

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
        char msg[64];
        snprintf(msg, sizeof(msg), "step count must be 1-%d", SEQ_MAX_STEPS);
        return web_json_error(req, 400, msg);
    }

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

        new_steps[i].relay_id = (uint8_t)relay_id_json->valueint;
        new_steps[i].state = cJSON_IsTrue(state_json) ? 1 : 0;
        new_steps[i].delay_ms = (uint16_t)delay_json->valueint;
    }
    cJSON_Delete(body);

    char err_msg[80];
    esp_err_t err = config_set_sequence(is_tx, new_steps, (uint8_t)count,
                                        err_msg, sizeof(err_msg));
    if (err != ESP_OK) {
        return web_json_error(req, 400, err_msg);
    }
    return web_json_ok(req, NULL);
}

/* ---- POST /api/seq/apply ------------------------------------------------ */

static esp_err_t api_seq_apply_handler(httpd_req_t *req)
{
    esp_err_t err = config_apply();
    if (err == ESP_ERR_INVALID_STATE) {
        return web_json_error(req, 409, "sequencer not in RX state");
    }
    if (err == ESP_ERR_TIMEOUT) {
        return web_json_error(req, 409, "sequencer busy (PTT active)");
    }
    if (err != ESP_OK) {
        return web_json_error(req, 500, "failed to apply config");
    }
    return web_json_ok(req, NULL);
}

/* ---- registration ------------------------------------------------------- */

void web_register_api_seq(httpd_handle_t server)
{
    const httpd_uri_t seq_uri = {
        .uri = "/api/seq",
        .method = HTTP_POST,
        .handler = api_seq_handler,
    };
    httpd_register_uri_handler(server, &seq_uri);

    const httpd_uri_t apply_uri = {
        .uri = "/api/seq/apply",
        .method = HTTP_POST,
        .handler = api_seq_apply_handler,
    };
    httpd_register_uri_handler(server, &apply_uri);
}
