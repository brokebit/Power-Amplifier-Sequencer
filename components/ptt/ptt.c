#include <stdbool.h>

#include "esp_log.h"

#include "driver/gpio.h"
#include "hw_config.h"
#include "sequencer.h"
#include "system_state.h"

#include "ptt.h"

static const char *TAG = "ptt";

static void IRAM_ATTR ptt_isr_handler(void *arg)
{
    /* Active low: GPIO low = PTT asserted, GPIO high = PTT released */
    int level = gpio_get_level(HW_PTT_GPIO);
    system_state_set_ptt(level == 0);

    seq_event_t evt = {
        .type = (level == 0) ? SEQ_EVENT_PTT_ASSERT : SEQ_EVENT_PTT_RELEASE,
        .data = 0,
    };

    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(sequencer_get_event_queue(), &evt, &woken);
    portYIELD_FROM_ISR(woken);
}

esp_err_t ptt_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << HW_PTT_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };

    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Install GPIO ISR service (no-op if already installed by another driver) */
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(err));
        return err;
    }

    err = gpio_isr_handler_add(HW_PTT_GPIO, ptt_isr_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_isr_handler_add failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "PTT driver initialised on GPIO %d (active=%s)",
             HW_PTT_GPIO, ptt_is_active() ? "YES" : "no");
    return ESP_OK;
}

bool ptt_is_active(void)
{
    return gpio_get_level(HW_PTT_GPIO) == 0;
}
