#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_spiffs.h"

#include "web_server.h"
#include "web_ws.h"

static const char *TAG = "web_server";

static httpd_handle_t s_server = NULL;
static app_config_t *s_cfg = NULL;

app_config_t *web_get_config(void)
{
    return s_cfg;
}

/* ---- Forward declarations for api_*.c register functions ---------------- */
void web_register_api_state(httpd_handle_t server);
void web_register_api_config(httpd_handle_t server);
void web_register_api_relay(httpd_handle_t server);
void web_register_api_fault(httpd_handle_t server);
void web_register_api_seq(httpd_handle_t server);
void web_register_api_adc(httpd_handle_t server);
void web_register_api_wifi(httpd_handle_t server);
void web_register_api_ota(httpd_handle_t server);
void web_register_api_system(httpd_handle_t server);
void web_register_api_static(httpd_handle_t server);

/* ---- SPIFFS ------------------------------------------------------------- */

static esp_err_t mount_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/www",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = true,
    };

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SPIFFS mount failed: %s (API endpoints still available)",
                 esp_err_to_name(err));
        return err;
    }

    size_t total = 0, used = 0;
    esp_spiffs_info("storage", &total, &used);
    ESP_LOGI(TAG, "SPIFFS mounted: %u KB total, %u KB used",
             (unsigned)(total / 1024), (unsigned)(used / 1024));
    return ESP_OK;
}

/* ---- WebSocket URI ------------------------------------------------------ */

static const httpd_uri_t ws_uri = {
    .uri = "/ws",
    .method = HTTP_GET,
    .handler = ws_handler,
    .is_websocket = true,
};

/* ---- public API --------------------------------------------------------- */

esp_err_t web_server_init(app_config_t *cfg)
{
    s_cfg = cfg;

    /* Mount SPIFFS — non-fatal if it fails */
    mount_spiffs();

    /* Start HTTP server */
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 6144;
    config.server_port = 80;
    config.max_uri_handlers = 40;
    config.max_open_sockets = 7;   /* 3 WS + 4 HTTP to avoid LRU closing WS */
    config.task_priority = 5;
    config.lru_purge_enable = false; /* Disable: LRU can close WS sockets mid-push */
    config.close_fn = ws_close_fd; /* Clean up WS client list on socket close */

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Register API endpoints */
    web_register_api_state(s_server);
    web_register_api_config(s_server);
    web_register_api_relay(s_server);
    web_register_api_fault(s_server);
    web_register_api_seq(s_server);
    web_register_api_adc(s_server);
    web_register_api_wifi(s_server);
    web_register_api_ota(s_server);
    web_register_api_system(s_server);

    /* WebSocket */
    httpd_register_uri_handler(s_server, &ws_uri);
    ws_init(s_server);

    /* Static files — must be registered last (wildcard catch-all) */
    web_register_api_static(s_server);

    ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
    return ESP_OK;
}

esp_err_t web_server_stop(void)
{
    if (!s_server) {
        return ESP_OK;
    }

    ws_stop();
    httpd_stop(s_server);
    s_server = NULL;

    esp_vfs_spiffs_unregister("storage");
    ESP_LOGI(TAG, "HTTP server stopped");
    return ESP_OK;
}
