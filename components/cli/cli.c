#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"

#include "cli.h"

static const char *TAG = "cli";

/* Forward declarations — each cmd_*.c file provides a register function */
void cli_register_cmd_status(void);
void cli_register_cmd_system(void);
void cli_register_cmd_log(void);
void cli_register_cmd_config(void);
void cli_register_cmd_relay(void);
void cli_register_cmd_fault(void);
void cli_register_cmd_seq(void);
void cli_register_cmd_adc(void);
void cli_register_cmd_monitor(void);
void cli_register_cmd_wifi(void);
void cli_register_cmd_ota(void);

esp_err_t cli_init(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "seq> ";
    repl_config.max_cmdline_length = 256;
    repl_config.task_stack_size = 16384;  /* OTA needs ~12KB for TLS handshake */

    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();

    esp_err_t ret = esp_console_new_repl_uart(&uart_config, &repl_config, &repl);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create REPL: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Register commands before starting the REPL */
    cli_register_cmd_status();
    cli_register_cmd_system();
    cli_register_cmd_log();
    cli_register_cmd_config();
    cli_register_cmd_relay();
    cli_register_cmd_fault();
    cli_register_cmd_seq();
    cli_register_cmd_adc();
    cli_register_cmd_monitor();
    cli_register_cmd_wifi();
    cli_register_cmd_ota();

    /* Suppress logging by default so the REPL prompt stays clean */
    esp_log_level_set("*", ESP_LOG_NONE);

    ret = esp_console_start_repl(repl);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start REPL: %s", esp_err_to_name(ret));
    }

    return ret;
}
