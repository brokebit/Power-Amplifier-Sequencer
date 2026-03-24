#include <string.h>

#include "cJSON.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "web_ws.h"

/* Defined in api_state.c */
cJSON *web_build_state_json(void);

static const char *TAG = "web_ws";

static int s_client_fds[WS_MAX_CLIENTS];
static SemaphoreHandle_t s_mutex;
static httpd_handle_t s_server;
static TaskHandle_t s_push_task;

/* ---- client tracking ---------------------------------------------------- */

void ws_add_client(int fd)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (s_client_fds[i] == -1) {
            s_client_fds[i] = fd;
            ESP_LOGI(TAG, "WS client added: fd=%d (slot %d)", fd, i);
            xSemaphoreGive(s_mutex);
            return;
        }
    }
    xSemaphoreGive(s_mutex);
    ESP_LOGW(TAG, "WS client rejected: fd=%d (all slots full)", fd);
}

void ws_remove_client(int fd)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (s_client_fds[i] == fd) {
            s_client_fds[i] = -1;
            ESP_LOGI(TAG, "WS client removed: fd=%d (slot %d)", fd, i);
            break;
        }
    }
    xSemaphoreGive(s_mutex);
}

/* ---- WebSocket URI handler ---------------------------------------------- */

esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        /* WebSocket handshake — new connection */
        ws_add_client(httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    /* Incoming WebSocket frame — we don't expect any, but handle gracefully */
    httpd_ws_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.type = HTTPD_WS_TYPE_TEXT;

    /* Read the frame to clear the buffer */
    uint8_t buf[128];
    frame.payload = buf;
    esp_err_t err = httpd_ws_recv_frame(req, &frame, sizeof(buf));
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "WS recv error: %s", esp_err_to_name(err));
    }

    /* Close frame handling */
    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        ws_remove_client(httpd_req_to_sockfd(req));
    }

    return ESP_OK;
}

/* ---- push task ---------------------------------------------------------- */

static void ws_push_task(void *arg)
{
    (void)arg;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(500));

        /* Build state JSON */
        cJSON *root = cJSON_CreateObject();
        if (!root) {
            continue;
        }

        cJSON *state = web_build_state_json();
        if (!state) {
            cJSON_Delete(root);
            continue;
        }

        /* For WS frames, send the state directly (no envelope) */
        char *json_str = cJSON_PrintUnformatted(state);
        cJSON_Delete(state);
        if (!json_str) {
            cJSON_Delete(root);
            continue;
        }
        cJSON_Delete(root);

        size_t json_len = strlen(json_str);

        httpd_ws_frame_t frame = {
            .type    = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)json_str,
            .len     = json_len,
            .final   = true,
        };

        /* Send to all connected clients */
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        for (int i = 0; i < WS_MAX_CLIENTS; i++) {
            if (s_client_fds[i] != -1) {
                esp_err_t err = httpd_ws_send_frame_async(s_server,
                                    s_client_fds[i], &frame);
                if (err != ESP_OK) {
                    ESP_LOGD(TAG, "WS send failed fd=%d: %s",
                             s_client_fds[i], esp_err_to_name(err));
                    s_client_fds[i] = -1;
                }
            }
        }
        xSemaphoreGive(s_mutex);

        free(json_str);
    }
}

/* ---- init / stop -------------------------------------------------------- */

void ws_init(httpd_handle_t server)
{
    s_server = server;
    s_mutex = xSemaphoreCreateMutex();

    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        s_client_fds[i] = -1;
    }

    xTaskCreate(ws_push_task, "ws_push", 4096, NULL, 3, &s_push_task);
    ESP_LOGI(TAG, "WebSocket push task started");
}

void ws_stop(void)
{
    if (s_push_task) {
        vTaskDelete(s_push_task);
        s_push_task = NULL;
    }
    if (s_mutex) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }
}
