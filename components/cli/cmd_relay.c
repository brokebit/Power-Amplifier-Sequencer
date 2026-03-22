#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_console.h"

#include "hw_config.h"
#include "relays.h"
#include "system_state.h"

static void show_relays(void)
{
    system_state_t ss;
    system_state_get(&ss);

    printf("Relays:");
    for (int i = 0; i < HW_RELAY_COUNT; i++) {
        bool on = (ss.relay_states >> i) & 1;
        printf(" [R%d:%s]", i + 1, on ? "ON" : "off");
    }
    printf("\n");
}

static int cmd_relay_handler(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: relay show | relay <1-%d> <on|off>\n", HW_RELAY_COUNT);
        return 1;
    }

    if (strcmp(argv[1], "show") == 0) {
        show_relays();
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
        printf("R%ld %s\n", relay_id, on ? "ON" : "OFF");
    } else {
        printf("Failed: %s\n", esp_err_to_name(ret));
    }
    return (ret == ESP_OK) ? 0 : 1;
}

void register_cmd_relay(void)
{
    const esp_console_cmd_t cmd = {
        .command = "relay",
        .help    = "Relay control: relay show | relay <1-6> <on|off>",
        .hint    = NULL,
        .func    = &cmd_relay_handler,
    };
    esp_console_cmd_register(&cmd);
}
