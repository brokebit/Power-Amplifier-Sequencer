#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_console.h"
#include "esp_err.h"

#include "config.h"
#include "hw_config.h"
#include "relays.h"
#include "system_state.h"

#include "cli.h"

static void show_relays(void)
{
    system_state_t ss;
    system_state_get(&ss);
    app_config_t snap;
    config_snapshot(&snap);

    printf("Relays:");
    for (int i = 0; i < HW_RELAY_COUNT; i++) {
        bool on = (ss.relay_states >> i) & 1;
        char label[24];
        config_relay_label(&snap, i + 1, label, sizeof(label));
        printf(" [%s:%s]", label, on ? "ON" : "off");
    }
    printf("\n");
}

static int cmd_relay_handler(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: relay show | relay <1-%d> <on|off> | relay name [<1-%d>] [<label>]\n",
               HW_RELAY_COUNT, HW_RELAY_COUNT);
        return 1;
    }

    if (strcmp(argv[1], "show") == 0) {
        show_relays();
        return 0;
    }

    if (strcmp(argv[1], "name") == 0) {
        if (argc < 3) {
            /* Show all names */
            app_config_t snap;
            config_snapshot(&snap);
            for (int i = 0; i < HW_RELAY_COUNT; i++) {
                if (snap.relay_names[i][0] != '\0') {
                    printf("  R%d = %s\n", i + 1, snap.relay_names[i]);
                } else {
                    printf("  R%d = (none)\n", i + 1);
                }
            }
            return 0;
        }
        char *end;
        long id = strtol(argv[2], &end, 10);
        if (end == argv[2] || *end != '\0' || id < 1 || id > HW_RELAY_COUNT) {
            printf("Invalid relay: %s (expected 1-%d)\n", argv[2], HW_RELAY_COUNT);
            return 1;
        }
        char err_msg[64];
        const char *name = (argc >= 4) ? argv[3] : NULL;
        esp_err_t ret = config_set_relay_name((uint8_t)id, name,
                                              err_msg, sizeof(err_msg));
        if (ret != ESP_OK) {
            printf("%s\n", err_msg);
            return 1;
        }
        if (name) {
            printf("R%ld = %s\n", id, name);
        } else {
            printf("R%ld name cleared\n", id);
        }
        printf("Use 'config save' to persist\n");
        return 0;
    }

    /* Parse relay number */
    char *end;
    long relay_id = strtol(argv[1], &end, 10);
    if (end == argv[1] || *end != '\0' || relay_id < 1 || relay_id > HW_RELAY_COUNT) {
        printf("Invalid relay: %s (expected 1-%d)\n", argv[1], HW_RELAY_COUNT);
        return 1;
    }

    if (argc < 3) {
        printf("Usage: relay %ld <on|off>\n", relay_id);
        return 1;
    }

    bool on;
    if (strcmp(argv[2], "on") == 0) {
        on = true;
    } else if (strcmp(argv[2], "off") == 0) {
        on = false;
    } else {
        printf("Invalid state: %s (expected on|off)\n", argv[2]);
        return 1;
    }

    printf("WARNING: Directly controlling relays bypasses the sequencer\n");
    esp_err_t ret = relay_set((uint8_t)relay_id, on);
    if (ret == ESP_OK) {
        app_config_t snap;
        config_snapshot(&snap);
        char label[24];
        config_relay_label(&snap, (uint8_t)relay_id, label, sizeof(label));
        printf("%s %s\n", label, on ? "ON" : "OFF");
    } else {
        printf("Failed: %s\n", esp_err_to_name(ret));
    }
    return (ret == ESP_OK) ? 0 : 1;
}

void cli_register_cmd_relay(void)
{
    const esp_console_cmd_t cmd = {
        .command = "relay",
        .help = "Relay control: relay show | relay <1-6> <on|off> | relay name [<1-6>] [<label>]",
        .hint = NULL,
        .func = &cmd_relay_handler,
    };
    esp_console_cmd_register(&cmd);
}
