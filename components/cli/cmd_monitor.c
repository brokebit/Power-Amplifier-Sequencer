#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_console.h"
#include "driver/uart.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hw_config.h"
#include "sequencer.h"
#include "system_state.h"

#define DEFAULT_INTERVAL_MS  1000

static int cmd_monitor_handler(int argc, char **argv)
{
    int interval_ms = DEFAULT_INTERVAL_MS;
    bool csv = false;

    for (int i = 1; i < argc; i++) {
        if (strcasecmp(argv[i], "csv") == 0) {
            csv = true;
        } else {
            char *end;
            long val = strtol(argv[i], &end, 10);
            if (end != argv[i] && *end == '\0' && val >= 100 && val <= 60000) {
                interval_ms = (int)val;
            } else {
                printf("Invalid argument: %s\n", argv[i]);
                return 1;
            }
        }
    }

    if (!csv) {
        printf("Monitoring every %dms — press Enter to stop\n", interval_ms);
    }

    system_state_t ss;
    for (;;) {
        system_state_get(&ss);

        if (csv) {
            printf("%d,%s,%s",
                   ss.ptt_active ? 1 : 0,
                   seq_state_name(ss.seq_state),
                   seq_fault_name(ss.seq_fault));
            for (int i = 0; i < HW_RELAY_COUNT; i++) {
                printf(",%d", (ss.relay_states >> i) & 1);
            }
            printf(",%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f\n",
                   ss.fwd_power_dbm, ss.fwd_power_w,
                   ss.ref_power_dbm, ss.ref_power_w,
                   ss.swr, ss.temp1_c, ss.temp2_c);
        } else {
            /* Build relay string */
            char relays[32];
            int pos = 0;
            for (int i = 0; i < HW_RELAY_COUNT; i++) {
                bool on = (ss.relay_states >> i) & 1;
                pos += snprintf(relays + pos, sizeof(relays) - pos,
                                "%s%d", on ? "+" : "-", i + 1);
            }

            printf("PTT:%-3s St:%-6s Flt:%-10s Rl:[%s] "
                   "F:%.1fdBm/%.1fW R:%.1fdBm/%.1fW SWR:%.1f T1:%.1fC T2:%.1fC\n",
                   ss.ptt_active ? "ON" : "off",
                   seq_state_name(ss.seq_state),
                   seq_fault_name(ss.seq_fault),
                   relays,
                   ss.fwd_power_dbm, ss.fwd_power_w,
                   ss.ref_power_dbm, ss.ref_power_w,
                   ss.swr, ss.temp1_c, ss.temp2_c);
        }

        /* Check for keypress to stop — poll UART0 in small increments */
        int waited = 0;
        while (waited < interval_ms) {
            uint8_t c;
            int len = uart_read_bytes(UART_NUM_0, &c, 1, pdMS_TO_TICKS(50));
            if (len > 0 && (c == '\r' || c == '\n')) {
                if (!csv) {
                    printf("Stopped\n");
                }
                return 0;
            }
            waited += 50;
        }
    }
}

void cli_register_cmd_monitor(void)
{
    const esp_console_cmd_t cmd = {
        .command = "monitor",
        .help = "Continuous status output: monitor [interval_ms] [csv]",
        .hint = NULL,
        .func = &cmd_monitor_handler,
    };
    esp_console_cmd_register(&cmd);
}
