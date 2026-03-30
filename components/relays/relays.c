#include "esp_log.h"

#include "driver/gpio.h"

#include "hw_config.h"
#include "system_state.h"

#include "relays.h"

static const char *TAG = "relays";

static const int s_relay_gpios[HW_RELAY_COUNT] = HW_RELAY_GPIOS;

esp_err_t relays_init(void)
{
    gpio_config_t cfg = {
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
        .pin_bit_mask = 0,
    };

    for (int i = 0; i < HW_RELAY_COUNT; i++) {
        cfg.pin_bit_mask |= (1ULL << s_relay_gpios[i]);
    }

    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Start with all relays off */
    relays_all_off();
    ESP_LOGI(TAG, "Relay GPIOs initialised, all off");
    return ESP_OK;
}

esp_err_t relay_set(uint8_t relay_id, bool on)
{
    if (relay_id < 1 || relay_id > HW_RELAY_COUNT) {
        ESP_LOGE(TAG, "relay_set: invalid relay_id %d (must be 1-%d)", relay_id, HW_RELAY_COUNT);
        return ESP_ERR_INVALID_ARG;
    }

    int gpio = s_relay_gpios[relay_id - 1];
    gpio_set_level(gpio, on ? 1 : 0);
    system_state_set_relay(relay_id, on);
    ESP_LOGD(TAG, "Relay %d (GPIO %d) -> %s", relay_id, gpio, on ? "ON" : "OFF");
    return ESP_OK;
}

void relays_all_off(void)
{
    for (int i = 0; i < HW_RELAY_COUNT; i++) {
        gpio_set_level(s_relay_gpios[i], 0);
    }
    system_state_set_relays_all_off();
    ESP_LOGD(TAG, "All relays OFF");
}
