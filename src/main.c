#include "config.h"
#include "relays.h"
#include "sequencer.h"
#include "system_state.h"
#include "ptt.h"
#include "buttons.h"
#include "monitor.h"
#include "esp_log.h"

static const char *TAG = "main";

void app_main(void)
{
    /* --- load config from NVS (writes defaults on first boot) --- */
    app_config_t cfg;
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

    /* Print system state once per second */
    static const char *state_names[] = {
        [SEQ_STATE_RX]            = "RX",
        [SEQ_STATE_SEQUENCING_TX] = "SEQ_TX",
        [SEQ_STATE_TX]            = "TX",
        [SEQ_STATE_SEQUENCING_RX] = "SEQ_RX",
        [SEQ_STATE_FAULT]         = "FAULT",
    };
    static const char *fault_names[] = {
        [SEQ_FAULT_NONE]       = "none",
        [SEQ_FAULT_HIGH_SWR]   = "HIGH_SWR",
        [SEQ_FAULT_OVER_TEMP1] = "OVER_TEMP1",
        [SEQ_FAULT_OVER_TEMP2] = "OVER_TEMP2",
        [SEQ_FAULT_EMERGENCY]  = "EMERGENCY",
    };

    system_state_t ss;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        system_state_get(&ss);

        ESP_LOGI(TAG,
            "PTT:%s  State:%-6s  Fault:%-10s  "
            "Relays:0x%02X  "
            "Fwd:%.1fW  Ref:%.1fW  SWR:%.1f  "
            "T1:%.1f°C  T2:%.1f°C",
            ss.ptt_active ? "ON " : "off",
            state_names[ss.seq_state],
            fault_names[ss.seq_fault],
            ss.relay_states,
            ss.fwd_power_w, ss.ref_power_w, ss.swr,
            ss.temp1_c, ss.temp2_c);
    }
}
