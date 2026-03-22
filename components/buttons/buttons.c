#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"

#include "hw_config.h"
#include "sequencer.h"

#include "buttons.h"

static const char *TAG = "buttons";

/* ---------------------------------------------------------
 * Per-button state
 * --------------------------------------------------------- */
typedef struct {
    int              gpio;
    uint8_t          id;          /* 1-indexed */
    esp_timer_handle_t timer;
    button_cb_t      cb;          /* NULL for BTN1 (handled internally) */
} button_ctx_t;

static button_ctx_t s_btns[HW_BUTTON_COUNT];
static const int btn_gpios[HW_BUTTON_COUNT] = HW_BUTTON_GPIOS;

/* ---------------------------------------------------------
 * Timer callback — runs in esp_timer task context (not ISR)
 * --------------------------------------------------------- */
static void debounce_timer_cb(void *arg)
{
    button_ctx_t *btn = (button_ctx_t *)arg;

    /* Confirm button is still pressed (active low) */
    if (gpio_get_level(btn->gpio) != 0) {
        /* Spurious — re-arm interrupt and return */
        gpio_intr_enable(btn->gpio);
        return;
    }

    ESP_LOGD(TAG, "BTN%d confirmed press", btn->id);

    if (btn->id == 1) {
        /* Emergency PA Off → sequencer queue */
        seq_event_t evt = {
            .type = SEQ_EVENT_EMERGENCY_PA_OFF,
            .data = 0,
        };
        xQueueSend(sequencer_get_event_queue(), &evt, 0);
        ESP_LOGW(TAG, "BTN1: Emergency PA Off sent");
    } else if (btn->cb != NULL) {
        btn->cb(btn->id);
    }

    /* Re-arm GPIO interrupt for next press */
    gpio_intr_enable(btn->gpio);
}

/* ---------------------------------------------------------
 * ISR — falling edge only
 * --------------------------------------------------------- */
static void IRAM_ATTR btn_isr_handler(void *arg)
{
    button_ctx_t *btn = (button_ctx_t *)arg;

    /* Disable this GPIO's interrupt until debounce timer fires */
    gpio_intr_disable(btn->gpio);

    /* Start/restart debounce one-shot timer */
    esp_timer_stop(btn->timer);
    esp_timer_start_once(btn->timer, BUTTONS_DEBOUNCE_MS * 1000ULL);
}

/* ---------------------------------------------------------
 * Public API
 * --------------------------------------------------------- */

esp_err_t buttons_init(void)
{
    /* Configure all button GPIOs */
    gpio_config_t cfg = {
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
        .pin_bit_mask = 0,
    };
    for (int i = 0; i < HW_BUTTON_COUNT; i++) {
        cfg.pin_bit_mask |= (1ULL << btn_gpios[i]);
    }

    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Install GPIO ISR service if not already done */
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Initialise per-button context, create debounce timers, register ISRs */
    for (int i = 0; i < HW_BUTTON_COUNT; i++) {
        s_btns[i].gpio = btn_gpios[i];
        s_btns[i].id   = (uint8_t)(i + 1);
        s_btns[i].cb   = NULL;

        esp_timer_create_args_t timer_args = {
            .callback        = debounce_timer_cb,
            .arg             = &s_btns[i],
            .dispatch_method = ESP_TIMER_TASK,
            .name            = "btn_debounce",
        };
        err = esp_timer_create(&timer_args, &s_btns[i].timer);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_timer_create BTN%d failed: %s", i + 1, esp_err_to_name(err));
            return err;
        }

        err = gpio_isr_handler_add(btn_gpios[i], btn_isr_handler, &s_btns[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "gpio_isr_handler_add BTN%d failed: %s", i + 1, esp_err_to_name(err));
            return err;
        }
    }

    ESP_LOGI(TAG, "%d buttons initialised (debounce %d ms)", HW_BUTTON_COUNT, BUTTONS_DEBOUNCE_MS);
    return ESP_OK;
}

esp_err_t button_register_cb(uint8_t button_id, button_cb_t cb)
{
    if (button_id < 2 || button_id > HW_BUTTON_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    s_btns[button_id - 1].cb = cb;
    return ESP_OK;
}
