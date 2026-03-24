#include <stdio.h>
#include <string.h>

#include "esp_console.h"

#include "config.h"
#include "hw_config.h"

#include "cli.h"

/* ---- config show -------------------------------------------------------- */

static void print_config(const app_config_t *cfg)
{
    char label[24];

    printf("Fault settings:\n");
    printf("  swr_threshold   = %.1f\n", cfg->swr_fault_threshold);
    printf("  temp1_threshold = %.1f C\n", cfg->temp1_fault_threshold_c);
    printf("  temp2_threshold = %.1f C\n", cfg->temp2_fault_threshold_c);
    config_relay_label(cfg, cfg->pa_relay_id, label, sizeof(label));
    printf("  pa_relay        = %s\n", label);

    printf("Power calibration:\n");
    printf("  fwd_cal         = %.4f\n", cfg->fwd_power_cal_factor);
    printf("  ref_cal         = %.4f\n", cfg->ref_power_cal_factor);

    printf("Thermistor:\n");
    printf("  therm_beta      = %.0f\n", cfg->thermistor_beta);
    printf("  therm_r0        = %.0f ohms\n", cfg->thermistor_r0_ohms);
    printf("  therm_rseries   = %.0f ohms\n", cfg->thermistor_r_series_ohms);

    printf("Relay names:\n");
    for (int i = 0; i < HW_RELAY_COUNT; i++) {
        if (cfg->relay_names[i][0] != '\0') {
            printf("  R%d = %s\n", i + 1, cfg->relay_names[i]);
        } else {
            printf("  R%d = (none)\n", i + 1);
        }
    }

    printf("TX sequence (%d steps):\n", cfg->tx_num_steps);
    for (int i = 0; i < cfg->tx_num_steps; i++) {
        config_relay_label(cfg, cfg->tx_steps[i].relay_id, label, sizeof(label));
        printf("  %d: %-12s %s  %dms\n", i + 1, label,
               cfg->tx_steps[i].state ? "ON" : "OFF",
               cfg->tx_steps[i].delay_ms);
    }

    printf("RX sequence (%d steps):\n", cfg->rx_num_steps);
    for (int i = 0; i < cfg->rx_num_steps; i++) {
        config_relay_label(cfg, cfg->rx_steps[i].relay_id, label, sizeof(label));
        printf("  %d: %-12s %s  %dms\n", i + 1, label,
               cfg->rx_steps[i].state ? "ON" : "OFF",
               cfg->rx_steps[i].delay_ms);
    }
}

/* ---- config set --------------------------------------------------------- */

static int set_config_value(app_config_t *cfg, const char *key, const char *val_str)
{
    char err_msg[64] = {0};
    esp_err_t err = config_set_by_key(cfg, key, val_str, err_msg, sizeof(err_msg));
    if (err == ESP_OK) {
        printf("%s = %s\n", key, val_str);
        return 0;
    }
    printf("%s\n", err_msg);
    return 1;
}

/* ---- command handler ---------------------------------------------------- */

static int cmd_config_handler(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: config <show|set|save|defaults>\n");
        return 1;
    }

    app_config_t *cfg = cli_get_config();

    if (strcmp(argv[1], "show") == 0) {
        print_config(cfg);
        return 0;
    }

    if (strcmp(argv[1], "set") == 0) {
        if (argc < 4) {
            printf("Usage: config set <key> <value>\n");
            printf("Keys: swr_threshold temp1_threshold temp2_threshold "
                   "fwd_cal ref_cal therm_beta therm_r0 therm_rseries pa_relay\n");
            return 1;
        }
        return set_config_value(cfg, argv[2], argv[3]);
    }

    if (strcmp(argv[1], "save") == 0) {
        esp_err_t ret = config_save(cfg);
        if (ret == ESP_OK) {
            printf("Config saved to NVS\n");
        } else {
            printf("Save failed: %s\n", esp_err_to_name(ret));
        }
        return (ret == ESP_OK) ? 0 : 1;
    }

    if (strcmp(argv[1], "defaults") == 0) {
        config_defaults(cfg);
        printf("Config reset to factory defaults (in memory only)\n");
        printf("Use 'config save' to persist, 'config show' to review\n");
        return 0;
    }

    printf("Unknown subcommand: %s\n", argv[1]);
    return 1;
}

void register_cmd_config(void)
{
    const esp_console_cmd_t cmd = {
        .command = "config",
        .help    = "Config management: config <show|set|save|defaults>",
        .hint    = NULL,
        .func    = &cmd_config_handler,
    };
    esp_console_cmd_register(&cmd);
}
