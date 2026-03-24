#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"

#include "nvs.h"
#include "system_state.h"

#include "wifi_sta.h"

static const char *TAG = "wifi_sta";

/* NVS namespace and keys */
#define WIFI_NVS_NAMESPACE "wifi_cfg"
#define NVS_KEY_SSID       "ssid"
#define NVS_KEY_PASS       "pass"
#define NVS_KEY_ENABLED    "enabled"

/* Retry backoff */
#define RETRY_INITIAL_MS    1000
#define RETRY_MAX_MS        30000
#define RETRY_BACKOFF       2

/* Internal state */
static esp_netif_t *s_netif = NULL;
static EventGroupHandle_t s_wifi_eg = NULL;
static TimerHandle_t s_retry_timer = NULL;
static uint32_t s_retry_ms = RETRY_INITIAL_MS;
static bool s_connecting = false;
static uint32_t s_ip_addr = 0;

#define CONNECTED_BIT  BIT0

/* ---- helpers ------------------------------------------------------------ */

static bool load_credentials(wifi_config_t *wifi_cfg)
{
    nvs_handle_t h;
    if (nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }

    size_t len = sizeof(wifi_cfg->sta.ssid);
    esp_err_t err = nvs_get_str(h, NVS_KEY_SSID, (char *)wifi_cfg->sta.ssid, &len);
    if (err != ESP_OK || len <= 1) {
        nvs_close(h);
        return false;
    }

    len = sizeof(wifi_cfg->sta.password);
    err = nvs_get_str(h, NVS_KEY_PASS, (char *)wifi_cfg->sta.password, &len);
    if (err != ESP_OK) {
        wifi_cfg->sta.password[0] = '\0';
    }

    nvs_close(h);
    return true;
}

/* ---- retry timer -------------------------------------------------------- */

static void retry_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    ESP_LOGI(TAG, "Retrying connection (backoff %lu ms)...", (unsigned long)s_retry_ms);
    esp_wifi_connect();
}

/* ---- event handler ------------------------------------------------------ */

static void event_handler(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    (void)arg;

    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            if (s_connecting) {
                esp_wifi_connect();
            }
            break;

        case WIFI_EVENT_STA_CONNECTED:
            s_retry_ms = RETRY_INITIAL_MS;
            ESP_LOGI(TAG, "Associated with AP");
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            s_ip_addr = 0;
            system_state_set_wifi(false, 0, 0);
            xEventGroupClearBits(s_wifi_eg, CONNECTED_BIT);

            if (s_connecting && s_retry_timer) {
                xTimerChangePeriod(s_retry_timer,
                                   pdMS_TO_TICKS(s_retry_ms), 0);
                xTimerStart(s_retry_timer, 0);
                uint32_t next = s_retry_ms * RETRY_BACKOFF;
                s_retry_ms = (next > RETRY_MAX_MS) ? RETRY_MAX_MS : next;
            }
            break;

        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        s_ip_addr = event->ip_info.ip.addr;

        int8_t rssi = 0;
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            rssi = ap.rssi;
        }

        system_state_set_wifi(true, s_ip_addr, rssi);
        xEventGroupSetBits(s_wifi_eg, CONNECTED_BIT);

        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

/* ---- public API --------------------------------------------------------- */

esp_err_t app_wifi_init(void)
{
    s_wifi_eg = xEventGroupCreate();

    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    ESP_ERROR_CHECK(esp_netif_init());

    s_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    s_retry_timer = xTimerCreate("wifi_retry", pdMS_TO_TICKS(RETRY_INITIAL_MS),
                                  pdFALSE, NULL, retry_timer_cb);

    if (app_wifi_get_enabled()) {
        wifi_config_t wifi_cfg = {0};
        if (load_credentials(&wifi_cfg)) {
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
            s_connecting = true;
            ESP_ERROR_CHECK(esp_wifi_start());
            ESP_LOGI(TAG, "Auto-connecting to '%s'", wifi_cfg.sta.ssid);
        } else {
            ESP_LOGI(TAG, "No WiFi credentials saved");
            ESP_ERROR_CHECK(esp_wifi_start());
        }
    } else {
        ESP_LOGI(TAG, "WiFi auto-connect disabled");
        ESP_ERROR_CHECK(esp_wifi_start());
    }

    return ESP_OK;
}

esp_err_t app_wifi_set_credentials(const char *ssid, const char *pass)
{
    if (!ssid || strlen(ssid) == 0 || strlen(ssid) > 32) {
        return ESP_ERR_INVALID_ARG;
    }
    if (pass && strlen(pass) > 64) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(h, NVS_KEY_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(h, NVS_KEY_PASS, pass ? pass : "");
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }

    nvs_close(h);
    return err;
}

esp_err_t app_wifi_connect(void)
{
    wifi_config_t wifi_cfg = {0};
    if (!load_credentials(&wifi_cfg)) {
        return ESP_ERR_NOT_FOUND;
    }

    s_retry_ms = RETRY_INITIAL_MS;
    s_connecting = true;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    return esp_wifi_connect();
}

esp_err_t app_wifi_disconnect(void)
{
    s_connecting = false;
    if (s_retry_timer) {
        xTimerStop(s_retry_timer, 0);
    }
    return esp_wifi_disconnect();
}

esp_err_t app_wifi_erase_credentials(void)
{
    app_wifi_disconnect();

    nvs_handle_t h;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }

    nvs_erase_key(h, NVS_KEY_SSID);
    nvs_erase_key(h, NVS_KEY_PASS);
    err = nvs_commit(h);

    nvs_close(h);
    return err;
}

bool app_wifi_is_connected(void)
{
    if (!s_wifi_eg) {
        return false;
    }
    return (xEventGroupGetBits(s_wifi_eg) & CONNECTED_BIT) != 0;
}

esp_err_t app_wifi_get_ip_str(char *buf, size_t buf_len)
{
    if (!app_wifi_is_connected() || s_ip_addr == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    snprintf(buf, buf_len, IPSTR, IP2STR((esp_ip4_addr_t *)&s_ip_addr));
    return ESP_OK;
}

esp_err_t app_wifi_get_rssi(int8_t *rssi)
{
    if (!app_wifi_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }
    wifi_ap_record_t ap;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap);
    if (err == ESP_OK) {
        *rssi = ap.rssi;
    }
    return err;
}

esp_err_t app_wifi_scan_results(wifi_scan_result_t **results, uint16_t *out_count)
{
    *results = NULL;
    *out_count = 0;

    wifi_scan_config_t scan_cfg = {
        .show_hidden = false,
        .scan_type   = WIFI_SCAN_TYPE_ACTIVE,
    };

    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK) {
        return err;
    }

    uint16_t count = 0;
    esp_wifi_scan_get_ap_num(&count);

    if (count == 0) {
        return ESP_OK;
    }

    wifi_ap_record_t *records = malloc(count * sizeof(wifi_ap_record_t));
    if (!records) {
        return ESP_ERR_NO_MEM;
    }
    esp_wifi_scan_get_ap_records(&count, records);

    wifi_scan_result_t *out = malloc(count * sizeof(wifi_scan_result_t));
    if (!out) {
        free(records);
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0; i < count; i++) {
        strncpy(out[i].ssid, (char *)records[i].ssid, sizeof(out[i].ssid) - 1);
        out[i].ssid[sizeof(out[i].ssid) - 1] = '\0';
        out[i].rssi     = records[i].rssi;
        out[i].channel  = records[i].primary;
        out[i].authmode = (uint8_t)records[i].authmode;
    }

    free(records);
    *results = out;
    *out_count = count;
    return ESP_OK;
}

esp_err_t app_wifi_scan(void)
{
    wifi_scan_result_t *results = NULL;
    uint16_t count = 0;

    esp_err_t err = app_wifi_scan_results(&results, &count);
    if (err != ESP_OK) {
        return err;
    }

    if (count == 0) {
        printf("No networks found\n");
        return ESP_OK;
    }

    printf("%-32s  RSSI  Ch  Security\n", "SSID");
    printf("%-32s  ----  --  --------\n", "----");
    for (int i = 0; i < count; i++) {
        const char *auth;
        switch (results[i].authmode) {
        case WIFI_AUTH_OPEN:          auth = "Open";   break;
        case WIFI_AUTH_WPA_PSK:       auth = "WPA";    break;
        case WIFI_AUTH_WPA2_PSK:      auth = "WPA2";   break;
        case WIFI_AUTH_WPA3_PSK:      auth = "WPA3";   break;
        case WIFI_AUTH_WPA_WPA2_PSK:  auth = "WPA/2";  break;
        case WIFI_AUTH_WPA2_WPA3_PSK: auth = "WPA2/3"; break;
        default:                      auth = "Other";  break;
        }
        printf("%-32s  %4d  %2d  %s\n",
               results[i].ssid, results[i].rssi,
               results[i].channel, auth);
    }

    free(results);
    return ESP_OK;
}

esp_err_t app_wifi_set_enabled(bool enabled)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u8(h, NVS_KEY_ENABLED, enabled ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }

    nvs_close(h);
    return err;
}

bool app_wifi_get_enabled(void)
{
    nvs_handle_t h;
    if (nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return true;  /* default: enabled */
    }

    uint8_t val = 1;
    nvs_get_u8(h, NVS_KEY_ENABLED, &val);
    nvs_close(h);
    return val != 0;
}
