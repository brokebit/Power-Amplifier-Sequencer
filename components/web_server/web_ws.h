#pragma once

#include "esp_http_server.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WS_MAX_CLIENTS 3

/**
 * Initialise WebSocket client tracking and start the push task.
 * Must be called after httpd is started.
 */
void ws_init(httpd_handle_t server);

/**
 * Stop the push task. Called during server shutdown.
 */
void ws_stop(void);

/**
 * Add a client socket fd to the tracking list.
 * Called from the WebSocket open handler.
 */
void ws_add_client(int fd);

/**
 * Remove a client socket fd from the tracking list.
 * Called on close or send error.
 */
void ws_remove_client(int fd);

/**
 * WebSocket URI handler — handles open/close and incoming frames.
 */
esp_err_t ws_handler(httpd_req_t *req);

/**
 * Socket close callback — registered with httpd to clean up WS clients
 * when their socket is closed for any reason (disconnect, LRU purge, etc.).
 */
void ws_close_fd(httpd_handle_t hd, int fd);

#ifdef __cplusplus
}
#endif
