#pragma once

#include "cJSON.h"
#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Send a success response: {"ok": true, "data": <data>}
 * Takes ownership of data — frees it after sending.
 * If data is NULL, sends {"ok": true}.
 */
esp_err_t web_json_ok(httpd_req_t *req, cJSON *data);

/**
 * Send an error response with the given HTTP status code.
 * Body: {"ok": false, "error": "<msg>"}
 */
esp_err_t web_json_error(httpd_req_t *req, int http_status, const char *msg);

/**
 * Parse JSON body from the request.
 * Returns cJSON object on success (caller must cJSON_Delete).
 * Returns NULL on parse error or body too large (>2048 bytes).
 * Sends a 400 error response automatically on failure.
 */
cJSON *web_parse_body(httpd_req_t *req);

/**
 * Build the full system state JSON object (sequencer, relays, ADC, etc.).
 * Caller must cJSON_Delete the returned object.
 */
cJSON *web_build_state_json(void);

#ifdef __cplusplus
}
#endif
