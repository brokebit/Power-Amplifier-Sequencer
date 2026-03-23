#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

typedef struct {
    const char *key;
    size_t      offset;
    float       min;
    float       max;
} config_key_t;

#define CFG_KEY(name, field, lo, hi) \
    { name, offsetof(app_config_t, field), lo, hi }

static const config_key_t s_keys[] = {
    CFG_KEY("swr_threshold",  swr_fault_threshold,       1.0f,  99.0f),
    CFG_KEY("temp1_threshold", temp1_fault_threshold_c,    0.0f, 200.0f),
    CFG_KEY("temp2_threshold", temp2_fault_threshold_c,    0.0f, 200.0f),
    CFG_KEY("fwd_cal",        fwd_power_cal_factor,       0.001f, 1000.0f),
    CFG_KEY("ref_cal",        ref_power_cal_factor,       0.001f, 1000.0f),
    CFG_KEY("therm_beta",     thermistor_beta,            1.0f, 100000.0f),
    CFG_KEY("therm_r0",       thermistor_r0_ohms,         1.0f, 10000000.0f),
    CFG_KEY("therm_rseries",  thermistor_r_series_ohms,   1.0f, 10000000.0f),
};

#define NUM_KEYS (sizeof(s_keys) / sizeof(s_keys[0]))

/* Integer config keys (uint8_t fields) */
typedef struct {
    const char *key;
    size_t      offset;
    int         min;
    int         max;
} config_int_key_t;

#define CFG_INT_KEY(name, field, lo, hi) \
    { name, offsetof(app_config_t, field), lo, hi }

static const config_int_key_t s_int_keys[] = {
    CFG_INT_KEY("pa_relay", pa_relay_id, 1, 6),
};

#define NUM_INT_KEYS (sizeof(s_int_keys) / sizeof(s_int_keys[0]))

static int set_config_value(app_config_t *cfg, const char *key, const char *val_str)
{
    for (size_t i = 0; i < NUM_KEYS; i++) {
        if (strcmp(s_keys[i].key, key) == 0) {
            char *end;
            float val = strtof(val_str, &end);
            if (end == val_str || *end != '\0') {
                printf("Invalid number: %s\n", val_str);
                return 1;
            }
            if (val < s_keys[i].min || val > s_keys[i].max) {
                printf("Out of range [%.3g .. %.3g]: %s\n",
                       s_keys[i].min, s_keys[i].max, val_str);
                return 1;
            }
            float *field = (float *)((uint8_t *)cfg + s_keys[i].offset);
            *field = val;
            printf("%s = %.4g\n", key, val);
            return 0;
        }
    }

    for (size_t i = 0; i < NUM_INT_KEYS; i++) {
        if (strcmp(s_int_keys[i].key, key) == 0) {
            char *end;
            long val = strtol(val_str, &end, 10);
            if (end == val_str || *end != '\0') {
                printf("Invalid integer: %s\n", val_str);
                return 1;
            }
            if (val < s_int_keys[i].min || val > s_int_keys[i].max) {
                printf("Out of range [%d .. %d]: %s\n",
                       s_int_keys[i].min, s_int_keys[i].max, val_str);
                return 1;
            }
            uint8_t *field = (uint8_t *)cfg + s_int_keys[i].offset;
            *field = (uint8_t)val;
            printf("%s = %d\n", key, (int)*field);
            return 0;
        }
    }

    printf("Unknown key: %s\nValid keys:", key);
    for (size_t i = 0; i < NUM_KEYS; i++) {
        printf(" %s", s_keys[i].key);
    }
    for (size_t i = 0; i < NUM_INT_KEYS; i++) {
        printf(" %s", s_int_keys[i].key);
    }
    printf("\n");
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
            printf("Keys:");
            for (size_t i = 0; i < NUM_KEYS; i++) {
                printf(" %s", s_keys[i].key);
            }
            for (size_t i = 0; i < NUM_INT_KEYS; i++) {
                printf(" %s", s_int_keys[i].key);
            }
            printf("\n");
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
