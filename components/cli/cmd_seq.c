#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_console.h"

#include "config.h"
#include "sequencer.h"
#include "monitor.h"

#include "cli.h"

/* ---- helpers ------------------------------------------------------------ */

static void print_sequence(const char *label, const seq_step_t *steps, uint8_t num)
{
    printf("%s sequence (%d steps):\n", label, num);
    if (num == 0) {
        printf("  (empty)\n");
        return;
    }
    for (int i = 0; i < num; i++) {
        printf("  %d: R%d %s  %dms\n", i + 1,
               steps[i].relay_id,
               steps[i].state ? "ON" : "OFF",
               steps[i].delay_ms);
    }
}

/**
 * Parse step tokens in the format R<n>:<on|off>:<delay_ms>.
 * Returns number of steps parsed, or -1 on error.
 */
static int parse_steps(int argc, char **argv, int first_arg,
                       seq_step_t *out, int max_steps)
{
    int count = argc - first_arg;
    if (count < 1) {
        printf("No steps provided\n");
        return -1;
    }
    if (count > max_steps) {
        printf("Too many steps (%d, max %d)\n", count, max_steps);
        return -1;
    }

    for (int i = 0; i < count; i++) {
        const char *token = argv[first_arg + i];
        unsigned int relay_id;
        char state_str[4];
        unsigned int delay_ms;

        int matched = sscanf(token, "R%u:%3[^:]:%u", &relay_id, state_str, &delay_ms);
        if (matched != 3) {
            printf("Bad format: '%s' (expected R<n>:<on|off>:<ms>)\n", token);
            return -1;
        }

        if (relay_id < 1 || relay_id > 6) {
            printf("Invalid relay R%u (expected 1-6)\n", relay_id);
            return -1;
        }

        bool on;
        if (strcmp(state_str, "on") == 0) {
            on = true;
        } else if (strcmp(state_str, "off") == 0) {
            on = false;
        } else {
            printf("Invalid state '%s' in '%s' (expected on|off)\n", state_str, token);
            return -1;
        }

        if (delay_ms > 10000) {
            printf("Delay %ums too large (max 10000)\n", delay_ms);
            return -1;
        }

        out[i].relay_id = (uint8_t)relay_id;
        out[i].state    = on ? 1 : 0;
        out[i].delay_ms = (uint16_t)delay_ms;
    }

    return count;
}

/* ---- command handler ---------------------------------------------------- */

static int cmd_seq_handler(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage:\n");
        printf("  seq tx show\n");
        printf("  seq rx show\n");
        printf("  seq tx set R3:on:50 R1:on:50 R2:on:0\n");
        printf("  seq rx set R2:off:50 R1:off:50 R3:off:0\n");
        printf("  seq save\n");
        printf("  seq apply\n");
        return 1;
    }

    app_config_t *cfg = cli_get_config();

    /* --- seq save --- */
    if (strcmp(argv[1], "save") == 0) {
        esp_err_t ret = config_save(cfg);
        if (ret == ESP_OK) {
            printf("Sequences saved to NVS\n");
        } else {
            printf("Save failed: %s\n", esp_err_to_name(ret));
        }
        return (ret == ESP_OK) ? 0 : 1;
    }

    /* --- seq apply --- */
    if (strcmp(argv[1], "apply") == 0) {
        esp_err_t ret = sequencer_update_config(cfg);
        if (ret != ESP_OK) {
            printf("Cannot apply: sequencer not in RX state\n");
            return 1;
        }
        monitor_update_config(cfg);
        printf("Config applied to sequencer and monitor\n");
        return 0;
    }

    /* --- seq tx|rx ... --- */
    bool is_tx;
    if (strcmp(argv[1], "tx") == 0) {
        is_tx = true;
    } else if (strcmp(argv[1], "rx") == 0) {
        is_tx = false;
    } else {
        printf("Unknown subcommand: %s\n", argv[1]);
        return 1;
    }

    if (argc < 3) {
        printf("Usage: seq %s <show|set>\n", argv[1]);
        return 1;
    }

    seq_step_t *steps    = is_tx ? cfg->tx_steps    : cfg->rx_steps;
    uint8_t    *num_ptr  = is_tx ? &cfg->tx_num_steps : &cfg->rx_num_steps;
    const char *label    = is_tx ? "TX" : "RX";

    if (strcmp(argv[2], "show") == 0) {
        print_sequence(label, steps, *num_ptr);
        return 0;
    }

    if (strcmp(argv[2], "set") == 0) {
        if (argc < 4) {
            printf("Usage: seq %s set R<n>:<on|off>:<ms> [R<n>:<on|off>:<ms> ...]\n",
                   argv[1]);
            return 1;
        }

        seq_step_t new_steps[SEQ_MAX_STEPS];
        int count = parse_steps(argc, argv, 3, new_steps, SEQ_MAX_STEPS);
        if (count < 0) {
            return 1;
        }

        memcpy(steps, new_steps, count * sizeof(seq_step_t));
        *num_ptr = (uint8_t)count;

        print_sequence(label, steps, *num_ptr);
        printf("(use 'seq save' to persist, 'seq apply' to activate)\n");
        return 0;
    }

    printf("Unknown action: %s\n", argv[2]);
    return 1;
}

void register_cmd_seq(void)
{
    const esp_console_cmd_t cmd = {
        .command = "seq",
        .help    = "Sequence editor: seq <tx|rx> <show|set> | seq <save|apply>",
        .hint    = NULL,
        .func    = &cmd_seq_handler,
    };
    esp_console_cmd_register(&cmd);
}
