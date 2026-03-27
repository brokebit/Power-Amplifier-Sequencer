#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "wifi_sta.h"

#include "esp_partition.h"
#include "esp_spiffs.h"
#include "web_server.h"

#include "ota.h"

static const char *TAG = "ota";

#define OTA_NVS_NAMESPACE "ota_cfg"
#define NVS_KEY_REPO "repo"
#define OTA_RECV_TIMEOUT 15000
#define MAX_URL_LEN 256
#define MAX_REPO_LEN 128

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
        case ESP_OTA_IMG_NEW: return "NEW";
        case ESP_OTA_IMG_PENDING_VERIFY: return "PENDING_VERIFY";
        case ESP_OTA_IMG_VALID: return "VALID";
        case ESP_OTA_IMG_INVALID: return "INVALID";
        case ESP_OTA_IMG_ABORTED: return "ABORTED";
        case ESP_OTA_IMG_UNDEFINED: return "UNDEFINED";
        default: return "UNKNOWN";
    }
}

esp_err_t app_ota_get_status(ota_status_t *out)
{
    memset(out, 0, sizeof(*out));

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    const esp_partition_t *update = esp_ota_get_next_update_partition(NULL);

    strncpy(out->version, app_ota_get_version(), sizeof(out->version) - 1);
    strncpy(out->running_partition, running->label, sizeof(out->running_partition) - 1);
    strncpy(out->boot_partition, boot->label, sizeof(out->boot_partition) - 1);
    if (update) {
        strncpy(out->next_update_partition, update->label,
                sizeof(out->next_update_partition) - 1);
    }

    esp_ota_img_states_t state;
    esp_err_t err = esp_ota_get_state_partition(running, &state);
    if (err == ESP_OK) {
        strncpy(out->app_state, ota_state_str(state), sizeof(out->app_state) - 1);
    } else {
        strncpy(out->app_state, "unknown", sizeof(out->app_state) - 1);
    }

    const esp_partition_t *other = esp_ota_get_next_update_partition(running);
    if (other) {
        esp_app_desc_t other_desc;
        err = esp_ota_get_partition_description(other, &other_desc);
        if (err == ESP_OK) {
            strncpy(out->other_version, other_desc.version,
                    sizeof(out->other_version) - 1);
        } else {
            strncpy(out->other_version, "(empty)", sizeof(out->other_version) - 1);
        }
    }

    return ESP_OK;
}

void app_ota_print_status(void)
{
    ota_status_t status;
    app_ota_get_status(&status);

    const esp_partition_t *running = esp_ota_get_running_partition();

    printf("Firmware version: %s\n", status.version);
    printf("Running partition: %s (addr 0x%lx, size %lu KB)\n",
           running->label, (unsigned long)running->address,
           (unsigned long)(running->size / 1024));
    printf("Boot partition: %s\n", status.boot_partition);
    printf("Next update slot: %s\n",
           status.next_update_partition[0] ? status.next_update_partition : "(none)");
    printf("App state: %s\n", status.app_state);

    if (status.other_version[0]) {
        const esp_partition_t *other = esp_ota_get_next_update_partition(running);
        printf("Other slot: %s (%s)\n",
               other ? other->label : "?", status.other_version);
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

static esp_err_t build_url(const char *target, const char *filename,
                           char *url, size_t url_len)
{
    /* Full URL — use as-is (only valid for firmware, not SPIFFS) */
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
            "https://github.com/%s/releases/latest/download/%s",
            repo, filename);
    } else {
        written = snprintf(url, url_len,
            "https://github.com/%s/releases/download/%s/%s",
            repo, target, filename);
    }

    if (written < 0 || (size_t)written >= url_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

/* ---- SPIFFS partition update -------------------------------------------- */

#define SPIFFS_BUF_SIZE 1024

/**
 * Download and write a SPIFFS image to the storage partition.
 * Returns ESP_OK on success, ESP_ERR_NOT_FOUND if the server returns 404
 * (no spiffs.bin in this release), or another error on failure.
 */
static esp_err_t ota_update_spiffs(const char *url)
{
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "storage");
    if (!part) {
        ESP_LOGW(TAG, "No SPIFFS partition found — skipping filesystem update");
        return ESP_ERR_NOT_FOUND;
    }

    /* Stop web server to release the SPIFFS mount */
    web_server_stop();

    /* Unmount SPIFFS in case it was mounted outside the web server */
    esp_vfs_spiffs_unregister("storage");

    esp_http_client_config_t http_cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = OTA_RECV_TIMEOUT,
        .buffer_size = SPIFFS_BUF_SIZE,
        .buffer_size_tx = 512,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client for SPIFFS");
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);

    if (status == 404) {
        ESP_LOGI(TAG, "No SPIFFS image in release (404) — skipping filesystem update");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NOT_FOUND;
    }

    if (status != 200) {
        ESP_LOGE(TAG, "SPIFFS download HTTP %d", status);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    /* Check image fits in partition */
    if (content_length > 0 && (size_t)content_length > part->size) {
        printf("Error: SPIFFS image (%d bytes) exceeds partition size (%lu bytes)\n",
               content_length, (unsigned long)part->size);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_SIZE;
    }

    printf("Updating SPIFFS filesystem...\n");
    if (content_length > 0) {
        printf("  Image size: %d bytes, partition size: %lu bytes\n",
               content_length, (unsigned long)part->size);
    }

    /* Erase the partition */
    err = esp_partition_erase_range(part, 0, part->size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Partition erase failed: %s", esp_err_to_name(err));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return err;
    }

    /* Stream and write */
    char *buf = malloc(SPIFFS_BUF_SIZE);
    if (!buf) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    size_t offset = 0;
    int last_progress = -1;

    while (1) {
        int read_len = esp_http_client_read(client, buf, SPIFFS_BUF_SIZE);
        if (read_len < 0) {
            ESP_LOGE(TAG, "SPIFFS download read error");
            err = ESP_FAIL;
            break;
        }
        if (read_len == 0) {
            break; /* download complete */
        }

        if (offset + read_len > part->size) {
            printf("Error: SPIFFS image exceeds partition size\n");
            err = ESP_ERR_INVALID_SIZE;
            break;
        }

        err = esp_partition_write(part, offset, buf, read_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Partition write failed at offset %u: %s",
                     (unsigned)offset, esp_err_to_name(err));
            break;
        }

        offset += read_len;

        if (content_length > 0) {
            int progress = (int)((offset * 100) / content_length);
            if (progress / 10 != last_progress / 10) {
                printf("  SPIFFS: %d%% (%u / %d bytes)\n",
                       progress, (unsigned)offset, content_length);
                last_progress = progress;
            }
        }
    }

    free(buf);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (err == ESP_OK) {
        printf("  SPIFFS update complete (%u bytes written)\n", (unsigned)offset);
    }
    return err;
}

/* ---- OTA update --------------------------------------------------------- */

esp_err_t app_ota_update(const char *target)
{
    if (!app_wifi_is_connected()) {
        printf("Error: WiFi not connected. Connect first with 'wifi connect'\n");
        return ESP_ERR_INVALID_STATE;
    }

    char url[MAX_URL_LEN];

    /* ---- Step 1: Update SPIFFS (optional) ---- */
    bool is_direct_url = (strncmp(target, "http", 4) == 0);
    if (!is_direct_url) {
        esp_err_t spiffs_err = build_url(target, "spiffs.bin", url, sizeof(url));
        if (spiffs_err == ESP_ERR_NVS_NOT_FOUND) {
            printf("Error: No GitHub repo configured. Set with 'ota repo owner/repo'\n");
            return spiffs_err;
        }
        if (spiffs_err == ESP_OK) {
            printf("Checking for SPIFFS update: %s\n", url);
            spiffs_err = ota_update_spiffs(url);
            if (spiffs_err != ESP_OK && spiffs_err != ESP_ERR_NOT_FOUND) {
                printf("Error: SPIFFS update failed: %s\n", esp_err_to_name(spiffs_err));
                return spiffs_err;
            }
        }
    } else {
        printf("Direct URL target — skipping SPIFFS update\n");
    }

    /* ---- Step 2: Update firmware ---- */
    esp_err_t err = build_url(target, "firmware.bin", url, sizeof(url));
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        printf("Error: No GitHub repo configured. Set with 'ota repo owner/repo'\n");
        return err;
    }
    if (err != ESP_OK) {
        printf("Error: Failed to build URL: %s\n", esp_err_to_name(err));
        return err;
    }

    printf("Firmware update from: %s\n", url);
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
