#include <stdio.h>

#include "esp_console.h"

#include "cli.h"
#include "config.h"
#include "hw_config.h"
#include "sequencer.h"
#include "system_state.h"

static const char *s_state_names[] = {
    [SEQ_STATE_RX]            = "RX",
    [SEQ_STATE_SEQUENCING_TX] = "SEQ_TX",
    [SEQ_STATE_TX]            = "TX",
    [SEQ_STATE_SEQUENCING_RX] = "SEQ_RX",
    [SEQ_STATE_FAULT]         = "FAULT",
};

static const char *s_fault_names[] = {
    [SEQ_FAULT_NONE]       = "none",
    [SEQ_FAULT_HIGH_SWR]   = "HIGH_SWR",
    [SEQ_FAULT_OVER_TEMP1] = "OVER_TEMP1",
    [SEQ_FAULT_OVER_TEMP2] = "OVER_TEMP2",
    [SEQ_FAULT_EMERGENCY]  = "EMERGENCY",
};

static int cmd_status_handler(int argc, char **argv)
{
    system_state_t ss;
    system_state_get(&ss);

    printf("PTT: %s   State: %-6s  Fault: %s\n",
           ss.ptt_active ? "ON" : "off",
           s_state_names[ss.seq_state],
           s_fault_names[ss.seq_fault]);

    printf("Relays:");
    const app_config_t *cfg = cli_get_config();
    for (int i = 0; i < HW_RELAY_COUNT; i++) {
        bool on = (ss.relay_states >> i) & 1;
        char label[24];
        config_relay_label(cfg, i + 1, label, sizeof(label));
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

void register_cmd_status(void)
{
    const esp_console_cmd_t cmd = {
        .command = "status",
        .help    = "Show system state (PTT, relays, power, SWR, temps)",
        .hint    = NULL,
        .func    = &cmd_status_handler,
    };
    esp_console_cmd_register(&cmd);
}
