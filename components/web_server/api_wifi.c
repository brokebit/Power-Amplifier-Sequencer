#include <string.h>

#include "cJSON.h"
#include "esp_http_server.h"

#include "wifi_sta.h"

#include "web_json.h"

/* ---- GET /api/wifi/status ----------------------------------------------- */

static esp_err_t api_wifi_status_handler(httpd_req_t *req)
{
    cJSON *data = cJSON_CreateObject();
    if (!data) {
        return web_json_error(req, 500, "out of memory");
    }

    bool connected = app_wifi_is_connected();
    cJSON_AddBoolToObject(data, "connected", connected);
    cJSON_AddBoolToObject(data, "auto_connect", app_wifi_get_enabled());

    if (connected) {
        char ip[16];
        if (app_wifi_get_ip_str(ip, sizeof(ip)) == ESP_OK) {
            cJSON_AddStringToObject(data, "ip", ip);
        }
        int8_t rssi;
        if (app_wifi_get_rssi(&rssi) == ESP_OK) {
            cJSON_AddNumberToObject(data, "rssi", rssi);
        }
    }

    return web_json_ok(req, data);
}

/* ---- POST /api/wifi/config ---------------------------------------------- */

static esp_err_t api_wifi_config_handler(httpd_req_t *req)
{
    cJSON *body = web_parse_body(req);
    if (!body) {
        return ESP_OK;
    }

    cJSON *ssid_json = cJSON_GetObjectItem(body, "ssid");
    if (!ssid_json || !cJSON_IsString(ssid_json)) {
        cJSON_Delete(body);
        return web_json_error(req, 400, "missing 'ssid' string field");
    }

    const char *pass = "";
    cJSON *pass_json = cJSON_GetObjectItem(body, "password");
    if (pass_json && cJSON_IsString(pass_json)) {
        pass = pass_json->valuestring;
    }

    esp_err_t err = app_wifi_set_credentials(ssid_json->valuestring, pass);
    cJSON_Delete(body);

    if (err != ESP_OK) {
        return web_json_error(req, 400, "invalid credentials");
    }
    return web_json_ok(req, NULL);
}

/* ---- POST /api/wifi/connect --------------------------------------------- */

static esp_err_t api_wifi_connect_handler(httpd_req_t *req)
{
    esp_err_t err = app_wifi_connect();
    if (err == ESP_ERR_NOT_FOUND) {
        return web_json_error(req, 404, "no WiFi credentials saved");
    }
    if (err != ESP_OK) {
        return web_json_error(req, 500, "connect failed");
    }
    return web_json_ok(req, NULL);
}

/* ---- POST /api/wifi/disconnect ------------------------------------------ */

static esp_err_t api_wifi_disconnect_handler(httpd_req_t *req)
{
    esp_err_t err = app_wifi_disconnect();
    if (err != ESP_OK) {
        return web_json_error(req, 500, "disconnect failed");
    }
    return web_json_ok(req, NULL);
}

/* ---- GET /api/wifi/scan ------------------------------------------------- */

static esp_err_t api_wifi_scan_handler(httpd_req_t *req)
{
    wifi_scan_result_t *results = NULL;
    uint16_t count = 0;

    esp_err_t err = app_wifi_scan_results(&results, &count);
    if (err != ESP_OK) {
        return web_json_error(req, 500, "scan failed");
    }

    cJSON *networks = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", results[i].ssid);
        cJSON_AddNumberToObject(item, "rssi", results[i].rssi);
        cJSON_AddNumberToObject(item, "channel", results[i].channel);
        cJSON_AddNumberToObject(item, "authmode", results[i].authmode);
        cJSON_AddItemToArray(networks, item);
    }
    free(results);

    cJSON *data = cJSON_CreateObject();
    cJSON_AddItemToObject(data, "networks", networks);
    cJSON_AddNumberToObject(data, "count", count);
    return web_json_ok(req, data);
}

/* ---- POST /api/wifi/auto ------------------------------------------------ */

static esp_err_t api_wifi_auto_handler(httpd_req_t *req)
{
    cJSON *body = web_parse_body(req);
    if (!body) {
        return ESP_OK;
    }

    cJSON *enabled_json = cJSON_GetObjectItem(body, "enabled");
    if (!enabled_json || !cJSON_IsBool(enabled_json)) {
        cJSON_Delete(body);
        return web_json_error(req, 400, "missing 'enabled' (bool) field");
    }

    bool enabled = cJSON_IsTrue(enabled_json);
    cJSON_Delete(body);

    esp_err_t err = app_wifi_set_enabled(enabled);
    if (err != ESP_OK) {
        return web_json_error(req, 500, "failed to set auto-connect");
    }
    return web_json_ok(req, NULL);
}

/* ---- POST /api/wifi/erase ----------------------------------------------- */

static esp_err_t api_wifi_erase_handler(httpd_req_t *req)
{
    esp_err_t err = app_wifi_erase_credentials();
    if (err != ESP_OK) {
        return web_json_error(req, 500, "erase failed");
    }
    return web_json_ok(req, NULL);
}

/* ---- registration ------------------------------------------------------- */

void web_register_api_wifi(httpd_handle_t server)
{
    const httpd_uri_t status_uri = {
        .uri     = "/api/wifi/status",
        .method  = HTTP_GET,
        .handler = api_wifi_status_handler,
    };
    httpd_register_uri_handler(server, &status_uri);

    const httpd_uri_t config_uri = {
        .uri     = "/api/wifi/config",
        .method  = HTTP_POST,
        .handler = api_wifi_config_handler,
    };
    httpd_register_uri_handler(server, &config_uri);

    const httpd_uri_t connect_uri = {
        .uri     = "/api/wifi/connect",
        .method  = HTTP_POST,
        .handler = api_wifi_connect_handler,
    };
    httpd_register_uri_handler(server, &connect_uri);

    const httpd_uri_t disconnect_uri = {
        .uri     = "/api/wifi/disconnect",
        .method  = HTTP_POST,
        .handler = api_wifi_disconnect_handler,
    };
    httpd_register_uri_handler(server, &disconnect_uri);

    const httpd_uri_t scan_uri = {
        .uri     = "/api/wifi/scan",
        .method  = HTTP_GET,
        .handler = api_wifi_scan_handler,
    };
    httpd_register_uri_handler(server, &scan_uri);

    const httpd_uri_t auto_uri = {
        .uri     = "/api/wifi/auto",
        .method  = HTTP_POST,
        .handler = api_wifi_auto_handler,
    };
    httpd_register_uri_handler(server, &auto_uri);

    const httpd_uri_t erase_uri = {
        .uri     = "/api/wifi/erase",
        .method  = HTTP_POST,
        .handler = api_wifi_erase_handler,
    };
    httpd_register_uri_handler(server, &erase_uri);
}
