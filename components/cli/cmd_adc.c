#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_console.h"
#include "esp_err.h"

#include "ads1115.h"
#include "monitor.h"

static const char *s_ch_names[] = {
    [ADS1115_CHANNEL_0] = "AIN0 (fwd power)",
    [ADS1115_CHANNEL_1] = "AIN1 (ref power)",
    [ADS1115_CHANNEL_2] = "AIN2 (temp1)",
    [ADS1115_CHANNEL_3] = "AIN3 (temp2)"
};

static void read_and_print(ads1115_channel_t ch)
{
    float voltage;
    esp_err_t ret = monitor_read_channel(ch, &voltage);
    if (ret == ESP_OK) {
        printf("  CH%d %-18s  %.4f V\n", ch, s_ch_names[ch], voltage);
    } else {
        printf("  CH%d %-18s  error: %s\n", ch, s_ch_names[ch], esp_err_to_name(ret));
    }
}

static int cmd_adc_handler(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: adc <read <0-3> | scan>\n");
        return 1;
    }

    if (strcmp(argv[1], "scan") == 0) {
        printf("ADC scan (chip 1, 0x49):\n");
        for (int ch = 0; ch <= 3; ch++) {
            read_and_print((ads1115_channel_t)ch);
        }
        return 0;
    }

    if (strcmp(argv[1], "read") == 0) {
        if (argc < 3) {
            printf("Usage: adc read <0-3>\n");
            return 1;
        }
        char *end;
        long ch = strtol(argv[2], &end, 10);
        if (end == argv[2] || *end != '\0' || ch < 0 || ch > 3) {
            printf("Invalid channel: %s (expected 0-3)\n", argv[2]);
            return 1;
        }
        read_and_print((ads1115_channel_t)ch);
        return 0;
    }

    printf("Unknown subcommand: %s\n", argv[1]);
    return 1;
}

void cli_register_cmd_adc(void)
{
    const esp_console_cmd_t cmd = {
        .command = "adc",
        .help = "ADC reading: adc <read <0-3> | scan>",
        .hint = NULL,
        .func = &cmd_adc_handler,
    };
    esp_console_cmd_register(&cmd);
}
