#include <stdio.h>
#include <string.h>

#include "esp_http_server.h"

#include "cJSON.h"
#include "config.h"
#include "hw_config.h"
#include "relays.h"

#include "web_json.h"

/* ---- POST /api/relay ---------------------------------------------------- */

static esp_err_t api_relay_handler(httpd_req_t *req)
{
    cJSON *body = web_parse_body(req);
    if (!body) {
        return ESP_OK;
    }

    cJSON *id_json = cJSON_GetObjectItem(body, "id");
    cJSON *on_json = cJSON_GetObjectItem(body, "on");
    if (!id_json || !cJSON_IsNumber(id_json) || !on_json || !cJSON_IsBool(on_json)) {
        cJSON_Delete(body);
        return web_json_error(req, 400, "missing 'id' (number) and 'on' (bool) fields");
    }

    int id = id_json->valueint;
    bool on = cJSON_IsTrue(on_json);
    cJSON_Delete(body);

    esp_err_t err = relay_set((uint8_t)id, on);
    if (err == ESP_ERR_INVALID_ARG) {
        char msg[48];
        snprintf(msg, sizeof(msg), "relay id must be 1-%d", HW_RELAY_COUNT);
        return web_json_error(req, 400, msg);
    }
    if (err != ESP_OK) {
        return web_json_error(req, 500, "relay_set failed");
    }

    return web_json_ok(req, NULL);
}

/* ---- POST /api/relay/name ----------------------------------------------- */

static esp_err_t api_relay_name_handler(httpd_req_t *req)
{
    cJSON *body = web_parse_body(req);
    if (!body) {
        return ESP_OK;
    }

    cJSON *id_json = cJSON_GetObjectItem(body, "id");
    cJSON *name_json = cJSON_GetObjectItem(body, "name");
    if (!id_json || !cJSON_IsNumber(id_json)) {
        cJSON_Delete(body);
        return web_json_error(req, 400, "missing 'id' (number) field");
    }

    int id = id_json->valueint;
    const char *name = (name_json && cJSON_IsString(name_json))
                           ? name_json->valuestring : NULL;

    char err_msg[64];
    esp_err_t err = config_set_relay_name((uint8_t)id, name,
                                          err_msg, sizeof(err_msg));
    cJSON_Delete(body);

    if (err != ESP_OK) {
        return web_json_error(req, 400, err_msg);
    }
    return web_json_ok(req, NULL);
}

/* ---- registration ------------------------------------------------------- */

void web_register_api_relay(httpd_handle_t server)
{
    const httpd_uri_t relay_uri = {
        .uri = "/api/relay",
        .method = HTTP_POST,
        .handler = api_relay_handler,
    };
    httpd_register_uri_handler(server, &relay_uri);

    const httpd_uri_t name_uri = {
        .uri = "/api/relay/name",
        .method = HTTP_POST,
        .handler = api_relay_name_handler,
    };
    httpd_register_uri_handler(server, &name_uri);
}
