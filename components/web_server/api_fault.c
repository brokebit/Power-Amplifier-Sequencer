#include "esp_http_server.h"

#include "cJSON.h"
#include "sequencer.h"

#include "web_json.h"

/* ---- POST /api/fault/clear ---------------------------------------------- */

static esp_err_t api_fault_clear_handler(httpd_req_t *req)
{
    esp_err_t err = sequencer_clear_fault();
    if (err == ESP_ERR_INVALID_STATE) {
        return web_json_error(req, 409, "not in FAULT state");
    }
    if (err != ESP_OK) {
        return web_json_error(req, 500, "failed to clear fault");
    }
    return web_json_ok(req, NULL);
}

/* ---- POST /api/fault/inject --------------------------------------------- */

static esp_err_t api_fault_inject_handler(httpd_req_t *req)
{
    cJSON *body = web_parse_body(req);
    if (!body) {
        return ESP_OK;
    }

    cJSON *type_json = cJSON_GetObjectItem(body, "type");
    if (!type_json || !cJSON_IsString(type_json)) {
        cJSON_Delete(body);
        return web_json_error(req, 400, "missing 'type' string field");
    }

    seq_fault_t fault;
    if (!seq_fault_parse(type_json->valuestring, &fault)) {
        cJSON_Delete(body);
        return web_json_error(req, 400,
            "type must be: swr, temp1, temp2, or emergency");
    }
    cJSON_Delete(body);

    sequencer_inject_fault(fault);
    return web_json_ok(req, NULL);
}

/* ---- registration ------------------------------------------------------- */

void web_register_api_fault(httpd_handle_t server)
{
    const httpd_uri_t clear_uri = {
        .uri ="/api/fault/clear",
        .method =HTTP_POST,
        .handler = api_fault_clear_handler,
    };
    httpd_register_uri_handler(server, &clear_uri);

    const httpd_uri_t inject_uri = {
        .uri ="/api/fault/inject",
        .method =HTTP_POST,
        .handler = api_fault_inject_handler,
    };
    httpd_register_uri_handler(server, &inject_uri);
}
