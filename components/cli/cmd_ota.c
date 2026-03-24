#include <stdio.h>
#include <string.h>

#include "esp_console.h"
#include "esp_err.h"

#include "ota.h"

static int cmd_ota_handler(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: ota <status|repo|update|rollback|validate>\n");
        return 1;
    }

    if (strcmp(argv[1], "status") == 0) {
        app_ota_print_status();
        return 0;
    }

    if (strcmp(argv[1], "repo") == 0) {
        if (argc < 3) {
            char repo[128];
            esp_err_t err = app_ota_get_repo(repo, sizeof(repo));
            if (err == ESP_OK) {
                printf("GitHub repo: %s\n", repo);
            } else {
                printf("No GitHub repo configured\n");
            }
            return 0;
        }
        esp_err_t err = app_ota_set_repo(argv[2]);
        if (err == ESP_OK) {
            printf("GitHub repo set to '%s'\n", argv[2]);
        } else if (err == ESP_ERR_INVALID_ARG) {
            printf("Invalid repo format. Use 'owner/repo'\n");
        } else {
            printf("Failed to save repo: %s\n", esp_err_to_name(err));
        }
        return (err == ESP_OK) ? 0 : 1;
    }

    if (strcmp(argv[1], "update") == 0) {
        if (argc < 3) {
            printf("Usage: ota update <latest|vX.Y.Z|https://...>\n");
            return 1;
        }
        esp_err_t err = app_ota_update(argv[2]);
        /* If we get here, it failed (success reboots) */
        return (err == ESP_OK) ? 0 : 1;
    }

    if (strcmp(argv[1], "rollback") == 0) {
        app_ota_rollback();
        /* If we get here, it failed (success reboots) */
        return 1;
    }

    if (strcmp(argv[1], "validate") == 0) {
        esp_err_t err = app_ota_validate();
        return (err == ESP_OK) ? 0 : 1;
    }

    printf("Unknown subcommand: %s\n", argv[1]);
    return 1;
}

void register_cmd_ota(void)
{
    const esp_console_cmd_t cmd = {
        .command = "ota",
        .help    = "OTA updates: ota <status|repo|update|rollback|validate>",
        .hint    = NULL,
        .func    = &cmd_ota_handler,
    };
    esp_console_cmd_register(&cmd);
}
