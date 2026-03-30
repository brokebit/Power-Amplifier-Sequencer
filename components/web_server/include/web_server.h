#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise and start the HTTP server with REST API,
 * WebSocket state push, and SPIFFS static file serving.
 *
 * Must be called after config_init() and app_wifi_init().
 */
esp_err_t web_server_init(void);

/**
 * Stop the HTTP server, WebSocket push task, and unmount SPIFFS.
 * Safe to call if the server is not running (no-op).
 */
esp_err_t web_server_stop(void);

#ifdef __cplusplus
}
#endif
