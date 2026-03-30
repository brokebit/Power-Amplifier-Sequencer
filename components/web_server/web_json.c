#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "web_json.h"

static const char *TAG = "web_json";

esp_err_t web_json_ok(httpd_req_t *req, cJSON *data)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        if (data) {
            cJSON_Delete(data);
        }
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddBoolToObject(root, "ok", true);
    if (data) {
        cJSON_AddItemToObject(root, "data", data);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);   /* also frees data since it was added as item */

    if (!json_str) {
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }

    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, json_str);
    free(json_str);
    return err;
}

esp_err_t web_json_error(httpd_req_t *req, int http_status, const char *msg)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddBoolToObject(root, "ok", false);
    cJSON_AddStringToObject(root, "error", msg ? msg : "unknown error");

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_str) {
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }

    char status_str[32];
    snprintf(status_str, sizeof(status_str), "%d", http_status);
    httpd_resp_set_status(req, status_str);
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, json_str);
    free(json_str);
    return err;
}

cJSON *web_parse_body(httpd_req_t *req)
{
    int total_len = req->content_len;
    if (total_len <= 0 || total_len > 2048) {
        ESP_LOGW(TAG, "Bad body length: %d", total_len);
        web_json_error(req, 400, "request body missing or too large");
        return NULL;
    }

    char *buf = malloc(total_len + 1);
    if (!buf) {
        web_json_error(req, 500, "out of memory");
        return NULL;
    }

    int received = 0;
    while (received < total_len) {
        int ret = httpd_req_recv(req, buf + received, total_len - received);
        if (ret <= 0) {
            ESP_LOGW(TAG, "httpd_req_recv failed: %d", ret);
            free(buf);
            web_json_error(req, 400, "failed to read request body");
            return NULL;
        }
        received += ret;
    }
    buf[total_len] = '\0';

    cJSON *json = cJSON_Parse(buf);
    free(buf);

    if (!json) {
        web_json_error(req, 400, "invalid JSON");
        return NULL;
    }

    return json;
}
