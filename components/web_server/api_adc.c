#include <stdlib.h>

#include "cJSON.h"
#include "esp_http_server.h"

#include "ads1115.h"
#include "monitor.h"

#include "web_json.h"

static const char *s_channel_names[] = {
    "fwd_power", "ref_power", "temp_right", "temp_left"
};

/* ---- GET /api/adc or /api/adc?ch=N -------------------------------------- */

static esp_err_t api_adc_handler(httpd_req_t *req)
{
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

            float voltage = 0;
            esp_err_t err = monitor_read_channel((ads1115_channel_t)ch, &voltage);
            if (err != ESP_OK) {
                return web_json_error(req, 500, "ADC read failed");
            }

            cJSON *data = cJSON_CreateObject();
            cJSON_AddNumberToObject(data, "ch", ch);
            cJSON_AddStringToObject(data, "name", s_channel_names[ch]);
            cJSON_AddNumberToObject(data, "voltage", voltage);
            return web_json_ok(req, data);
        }
    }

    /* No ch parameter — read all channels */
    cJSON *channels = cJSON_CreateArray();
    for (int i = 0; i < 4; i++) {
        float voltage = 0;
        esp_err_t err = monitor_read_channel((ads1115_channel_t)i, &voltage);

        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "ch", i);
        cJSON_AddStringToObject(item, "name", s_channel_names[i]);
        if (err == ESP_OK) {
            cJSON_AddNumberToObject(item, "voltage", voltage);
        } else {
            cJSON_AddNullToObject(item, "voltage");
        }
        cJSON_AddItemToArray(channels, item);
    }

    cJSON *data = cJSON_CreateObject();
    cJSON_AddItemToObject(data, "channels", channels);
    return web_json_ok(req, data);
}

/* ---- registration ------------------------------------------------------- */

void web_register_api_adc(httpd_handle_t server)
{
    const httpd_uri_t adc_uri = {
        .uri     = "/api/adc",
        .method  = HTTP_GET,
        .handler = api_adc_handler,
    };
    httpd_register_uri_handler(server, &adc_uri);
}
