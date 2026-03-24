#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "nvs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ota.h"
#include "wifi_sta.h"

static const char *TAG = "ota";

#define OTA_NVS_NAMESPACE  "ota_cfg"
#define NVS_KEY_REPO       "repo"
#define OTA_RECV_TIMEOUT   15000
#define MAX_URL_LEN        256
#define MAX_REPO_LEN       128

/* ---- boot validation ---------------------------------------------------- */

esp_err_t app_ota_init(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    esp_err_t err = esp_ota_get_state_partition(running, &state);

    if (err == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "First boot after OTA — marking firmware valid");
        err = esp_ota_mark_app_valid_cancel_rollback();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to validate app: %s", esp_err_to_name(err));
        }
    } else if (err == ESP_ERR_NOT_SUPPORTED) {
        /* Running from a partition without OTA state — normal on first flash */
        err = ESP_OK;
    }

    return err;
}

/* ---- version ------------------------------------------------------------ */

const char *app_ota_get_version(void)
{
    return esp_app_get_description()->version;
}

/* ---- status ------------------------------------------------------------- */

static const char *ota_state_str(esp_ota_img_states_t state)
{
    switch (state) {
    case ESP_OTA_IMG_NEW:            return "NEW";
    case ESP_OTA_IMG_PENDING_VERIFY: return "PENDING_VERIFY";
    case ESP_OTA_IMG_VALID:          return "VALID";
    case ESP_OTA_IMG_INVALID:        return "INVALID";
    case ESP_OTA_IMG_ABORTED:        return "ABORTED";
    case ESP_OTA_IMG_UNDEFINED:      return "UNDEFINED";
    default:                         return "UNKNOWN";
    }
}

void app_ota_print_status(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    const esp_partition_t *update = esp_ota_get_next_update_partition(NULL);

    printf("Firmware version: %s\n", app_ota_get_version());
    printf("Running partition: %s (addr 0x%lx, size %lu KB)\n",
           running->label, (unsigned long)running->address,
           (unsigned long)(running->size / 1024));
    printf("Boot partition:    %s\n", boot->label);
    printf("Next update slot:  %s\n", update ? update->label : "(none)");

    esp_ota_img_states_t state;
    esp_err_t err = esp_ota_get_state_partition(running, &state);
    if (err == ESP_OK) {
        printf("App state:         %s\n", ota_state_str(state));
    }

    /* Show the other slot's info */
    const esp_partition_t *other = esp_ota_get_next_update_partition(running);
    if (other) {
        esp_app_desc_t other_desc;
        err = esp_ota_get_partition_description(other, &other_desc);
        if (err == ESP_OK) {
            printf("Other slot:        %s (version %s)\n",
                   other->label, other_desc.version);
        } else {
            printf("Other slot:        %s (empty)\n", other->label);
        }
    }
}

/* ---- NVS repo management ------------------------------------------------ */

esp_err_t app_ota_set_repo(const char *repo)
{
    if (!repo || strlen(repo) == 0 || strlen(repo) >= MAX_REPO_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Must contain exactly one '/' */
    const char *slash = strchr(repo, '/');
    if (!slash || slash == repo || *(slash + 1) == '\0'
        || strchr(slash + 1, '/') != NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(OTA_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(h, NVS_KEY_REPO, repo);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t app_ota_get_repo(char *buf, size_t buf_len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(OTA_NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_str(h, NVS_KEY_REPO, buf, &buf_len);
    nvs_close(h);
    return err;
}

/* ---- URL construction --------------------------------------------------- */

static esp_err_t build_url(const char *target, char *url, size_t url_len)
{
    /* Full URL — use as-is */
    if (strncmp(target, "http", 4) == 0) {
        if (strlen(target) >= url_len) {
            return ESP_ERR_INVALID_SIZE;
        }
        strncpy(url, target, url_len);
        url[url_len - 1] = '\0';
        return ESP_OK;
    }

    /* Version tag — resolve via stored GitHub repo */
    char repo[MAX_REPO_LEN];
    esp_err_t err = app_ota_get_repo(repo, sizeof(repo));
    if (err != ESP_OK) {
        return err;
    }

    int written;
    if (strcmp(target, "latest") == 0) {
        written = snprintf(url, url_len,
            "https://github.com/%s/releases/latest/download/firmware.bin",
            repo);
    } else {
        written = snprintf(url, url_len,
            "https://github.com/%s/releases/download/%s/firmware.bin",
            repo, target);
    }

    if (written < 0 || (size_t)written >= url_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

/* ---- OTA update --------------------------------------------------------- */

esp_err_t app_ota_update(const char *target)
{
    if (!app_wifi_is_connected()) {
        printf("Error: WiFi not connected. Connect first with 'wifi connect'\n");
        return ESP_ERR_INVALID_STATE;
    }

    char url[MAX_URL_LEN];
    esp_err_t err = build_url(target, url, sizeof(url));
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        printf("Error: No GitHub repo configured. Set with 'ota repo owner/repo'\n");
        return err;
    }
    if (err != ESP_OK) {
        printf("Error: Failed to build URL: %s\n", esp_err_to_name(err));
        return err;
    }

    printf("OTA update from: %s\n", url);
    printf("Starting download...\n");

    /* Enable logging temporarily so TLS/HTTP errors are visible */
    esp_log_level_set("esp_https_ota", ESP_LOG_INFO);
    esp_log_level_set("esp_http_client", ESP_LOG_INFO);
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_INFO);

    esp_http_client_config_t http_cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = OTA_RECV_TIMEOUT,
        .keep_alive_enable = true,
        .buffer_size = 1024,       /* receive buffer */
        .buffer_size_tx = 1024,    /* transmit buffer */
    };

    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    esp_https_ota_handle_t ota_handle = NULL;
    err = esp_https_ota_begin(&ota_cfg, &ota_handle);
    if (err != ESP_OK) {
        printf("Error: OTA begin failed: %s\n", esp_err_to_name(err));
        return err;
    }

    int image_size = esp_https_ota_get_image_size(ota_handle);
    int last_progress = -1;

    while (1) {
        err = esp_https_ota_perform(ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }

        int downloaded = esp_https_ota_get_image_len_read(ota_handle);
        if (image_size > 0) {
            int progress = (downloaded * 100) / image_size;
            if (progress / 10 != last_progress / 10) {
                printf("  Progress: %d%% (%d / %d bytes)\n",
                       progress, downloaded, image_size);
                last_progress = progress;
            }
        } else {
            /* No Content-Length; print every 100KB */
            if (downloaded > 0 && (downloaded / (100 * 1024)) !=
                ((downloaded - 1) / (100 * 1024))) {
                printf("  Downloaded: %d KB\n", downloaded / 1024);
            }
        }
    }

    if (err != ESP_OK) {
        printf("Error: OTA download failed: %s\n", esp_err_to_name(err));
        esp_https_ota_abort(ota_handle);
        return err;
    }

    if (!esp_https_ota_is_complete_data_received(ota_handle)) {
        printf("Error: Incomplete image received\n");
        esp_https_ota_abort(ota_handle);
        return ESP_ERR_INVALID_SIZE;
    }

    err = esp_https_ota_finish(ota_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            printf("Error: Firmware image validation failed\n");
        } else {
            printf("Error: OTA finish failed: %s\n", esp_err_to_name(err));
        }
        return err;
    }

    printf("OTA update successful! Rebooting in 2 seconds...\n");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    /* Never reached */
    return ESP_OK;
}

/* ---- rollback & validate ------------------------------------------------ */

esp_err_t app_ota_rollback(void)
{
    const esp_partition_t *other = esp_ota_get_next_update_partition(NULL);
    if (!other) {
        printf("Error: No other partition available for rollback\n");
        return ESP_ERR_NOT_FOUND;
    }

    esp_app_desc_t desc;
    esp_err_t err = esp_ota_get_partition_description(other, &desc);
    if (err != ESP_OK) {
        printf("Error: Other partition (%s) has no valid firmware\n",
               other->label);
        return err;
    }

    printf("Rolling back to %s (version %s)...\n", other->label, desc.version);
    err = esp_ota_mark_app_invalid_rollback_and_reboot();
    if (err != ESP_OK) {
        printf("Error: Rollback failed: %s\n", esp_err_to_name(err));
    }
    /* Does not return on success — reboots */
    return err;
}

esp_err_t app_ota_validate(void)
{
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        printf("Firmware marked as valid\n");
    } else if (err == ESP_ERR_NOT_SUPPORTED) {
        printf("Not in OTA verification state — nothing to validate\n");
        err = ESP_OK;
    } else {
        printf("Error: %s\n", esp_err_to_name(err));
    }
    return err;
}
