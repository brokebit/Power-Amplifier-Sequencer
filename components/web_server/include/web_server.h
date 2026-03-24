#pragma once

#include "esp_err.h"

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise and start the HTTP server with REST API,
 * WebSocket state push, and SPIFFS static file serving.
 *
 * cfg: pointer to the live app_config_t owned by main.
 *      The web server reads and modifies this struct in-place.
 *      Caller must ensure it outlives the server.
 *
 * Must be called after app_wifi_init().
 */
esp_err_t web_server_init(app_config_t *cfg);

/**
 * Stop the HTTP server, WebSocket push task, and unmount SPIFFS.
 * Safe to call if the server is not running (no-op).
 */
esp_err_t web_server_stop(void);

/**
 * Return the live config pointer passed to web_server_init().
 * Used internally by API handlers.
 */
app_config_t *web_get_config(void);

#ifdef __cplusplus
}
#endif
