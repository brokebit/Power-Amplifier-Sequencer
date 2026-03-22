#include "sequencer.h"
#include "system_state.h"
#include "relays.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>

static const char *TAG = "sequencer";

#define EVENT_QUEUE_LEN  16

/* ---------------------------------------------------------
 * Module state
 * --------------------------------------------------------- */
static QueueHandle_t s_queue        = NULL;
static seq_state_t   s_state        = SEQ_STATE_RX;
static seq_fault_t   s_fault        = SEQ_FAULT_NONE;
static app_config_t  s_cfg;

/* ---------------------------------------------------------
 * Helpers
 * --------------------------------------------------------- */

/**
 * Run one relay sequence (TX or RX steps).
 * Polls the event queue between steps; if a FAULT or EMERGENCY arrives
 * mid-sequence it returns false immediately so the caller can emergency-shutdown.
 * Returns true if sequence completed normally, false if aborted.
 */
static bool run_sequence(const seq_step_t *steps, uint8_t num_steps)
{
    for (uint8_t i = 0; i < num_steps; i++) {
        /* Drain urgent events before each step */
        seq_event_t evt;
        while (xQueueReceive(s_queue, &evt, 0) == pdTRUE) {
            if (evt.type == SEQ_EVENT_FAULT || evt.type == SEQ_EVENT_EMERGENCY_PA_OFF) {
                ESP_LOGW(TAG, "Urgent event %d during sequence — aborting", evt.type);
                /* Put it back so the main loop handles the state transition */
                xQueueSendToFront(s_queue, &evt, 0);
                return false;
            }
            /* Drop PTT events that arrived while mid-sequence */
        }

        relay_set(steps[i].relay_id, steps[i].state != 0);

        if (steps[i].delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(steps[i].delay_ms));
        }
    }
    return true;
}

/**
 * Emergency shutdown: PA off immediately, then full RX sequence, latch fault.
 */
static void emergency_shutdown(seq_fault_t fault_code)
{
    ESP_LOGE(TAG, "Emergency shutdown — fault %d", fault_code);

    /* PA off first, no delay */
    relay_set(2, false);

    /* Execute RX sequence to restore safe relay state */
    run_sequence(s_cfg.rx_steps, s_cfg.rx_num_steps);

    s_fault = fault_code;
    s_state = SEQ_STATE_FAULT;
    system_state_set_sequencer(s_state, s_fault);
}

/* ---------------------------------------------------------
 * Public API
 * --------------------------------------------------------- */

esp_err_t sequencer_init(const app_config_t *cfg)
{
    if (s_queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    memcpy(&s_cfg, cfg, sizeof(app_config_t));

    s_queue = xQueueCreate(EVENT_QUEUE_LEN, sizeof(seq_event_t));
    if (s_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create event queue");
        return ESP_ERR_NO_MEM;
    }

    s_state = SEQ_STATE_RX;
    s_fault = SEQ_FAULT_NONE;
    system_state_set_sequencer(s_state, s_fault);

    ESP_LOGI(TAG, "Sequencer initialised");
    return ESP_OK;
}

QueueHandle_t sequencer_get_event_queue(void)
{
    return s_queue;
}

seq_state_t sequencer_get_state(void)
{
    return s_state;
}

seq_fault_t sequencer_get_fault(void)
{
    return s_fault;
}

esp_err_t sequencer_clear_fault(void)
{
    if (s_state != SEQ_STATE_FAULT) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Ensure all relays are in the RX-safe state before clearing */
    relays_all_off();
    s_fault = SEQ_FAULT_NONE;
    s_state = SEQ_STATE_RX;
    system_state_set_sequencer(s_state, s_fault);
    ESP_LOGI(TAG, "Fault cleared — returning to RX");
    return ESP_OK;
}

/* ---------------------------------------------------------
 * Task
 * --------------------------------------------------------- */

void sequencer_task(void *arg)
{
    ESP_LOGI(TAG, "Task started, state=RX");

    for (;;) {
        seq_event_t evt;
        xQueueReceive(s_queue, &evt, portMAX_DELAY);

        switch (s_state) {

        /* ---- RX (idle) ---- */
        case SEQ_STATE_RX:
            switch (evt.type) {
            case SEQ_EVENT_PTT_ASSERT:
                ESP_LOGI(TAG, "PTT assert → sequencing TX");
                s_state = SEQ_STATE_SEQUENCING_TX;
                system_state_set_sequencer(s_state, s_fault);
                if (run_sequence(s_cfg.tx_steps, s_cfg.tx_num_steps)) {
                    s_state = SEQ_STATE_TX;
                    system_state_set_sequencer(s_state, s_fault);
                    ESP_LOGI(TAG, "TX active");
                } else {
                    /* Fault event arrived mid-sequence — handled next iteration */
                }
                break;

            case SEQ_EVENT_FAULT:
            case SEQ_EVENT_EMERGENCY_PA_OFF:
                emergency_shutdown(evt.type == SEQ_EVENT_FAULT
                                   ? (seq_fault_t)evt.data
                                   : SEQ_FAULT_EMERGENCY);
                break;

            default:
                break;
            }
            break;

        /* ---- TX active ---- */
        case SEQ_STATE_TX:
            switch (evt.type) {
            case SEQ_EVENT_PTT_RELEASE:
                ESP_LOGI(TAG, "PTT release → sequencing RX");
                s_state = SEQ_STATE_SEQUENCING_RX;
                system_state_set_sequencer(s_state, s_fault);
                if (run_sequence(s_cfg.rx_steps, s_cfg.rx_num_steps)) {
                    s_state = SEQ_STATE_RX;
                    system_state_set_sequencer(s_state, s_fault);
                    ESP_LOGI(TAG, "RX active");
                }
                break;

            case SEQ_EVENT_FAULT:
            case SEQ_EVENT_EMERGENCY_PA_OFF:
                emergency_shutdown(evt.type == SEQ_EVENT_FAULT
                                   ? (seq_fault_t)evt.data
                                   : SEQ_FAULT_EMERGENCY);
                break;

            default:
                break;
            }
            break;

        /* ---- Mid-sequence states ---- */
        case SEQ_STATE_SEQUENCING_TX:
        case SEQ_STATE_SEQUENCING_RX:
            /*
             * These states are only set briefly while run_sequence() executes.
             * If an event arrives here it means run_sequence() put a fault back
             * on the queue and returned; handle it now.
             */
            if (evt.type == SEQ_EVENT_FAULT || evt.type == SEQ_EVENT_EMERGENCY_PA_OFF) {
                emergency_shutdown(evt.type == SEQ_EVENT_FAULT
                                   ? (seq_fault_t)evt.data
                                   : SEQ_FAULT_EMERGENCY);
            }
            break;

        /* ---- Fault (latched) ---- */
        case SEQ_STATE_FAULT:
            /* All events ignored until sequencer_clear_fault() is called externally */
            ESP_LOGD(TAG, "Event %d ignored — latched fault %d", evt.type, s_fault);
            break;
        }
    }
}
