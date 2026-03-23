#include "esp_console.h"
#include "esp_log.h"

#include "cli.h"

static const char *TAG = "cli";

static app_config_t *s_cfg;

app_config_t *cli_get_config(void)
{
    return s_cfg;
}

/* Forward declarations — each cmd_*.c file provides a register function */
void register_cmd_status(void);
void register_cmd_system(void);
void register_cmd_log(void);
void register_cmd_config(void);
void register_cmd_relay(void);
void register_cmd_fault(void);
void register_cmd_seq(void);
void register_cmd_adc(void);
void register_cmd_monitor(void);
void register_cmd_wifi(void);

esp_err_t cli_init(app_config_t *cfg)
{
    s_cfg = cfg;

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "seq> ";
    repl_config.max_cmdline_length = 256;

    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();

    esp_err_t ret = esp_console_new_repl_uart(&uart_config, &repl_config, &repl);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create REPL: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Register commands before starting the REPL */
    register_cmd_status();
    register_cmd_system();
    register_cmd_log();
    register_cmd_config();
    register_cmd_relay();
    register_cmd_fault();
    register_cmd_seq();
    register_cmd_adc();
    register_cmd_monitor();
    register_cmd_wifi();

    /* Suppress logging by default so the REPL prompt stays clean */
    esp_log_level_set("*", ESP_LOG_NONE);

    ret = esp_console_start_repl(repl);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start REPL: %s", esp_err_to_name(ret));
    }

    return ret;
}
