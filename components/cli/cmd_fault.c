#include <stdio.h>
#include <string.h>

#include "esp_console.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "sequencer.h"

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

static int cmd_fault_handler(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: fault <show|clear|inject>\n");
        return 1;
    }

    if (strcmp(argv[1], "show") == 0) {
        seq_state_t state = sequencer_get_state();
        seq_fault_t fault = sequencer_get_fault();
        printf("State: %s   Fault: %s\n",
               s_state_names[state], s_fault_names[fault]);
        return 0;
    }

    if (strcmp(argv[1], "clear") == 0) {
        esp_err_t ret = sequencer_clear_fault();
        if (ret == ESP_OK) {
            printf("Fault cleared — returned to RX\n");
        } else {
            printf("Cannot clear: not in FAULT state\n");
        }
        return (ret == ESP_OK) ? 0 : 1;
    }

    if (strcmp(argv[1], "inject") == 0) {
        if (argc < 3) {
            printf("Usage: fault inject <swr|temp1|temp2|emergency>\n");
            return 1;
        }

        seq_fault_t fault;
        if (strcmp(argv[2], "swr") == 0) {
            fault = SEQ_FAULT_HIGH_SWR;
        } else if (strcmp(argv[2], "temp1") == 0) {
            fault = SEQ_FAULT_OVER_TEMP1;
        } else if (strcmp(argv[2], "temp2") == 0) {
            fault = SEQ_FAULT_OVER_TEMP2;
        } else if (strcmp(argv[2], "emergency") == 0) {
            fault = SEQ_FAULT_EMERGENCY;
        } else {
            printf("Unknown fault: %s\n", argv[2]);
            printf("Valid: swr, temp1, temp2, emergency\n");
            return 1;
        }

        seq_event_t ev = {
            .type = (fault == SEQ_FAULT_EMERGENCY)
                        ? SEQ_EVENT_EMERGENCY_PA_OFF
                        : SEQ_EVENT_FAULT,
            .data = (uint32_t)fault,
        };
        xQueueSend(sequencer_get_event_queue(), &ev, 0);
        printf("Injected fault: %s\n", s_fault_names[fault]);
        return 0;
    }

    printf("Unknown subcommand: %s\n", argv[1]);
    return 1;
}

void register_cmd_fault(void)
{
    const esp_console_cmd_t cmd = {
        .command = "fault",
        .help    = "Fault management: fault <show|clear|inject>",
        .hint    = NULL,
        .func    = &cmd_fault_handler,
    };
    esp_console_cmd_register(&cmd);
}
