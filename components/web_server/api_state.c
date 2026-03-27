#include <stdio.h>

#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_http_server.h"

#include "cJSON.h"
#include "hw_config.h"
#include "sequencer.h"
#include "system_state.h"

#include "web_json.h"
#include "web_server.h"

/* ---- helpers ------------------------------------------------------------ */

cJSON *web_build_state_json(void)
{
    system_state_t ss;
    system_state_get(&ss);

    cJSON *data = cJSON_CreateObject();
    if (!data) {
        return NULL;
    }

    cJSON_AddBoolToObject(data, "ptt", ss.ptt_active);
    cJSON_AddStringToObject(data, "seq_state", seq_state_name(ss.seq_state));
    cJSON_AddStringToObject(data, "seq_fault", seq_fault_name(ss.seq_fault));

    cJSON *relays = cJSON_CreateArray();
    for (int i = 0; i < HW_RELAY_COUNT; i++) {
        cJSON_AddItemToArray(relays, cJSON_CreateBool((ss.relay_states >> i) & 1));
    }
    cJSON_AddItemToObject(data, "relays", relays);

    /* Relay names */
    const app_config_t *cfg = web_get_config();
    cJSON *names = cJSON_CreateArray();
    for (int i = 0; i < HW_RELAY_COUNT; i++) {
        cJSON_AddItemToArray(names,
            cJSON_CreateString(cfg->relay_names[i][0] ? cfg->relay_names[i] : ""));
    }
    cJSON_AddItemToObject(data, "relay_names", names);

    cJSON_AddNumberToObject(data, "fwd_w", ss.fwd_power_w);
    cJSON_AddNumberToObject(data, "ref_w", ss.ref_power_w);
    cJSON_AddNumberToObject(data, "swr", ss.swr);
    cJSON_AddNumberToObject(data, "temp1_c", ss.temp1_c);
    cJSON_AddNumberToObject(data, "temp2_c", ss.temp2_c);

    cJSON *wifi = cJSON_CreateObject();
    cJSON_AddBoolToObject(wifi, "connected", ss.wifi_connected);
    if (ss.wifi_connected) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), "%lu.%lu.%lu.%lu",
                 (unsigned long)(ss.wifi_ip_addr & 0xFF),
                 (unsigned long)((ss.wifi_ip_addr >> 8) & 0xFF),
                 (unsigned long)((ss.wifi_ip_addr >> 16) & 0xFF),
                 (unsigned long)((ss.wifi_ip_addr >> 24) & 0xFF));
        cJSON_AddStringToObject(wifi, "ip", ip_str);
        cJSON_AddNumberToObject(wifi, "rssi", ss.wifi_rssi);
    }
    cJSON_AddItemToObject(data, "wifi", wifi);

    return data;
}

/* ---- GET /api/state ----------------------------------------------------- */

static esp_err_t api_state_handler(httpd_req_t *req)
{
    cJSON *data = web_build_state_json();
    if (!data) {
        return web_json_error(req, 500, "out of memory");
    }
    return web_json_ok(req, data);
}

/* ---- GET /api/version --------------------------------------------------- */

static esp_err_t api_version_handler(httpd_req_t *req)
{
    const esp_app_desc_t *desc = esp_app_get_description();

    esp_chip_info_t chip;
    esp_chip_info(&chip);

    cJSON *data = cJSON_CreateObject();
    if (!data) {
        return web_json_error(req, 500, "out of memory");
    }

    cJSON_AddStringToObject(data, "project", desc->project_name);
    cJSON_AddStringToObject(data, "version", desc->version);
    cJSON_AddStringToObject(data, "idf_version", desc->idf_ver);
    cJSON_AddNumberToObject(data, "cores", chip.cores);

    return web_json_ok(req, data);
}

/* ---- registration ------------------------------------------------------- */

void web_register_api_state(httpd_handle_t server)
{
    const httpd_uri_t state_uri = {
        .uri ="/api/state",
        .method =HTTP_GET,
        .handler =api_state_handler,
    };
    httpd_register_uri_handler(server, &state_uri);

    const httpd_uri_t version_uri = {
        .uri ="/api/version",
        .method =HTTP_GET,
        .handler =api_version_handler,
    };
    httpd_register_uri_handler(server, &version_uri);
}
