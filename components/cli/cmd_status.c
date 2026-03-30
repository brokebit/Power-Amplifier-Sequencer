#include <stdio.h>

#include "esp_console.h"

#include "config.h"
#include "hw_config.h"
#include "sequencer.h"
#include "system_state.h"

#include "cli.h"

static int cmd_status_handler(int argc, char **argv)
{
    system_state_t ss;
    system_state_get(&ss);

    printf("PTT: %s   State: %-6s  Fault: %s\n",
           ss.ptt_active ? "ON" : "off",
           seq_state_name(ss.seq_state),
           seq_fault_name(ss.seq_fault));

    printf("Relays:");
    app_config_t snap;
    config_snapshot(&snap);
    for (int i = 0; i < HW_RELAY_COUNT; i++) {
        bool on = (ss.relay_states >> i) & 1;
        char label[24];
        config_relay_label(&snap, i + 1, label, sizeof(label));
        printf(" [%s:%s]", label, on ? "ON" : "off");
    }
    printf("\n");

    printf("Fwd: %.1fW  Ref: %.1fW  SWR: %.1f\n",
           ss.fwd_power_w, ss.ref_power_w, ss.swr);

    printf("Temp1: %.1fC  Temp2: %.1fC\n",
           ss.temp1_c, ss.temp2_c);

    if (ss.wifi_connected) {
        uint32_t ip = ss.wifi_ip_addr;
        printf("WiFi: connected  IP: %lu.%lu.%lu.%lu  RSSI: %d dBm\n",
               (unsigned long)(ip & 0xFF),
               (unsigned long)((ip >> 8) & 0xFF),
               (unsigned long)((ip >> 16) & 0xFF),
               (unsigned long)((ip >> 24) & 0xFF),
               ss.wifi_rssi);
    } else {
        printf("WiFi: disconnected\n");
    }

    return 0;
}

void cli_register_cmd_status(void)
{
    const esp_console_cmd_t cmd = {
        .command = "status",
        .help = "Show system state (PTT, relays, power, SWR, temps)",
        .hint = NULL,
        .func = &cmd_status_handler,
    };
    esp_console_cmd_register(&cmd);
}
