#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "buttons.h"
#include "cli.h"
#include "config.h"
#include "monitor.h"
#include "ptt.h"
#include "relays.h"
#include "sequencer.h"

static const char *TAG = "main";

void app_main(void)
{
    /* --- load config from NVS (writes defaults on first boot) --- */
    static app_config_t cfg;
    ESP_ERROR_CHECK(config_init(&cfg));

    /* --- relays: configure GPIO outputs before sequencer drives them --- */
    ESP_ERROR_CHECK(relays_init());

    /* --- sequencer: creates the event queue --- */
    ESP_ERROR_CHECK(sequencer_init(&cfg));

    /* --- PTT: installs gpio_isr_service, arms PTT interrupt --- */
    ESP_ERROR_CHECK(ptt_init());

    /* --- buttons: uses the already-installed gpio_isr_service --- */
    ESP_ERROR_CHECK(buttons_init());

    /* --- monitor: I2C bus, both ADS1115s, ALERT/RDY ISRs --- */
    ESP_ERROR_CHECK(monitor_init(&cfg));

    /* --- start tasks --- */
    xTaskCreate(sequencer_task, "sequencer", 4096, NULL, 10, NULL);
    xTaskCreate(monitor_task,   "monitor",   4096, NULL,  7, NULL);

    ESP_LOGI(TAG, "all components initialised");

    /* --- CLI REPL on UART0 (runs as its own task) --- */
    ESP_ERROR_CHECK(cli_init(&cfg));
}
