#pragma once

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Validate the running firmware on boot.
 *
 * If the bootloader marked the app as pending verification
 * (after an OTA update), this confirms it as valid.  Must be
 * called from app_main() after all critical init succeeds.
 * If init crashes before reaching this call, the watchdog
 * triggers and the bootloader rolls back automatically.
 *
 * Safe to call on every boot -- returns ESP_OK with no
 * effect if the app is already validated.
 */
esp_err_t app_ota_init(void);

/**
 * Get the firmware version string from the app descriptor.
 * Returns a pointer to a static string -- do not free.
 */
const char *app_ota_get_version(void);

/**
 * Print OTA status: running partition, version, rollback state.
 */
void app_ota_print_status(void);

/**
 * Structured OTA status for programmatic access.
 */
typedef struct {
    char version[32];
    char running_partition[16];
    char boot_partition[16];
    char next_update_partition[16];
    char app_state[24];
    char other_version[32];
} ota_status_t;

/**
 * Fill status struct with current OTA information.
 */
esp_err_t app_ota_get_status(ota_status_t *out);

/**
 * Store the GitHub repo identifier (e.g. "owner/repo") in NVS.
 */
esp_err_t app_ota_set_repo(const char *repo);

/**
 * Get the stored GitHub repo identifier.
 * Writes into buf.  Returns ESP_ERR_NVS_NOT_FOUND if not set.
 */
esp_err_t app_ota_get_repo(char *buf, size_t buf_len);

/**
 * Perform an OTA update.
 *
 * target can be:
 *   - A version tag like "v1.2.0" or "latest" (resolved via
 *     the stored GitHub repo)
 *   - A full https:// URL (used as-is)
 *
 * Requires WiFi to be connected.  Prints download progress.
 * On success, reboots the device (does not return).
 * Returns an error code on failure.
 */
esp_err_t app_ota_update(const char *target);

/**
 * Roll back to the previously running firmware and reboot.
 * Returns an error if no previous firmware is available.
 */
esp_err_t app_ota_rollback(void);

/**
 * Manually mark the running firmware as valid.
 * Normally handled automatically by app_ota_init(), but
 * exposed for manual override via CLI.
 */
esp_err_t app_ota_validate(void);

#ifdef __cplusplus
}
#endif
