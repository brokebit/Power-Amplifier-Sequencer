#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_http_server.h"
#include "esp_log.h"

#include "web_json.h"

static const char *TAG = "api_static";

#define CHUNK_SIZE 1024

/* ---- MIME type lookup --------------------------------------------------- */

static const char *get_mime_type(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot) {
        return "application/octet-stream";
    }
    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0) {
        return "text/html";
    }
    if (strcmp(dot, ".css") == 0) {
        return "text/css";
    }
    if (strcmp(dot, ".js") == 0) {
        return "application/javascript";
    }
    if (strcmp(dot, ".json") == 0) {
        return "application/json";
    }
    if (strcmp(dot, ".png") == 0) {
        return "image/png";
    }
    if (strcmp(dot, ".ico") == 0) {
        return "image/x-icon";
    }
    if (strcmp(dot, ".svg") == 0) {
        return "image/svg+xml";
    }
    if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0) {
        return "image/jpeg";
    }
    if (strcmp(dot, ".gif") == 0) {
        return "image/gif";
    }
    return "application/octet-stream";
}

/* ---- wildcard GET handler ----------------------------------------------- */

static esp_err_t static_file_handler(httpd_req_t *req)
{
    /* Build filesystem path from URI */
    char filepath[128];
    const char *uri = req->uri;

    /* Strip query string if present */
    const char *qmark = strchr(uri, '?');
    size_t uri_len = qmark ? (size_t)(qmark - uri) : strlen(uri);

    /* Default to index.html for root */
    if (uri_len == 1 && uri[0] == '/') {
        snprintf(filepath, sizeof(filepath), "/www/index.html");
    } else {
        snprintf(filepath, sizeof(filepath), "/www%.*s", (int)uri_len, uri);
    }

    /* Security: reject paths with .. */
    if (strstr(filepath, "..")) {
        return web_json_error(req, 403, "forbidden");
    }

    FILE *f = fopen(filepath, "r");
    if (!f) {
        ESP_LOGD(TAG, "File not found: %s", filepath);
        return web_json_error(req, 404, "file not found");
    }

    httpd_resp_set_type(req, get_mime_type(filepath));

    /* Cache static assets (not HTML) */
    const char *dot = strrchr(filepath, '.');
    if (dot && strcmp(dot, ".html") != 0 && strcmp(dot, ".htm") != 0) {
        httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
    }

    /* Stream file in chunks */
    char buf[CHUNK_SIZE];
    size_t read_bytes;
    do {
        read_bytes = fread(buf, 1, sizeof(buf), f);
        if (read_bytes > 0) {
            if (httpd_resp_send_chunk(req, buf, read_bytes) != ESP_OK) {
                fclose(f);
                httpd_resp_send_chunk(req, NULL, 0);
                return ESP_FAIL;
            }
        }
    } while (read_bytes == sizeof(buf));

    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);  /* End chunked response */
    return ESP_OK;
}

/* ---- registration ------------------------------------------------------- */

void web_register_api_static(httpd_handle_t server)
{
    /* Wildcard catch-all — must be registered LAST so API routes match first */
    const httpd_uri_t static_uri = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = static_file_handler,
    };
    httpd_register_uri_handler(server, &static_uri);
}
