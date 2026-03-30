#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise WiFi subsystem: event loop, netif, driver.
 * If credentials exist in NVS and WiFi is enabled,
 * begins connection attempt automatically.
 * Must be called after nvs_flash_init() (i.e., after config_init()).
 */
esp_err_t app_wifi_init(void);

/**
 * Save SSID and password to NVS.
 * Does NOT automatically connect — call app_wifi_connect() after.
 * ssid: null-terminated, max 32 chars.
 * pass: null-terminated, max 64 chars (empty string for open networks).
 */
esp_err_t app_wifi_set_credentials(const char *ssid, const char *pass);

/**
 * Connect using saved NVS credentials.
 * Returns ESP_ERR_NOT_FOUND if no credentials saved.
 */
esp_err_t app_wifi_connect(void);

/**
 * Disconnect from current AP. Does not erase credentials.
 */
esp_err_t app_wifi_disconnect(void);

/**
 * Erase saved credentials from NVS and disconnect.
 */
esp_err_t app_wifi_erase_credentials(void);

/**
 * Return true if connected and has an IP address.
 */
bool app_wifi_is_connected(void);

/**
 * Write current IP as dotted-quad string into buf.
 * Returns ESP_ERR_INVALID_STATE if not connected.
 */
esp_err_t app_wifi_get_ip_str(char *buf, size_t buf_len);

/**
 * Get current RSSI.
 * Returns ESP_ERR_INVALID_STATE if not connected.
 */
esp_err_t app_wifi_get_rssi(int8_t *rssi);

/**
 * Blocking WiFi scan — prints results table to stdout.
 */
esp_err_t app_wifi_scan(void);

/**
 * Structured scan result for programmatic access.
 */
typedef struct {
    char    ssid[33];
    int8_t  rssi;
    uint8_t channel;
    uint8_t authmode;  /* wifi_auth_mode_t cast to uint8 */
} wifi_scan_result_t;

/**
 * Blocking WiFi scan returning structured results.
 * Caller must free(*results) when done.
 * *out_count is set to the number of results.
 * Returns ESP_OK on success.
 */
esp_err_t app_wifi_scan_results(wifi_scan_result_t **results, uint16_t *out_count);

/**
 * Enable or disable auto-connect on boot. Persisted to NVS.
 */
esp_err_t app_wifi_set_enabled(bool enabled);

/**
 * Return the current enabled state (from NVS).
 */
bool app_wifi_get_enabled(void);

#ifdef __cplusplus
}
#endif
