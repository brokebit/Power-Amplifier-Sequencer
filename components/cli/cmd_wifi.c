#include <stdio.h>
#include <string.h>

#include "esp_console.h"
#include "esp_err.h"

#include "wifi_sta.h"
#include "system_state.h"

/* ---- wifi status -------------------------------------------------------- */

static void print_wifi_status(void)
{
    system_state_t ss;
    system_state_get(&ss);

    if (ss.wifi_connected) {
        char ip_str[16];
        app_wifi_get_ip_str(ip_str, sizeof(ip_str));
        int8_t rssi;
        app_wifi_get_rssi(&rssi);
        printf("WiFi: connected\n");
        printf("  IP:   %s\n", ip_str);
        printf("  RSSI: %d dBm\n", rssi);
    } else {
        printf("WiFi: disconnected\n");
    }
    printf("  Auto-connect: %s\n", app_wifi_get_enabled() ? "enabled" : "disabled");
}

/* ---- command handler ---------------------------------------------------- */

static int cmd_wifi_handler(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: wifi <status|config|connect|disconnect|scan|enable|disable|erase>\n");
        return 1;
    }

    if (strcmp(argv[1], "status") == 0) {
        print_wifi_status();
        return 0;
    }

    if (strcmp(argv[1], "config") == 0) {
        if (argc < 3) {
            printf("Usage: wifi config <ssid> [password]\n");
            return 1;
        }
        const char *ssid = argv[2];
        const char *pass = (argc >= 4) ? argv[3] : "";

        esp_err_t err = app_wifi_set_credentials(ssid, pass);
        if (err == ESP_OK) {
            printf("WiFi credentials saved for '%s'\n", ssid);
            printf("Use 'wifi connect' to connect now\n");
        } else {
            printf("Failed to save credentials: %s\n", esp_err_to_name(err));
        }
        return (err == ESP_OK) ? 0 : 1;
    }

    if (strcmp(argv[1], "connect") == 0) {
        esp_err_t err = app_wifi_connect();
        if (err == ESP_ERR_NOT_FOUND) {
            printf("No credentials saved. Use 'wifi config <ssid> <pass>' first\n");
        } else if (err == ESP_OK) {
            printf("Connecting...\n");
        } else {
            printf("Connect failed: %s\n", esp_err_to_name(err));
        }
        return (err == ESP_OK) ? 0 : 1;
    }

    if (strcmp(argv[1], "disconnect") == 0) {
        app_wifi_disconnect();
        printf("Disconnected\n");
        return 0;
    }

    if (strcmp(argv[1], "scan") == 0) {
        printf("Scanning...\n");
        esp_err_t err = app_wifi_scan();
        return (err == ESP_OK) ? 0 : 1;
    }

    if (strcmp(argv[1], "enable") == 0) {
        app_wifi_set_enabled(true);
        printf("WiFi auto-connect enabled\n");
        return 0;
    }

    if (strcmp(argv[1], "disable") == 0) {
        app_wifi_set_enabled(false);
        app_wifi_disconnect();
        printf("WiFi disabled and disconnected\n");
        return 0;
    }

    if (strcmp(argv[1], "erase") == 0) {
        app_wifi_erase_credentials();
        printf("WiFi credentials erased\n");
        return 0;
    }

    printf("Unknown subcommand: %s\n", argv[1]);
    return 1;
}

void register_cmd_wifi(void)
{
    const esp_console_cmd_t cmd = {
        .command = "wifi",
        .help    = "WiFi management: wifi <status|config|connect|disconnect|scan|enable|disable|erase>",
        .hint    = NULL,
        .func    = &cmd_wifi_handler,
    };
    esp_console_cmd_register(&cmd);
}
