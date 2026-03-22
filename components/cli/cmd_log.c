#include <stdio.h>
#include <string.h>

#include "esp_console.h"
#include "esp_log.h"

static int cmd_log_handler(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: log <on|off|none|error|warn|info|debug|verbose> [tag]\n");
        return 1;
    }

    const char *level_str = argv[1];
    const char *tag = (argc >= 3) ? argv[2] : "*";
    esp_log_level_t level;

    if (strcmp(level_str, "on") == 0 || strcmp(level_str, "info") == 0) {
        level = ESP_LOG_INFO;
    } else if (strcmp(level_str, "off") == 0 || strcmp(level_str, "none") == 0) {
        level = ESP_LOG_NONE;
    } else if (strcmp(level_str, "error") == 0) {
        level = ESP_LOG_ERROR;
    } else if (strcmp(level_str, "warn") == 0) {
        level = ESP_LOG_WARN;
    } else if (strcmp(level_str, "debug") == 0) {
        level = ESP_LOG_DEBUG;
    } else if (strcmp(level_str, "verbose") == 0) {
        level = ESP_LOG_VERBOSE;
    } else {
        printf("Unknown level: %s\n", level_str);
        return 1;
    }

    esp_log_level_set(tag, level);

    if (strcmp(tag, "*") == 0) {
        printf("Log level: %s (all tags)\n", level_str);
    } else {
        printf("Log level: %s for tag '%s'\n", level_str, tag);
    }

    return 0;
}

void register_cmd_log(void)
{
    const esp_console_cmd_t cmd = {
        .command = "log",
        .help    = "Set log level: log <on|off|error|warn|info|debug|verbose> [tag]",
        .hint    = NULL,
        .func    = &cmd_log_handler,
    };
    esp_console_cmd_register(&cmd);
}
