#include <string.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "web_json.h"

/* ---- POST /api/reboot --------------------------------------------------- */

static void reboot_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}

static esp_err_t api_reboot_handler(httpd_req_t *req)
{
    /* Respond before rebooting */
    web_json_ok(req, NULL);
    xTaskCreate(reboot_task, "reboot", 2048, NULL, 5, NULL);
    return ESP_OK;
}

/* ---- POST /api/log ------------------------------------------------------ */

static esp_err_t api_log_handler(httpd_req_t *req)
{
    cJSON *body = web_parse_body(req);
    if (!body) {
        return ESP_OK;
    }

    cJSON *level_json = cJSON_GetObjectItem(body, "level");
    if (!level_json || !cJSON_IsString(level_json)) {
        cJSON_Delete(body);
        return web_json_error(req, 400, "missing 'level' string field");
    }

    const char *level_str = level_json->valuestring;
    esp_log_level_t level;
    if (strcmp(level_str, "off") == 0 || strcmp(level_str, "none") == 0) {
        level = ESP_LOG_NONE;
    } else if (strcmp(level_str, "error") == 0) {
        level = ESP_LOG_ERROR;
    } else if (strcmp(level_str, "warn") == 0) {
        level = ESP_LOG_WARN;
    } else if (strcmp(level_str, "info") == 0) {
        level = ESP_LOG_INFO;
    } else if (strcmp(level_str, "debug") == 0) {
        level = ESP_LOG_DEBUG;
    } else if (strcmp(level_str, "verbose") == 0) {
        level = ESP_LOG_VERBOSE;
    } else {
        cJSON_Delete(body);
        return web_json_error(req, 400,
            "level must be: off, error, warn, info, debug, or verbose");
    }

    const char *tag = "*";
    cJSON *tag_json = cJSON_GetObjectItem(body, "tag");
    if (tag_json && cJSON_IsString(tag_json)) {
        tag = tag_json->valuestring;
    }

    esp_log_level_set(tag, level);
    cJSON_Delete(body);

    return web_json_ok(req, NULL);
}

/* ---- registration ------------------------------------------------------- */

void web_register_api_system(httpd_handle_t server)
{
    const httpd_uri_t reboot_uri = {
        .uri     = "/api/reboot",
        .method  = HTTP_POST,
        .handler = api_reboot_handler,
    };
    httpd_register_uri_handler(server, &reboot_uri);

    const httpd_uri_t log_uri = {
        .uri     = "/api/log",
        .method  = HTTP_POST,
        .handler = api_log_handler,
    };
    httpd_register_uri_handler(server, &log_uri);
}
