#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"

#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ota.h"

#include "web_json.h"

/* ---- GET /api/ota/status ------------------------------------------------ */

static esp_err_t api_ota_status_handler(httpd_req_t *req)
{
    ota_status_t status;
    esp_err_t err = app_ota_get_status(&status);
    if (err != ESP_OK) {
        return web_json_error(req, 500, "failed to get OTA status");
    }

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "version", status.version);
    cJSON_AddStringToObject(data, "running_partition", status.running_partition);
    cJSON_AddStringToObject(data, "boot_partition", status.boot_partition);
    cJSON_AddStringToObject(data, "next_update_partition", status.next_update_partition);
    cJSON_AddStringToObject(data, "app_state", status.app_state);
    cJSON_AddStringToObject(data, "other_version", status.other_version);

    return web_json_ok(req, data);
}

/* ---- GET /api/ota/repo -------------------------------------------------- */

static esp_err_t api_ota_repo_get_handler(httpd_req_t *req)
{
    char repo[128];
    esp_err_t err = app_ota_get_repo(repo, sizeof(repo));

    cJSON *data = cJSON_CreateObject();
    if (err == ESP_OK) {
        cJSON_AddStringToObject(data, "repo", repo);
    } else {
        cJSON_AddStringToObject(data, "repo", "");
    }
    return web_json_ok(req, data);
}

/* ---- POST /api/ota/repo ------------------------------------------------- */

static esp_err_t api_ota_repo_set_handler(httpd_req_t *req)
{
    cJSON *body = web_parse_body(req);
    if (!body) {
        return ESP_OK;
    }

    cJSON *repo_json = cJSON_GetObjectItem(body, "repo");
    if (!repo_json || !cJSON_IsString(repo_json)) {
        cJSON_Delete(body);
        return web_json_error(req, 400, "missing 'repo' string field");
    }

    esp_err_t err = app_ota_set_repo(repo_json->valuestring);
    cJSON_Delete(body);

    if (err == ESP_ERR_INVALID_ARG) {
        return web_json_error(req, 400, "repo must be in 'owner/repo' format");
    }
    if (err != ESP_OK) {
        return web_json_error(req, 500, "failed to save repo");
    }
    return web_json_ok(req, NULL);
}

/* ---- POST /api/ota/update (async) --------------------------------------- */

static void ota_update_task(void *arg)
{
    char *target = (char *)arg;
    app_ota_update(target);  /* blocks; reboots on success */
    /* If we get here, the update failed */
    free(target);
    vTaskDelete(NULL);
}

static esp_err_t api_ota_update_handler(httpd_req_t *req)
{
    cJSON *body = web_parse_body(req);
    if (!body) {
        return ESP_OK;
    }

    cJSON *target_json = cJSON_GetObjectItem(body, "target");
    if (!target_json || !cJSON_IsString(target_json)) {
        cJSON_Delete(body);
        return web_json_error(req, 400, "missing 'target' string field");
    }

    char *target_copy = strdup(target_json->valuestring);
    cJSON_Delete(body);

    if (!target_copy) {
        return web_json_error(req, 500, "out of memory");
    }

    /* Launch OTA in a background task so we can respond immediately */
    BaseType_t ret = xTaskCreate(ota_update_task, "ota_web", 8192,
                                 target_copy, 5, NULL);
    if (ret != pdPASS) {
        free(target_copy);
        return web_json_error(req, 500, "failed to start OTA task");
    }

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "status", "started");
    return web_json_ok(req, data);
}

/* ---- POST /api/ota/rollback --------------------------------------------- */

static esp_err_t api_ota_rollback_handler(httpd_req_t *req)
{
    /* Send response before rollback reboots */
    web_json_ok(req, NULL);
    app_ota_rollback();  /* reboots on success */
    return ESP_OK;
}

/* ---- POST /api/ota/validate --------------------------------------------- */

static esp_err_t api_ota_validate_handler(httpd_req_t *req)
{
    esp_err_t err = app_ota_validate();
    if (err != ESP_OK) {
        return web_json_error(req, 500, "validation failed");
    }
    return web_json_ok(req, NULL);
}

/* ---- registration ------------------------------------------------------- */

void web_register_api_ota(httpd_handle_t server)
{
    const httpd_uri_t status_uri = {
        .uri ="/api/ota/status",
        .method =HTTP_GET,
        .handler = api_ota_status_handler,
    };
    httpd_register_uri_handler(server, &status_uri);

    const httpd_uri_t repo_get_uri = {
        .uri ="/api/ota/repo",
        .method =HTTP_GET,
        .handler = api_ota_repo_get_handler,
    };
    httpd_register_uri_handler(server, &repo_get_uri);

    const httpd_uri_t repo_set_uri = {
        .uri ="/api/ota/repo",
        .method =HTTP_POST,
        .handler = api_ota_repo_set_handler,
    };
    httpd_register_uri_handler(server, &repo_set_uri);

    const httpd_uri_t update_uri = {
        .uri ="/api/ota/update",
        .method =HTTP_POST,
        .handler = api_ota_update_handler,
    };
    httpd_register_uri_handler(server, &update_uri);

    const httpd_uri_t rollback_uri = {
        .uri ="/api/ota/rollback",
        .method =HTTP_POST,
        .handler = api_ota_rollback_handler,
    };
    httpd_register_uri_handler(server, &rollback_uri);

    const httpd_uri_t validate_uri = {
        .uri ="/api/ota/validate",
        .method =HTTP_POST,
        .handler = api_ota_validate_handler,
    };
    httpd_register_uri_handler(server, &validate_uri);
}
