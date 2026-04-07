#include <stdlib.h>

#include "esp_http_server.h"

#include "cJSON.h"
#include "config.h"
#include "system_state.h"

#include "web_json.h"

static const char *s_channel_names[] = {
    "fwd_power", "ref_power", "temp1", "temp2"
};

/* ---- GET /api/adc or /api/adc?ch=N -------------------------------------- */

static esp_err_t api_adc_handler(httpd_req_t *req)
{
    /* Snapshot chip 1 ADC voltages from system state (updated by monitor task) */
    system_state_t ss;
    system_state_get(&ss);
    const float voltages[] = { ss.adc_1_ch0, ss.adc_1_ch1, ss.adc_1_ch2, ss.adc_1_ch3 };

    /* Check for ?ch=N query parameter */
    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen > 0 && qlen < 16) {
        char query[16];
        httpd_req_get_url_query_str(req, query, sizeof(query));

        char ch_str[4];
        if (httpd_query_key_value(query, "ch", ch_str, sizeof(ch_str)) == ESP_OK) {
            int ch = atoi(ch_str);
            if (ch < 0 || ch > 3) {
                return web_json_error(req, 400, "ch must be 0-3");
            }

            cJSON *data = cJSON_CreateObject();
            cJSON_AddNumberToObject(data, "ch", ch);
            cJSON_AddStringToObject(data, "name", s_channel_names[ch]);
            cJSON_AddNumberToObject(data, "voltage", voltages[ch]);
            return web_json_ok(req, data);
        }
    }

    /* No ch parameter — read all channels */
    cJSON *channels = cJSON_CreateArray();
    for (int i = 0; i < 4; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "ch", i);
        cJSON_AddStringToObject(item, "name", s_channel_names[i]);
        cJSON_AddNumberToObject(item, "voltage", voltages[i]);
        cJSON_AddItemToArray(channels, item);
    }

    cJSON *data = cJSON_CreateObject();
    cJSON_AddItemToObject(data, "channels", channels);
    return web_json_ok(req, data);
}

/* ---- POST /api/adc/name ------------------------------------------------- */

static esp_err_t api_adc_name_handler(httpd_req_t *req)
{
    cJSON *body = web_parse_body(req);
    if (!body) {
        return ESP_OK;
    }

    cJSON *ch_json = cJSON_GetObjectItem(body, "ch");
    cJSON *name_json = cJSON_GetObjectItem(body, "name");
    if (!ch_json || !cJSON_IsNumber(ch_json)) {
        cJSON_Delete(body);
        return web_json_error(req, 400, "missing 'ch' (number) field");
    }

    int ch = ch_json->valueint;
    const char *name = (name_json && cJSON_IsString(name_json))
                           ? name_json->valuestring : NULL;

    char err_msg[64];
    esp_err_t err = config_set_adc_ch_name((uint8_t)ch, name,
                                            err_msg, sizeof(err_msg));
    cJSON_Delete(body);

    if (err != ESP_OK) {
        return web_json_error(req, 400, err_msg);
    }
    return web_json_ok(req, NULL);
}

/* ---- registration ------------------------------------------------------- */

void web_register_api_adc(httpd_handle_t server)
{
    const httpd_uri_t adc_uri = {
        .uri = "/api/adc",
        .method = HTTP_GET,
        .handler = api_adc_handler,
    };
    httpd_register_uri_handler(server, &adc_uri);

    const httpd_uri_t name_uri = {
        .uri = "/api/adc/name",
        .method = HTTP_POST,
        .handler = api_adc_name_handler,
    };
    httpd_register_uri_handler(server, &name_uri);
}
