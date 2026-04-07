#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_console.h"
#include "esp_err.h"

#include "ads1115.h"
#include "config.h"
#include "monitor.h"
#include "system_state.h"

static const char *s_ch_names[] = {
    [ADS1115_CHANNEL_0] = "AIN0 (fwd power)",
    [ADS1115_CHANNEL_1] = "AIN1 (ref power)",
    [ADS1115_CHANNEL_2] = "AIN2 (temp1)",
    [ADS1115_CHANNEL_3] = "AIN3 (temp2)"
};

static void read_and_print(int chip, ads1115_channel_t ch)
{
    float voltage;
    esp_err_t ret = monitor_read_channel(chip, ch, &voltage);
    if (ret == ESP_OK) {
        printf("  CH%d %-18s  %.4f V\n", ch, s_ch_names[ch], voltage);
    } else {
        printf("  CH%d %-18s  error: %s\n", ch, s_ch_names[ch], esp_err_to_name(ret));
    }
}

static int cmd_adc_handler(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: adc <read <chip> <ch> | scan | name [<0-3>] [<label>]>\n");
        return 1;
    }

    if (strcmp(argv[1], "scan") == 0) {
        system_state_t st;
        system_state_get(&st);
        printf("ADC scan chip 0 (0x48) — corrected voltages:\n");
        printf("  CH0 AIN0  %.4f V\n", st.adc_0_ch0);
        printf("  CH1 AIN1  %.4f V\n", st.adc_0_ch1);
        printf("  CH2 AIN2  %.4f V\n", st.adc_0_ch2);
        printf("  CH3 AIN3  %.4f V\n", st.adc_0_ch3);
        printf("ADC scan chip 1 (0x49) — raw voltages:\n");
        printf("  CH0 AIN0 (fwd power)  %.4f V\n", st.adc_1_ch0);
        printf("  CH1 AIN1 (ref power)  %.4f V\n", st.adc_1_ch1);
        printf("  CH2 AIN2 (temp1)      %.4f V\n", st.adc_1_ch2);
        printf("  CH3 AIN3 (temp2)      %.4f V\n", st.adc_1_ch3);
        return 0;
    }

    if (strcmp(argv[1], "read") == 0) {
        if (argc < 4) {
            printf("Usage: adc read <chip 0|1> <ch 0-3>\n");
            return 1;
        }
        char *end;
        long chip = strtol(argv[2], &end, 10);
        if (end == argv[2] || *end != '\0' || chip < 0 || chip > 1) {
            printf("Invalid chip: %s (expected 0 or 1)\n", argv[2]);
            return 1;
        }
        long ch = strtol(argv[3], &end, 10);
        if (end == argv[3] || *end != '\0' || ch < 0 || ch > 3) {
            printf("Invalid channel: %s (expected 0-3)\n", argv[3]);
            return 1;
        }
        read_and_print((int)chip, (ads1115_channel_t)ch);
        return 0;
    }

    if (strcmp(argv[1], "name") == 0) {
        if (argc < 3) {
            /* Show all names */
            app_config_t snap;
            config_snapshot(&snap);
            for (int i = 0; i < 4; i++) {
                if (snap.adc_0_ch_names[i][0] != '\0') {
                    printf("  CH%d = %s\n", i, snap.adc_0_ch_names[i]);
                } else {
                    printf("  CH%d = (none)\n", i);
                }
            }
            return 0;
        }
        char *end;
        long ch = strtol(argv[2], &end, 10);
        if (end == argv[2] || *end != '\0' || ch < 0 || ch > 3) {
            printf("Invalid channel: %s (expected 0-3)\n", argv[2]);
            return 1;
        }
        char err_msg[64];
        const char *name = (argc >= 4) ? argv[3] : NULL;
        esp_err_t ret = config_set_adc_ch_name((uint8_t)ch, name,
                                                err_msg, sizeof(err_msg));
        if (ret != ESP_OK) {
            printf("%s\n", err_msg);
            return 1;
        }
        if (name) {
            printf("CH%ld = %s\n", ch, name);
        } else {
            printf("CH%ld name cleared\n", ch);
        }
        printf("Use 'config save' to persist\n");
        return 0;
    }

    printf("Unknown subcommand: %s\n", argv[1]);
    return 1;
}

void cli_register_cmd_adc(void)
{
    const esp_console_cmd_t cmd = {
        .command = "adc",
        .help = "ADC reading: adc <read <chip> <ch> | scan | name [<0-3>] [<label>]>",
        .hint = NULL,
        .func = &cmd_adc_handler,
    };
    esp_console_cmd_register(&cmd);
}
