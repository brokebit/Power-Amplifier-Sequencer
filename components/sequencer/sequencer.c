#include <string.h>

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/gpio.h"
#include "hw_config.h"
#include "relays.h"
#include "system_state.h"

#include "sequencer.h"

static const char *TAG = "sequencer";

#define EVENT_QUEUE_LEN  16

/* ---------------------------------------------------------
 * Enum ↔ string tables
 * --------------------------------------------------------- */

static const char *s_state_names[] = {
    [SEQ_STATE_RX] = "RX",
    [SEQ_STATE_SEQUENCING_TX] = "SEQ_TX",
    [SEQ_STATE_TX] = "TX",
    [SEQ_STATE_SEQUENCING_RX] = "SEQ_RX",
    [SEQ_STATE_FAULT] = "FAULT",
};

static const char *s_fault_names[] = {
    [SEQ_FAULT_NONE] = "none",
    [SEQ_FAULT_HIGH_SWR] = "HIGH_SWR",
    [SEQ_FAULT_OVER_TEMP1] = "OVER_TEMP1",
    [SEQ_FAULT_OVER_TEMP2] = "OVER_TEMP2",
    [SEQ_FAULT_EMERGENCY] = "EMERGENCY",
};

typedef struct {
    const char  *key;
    seq_fault_t  fault;
} fault_keyword_t;

static const fault_keyword_t s_fault_keywords[] = {
    { "swr", SEQ_FAULT_HIGH_SWR },
    { "temp1", SEQ_FAULT_OVER_TEMP1 },
    { "temp2", SEQ_FAULT_OVER_TEMP2 },
    { "emergency", SEQ_FAULT_EMERGENCY },
};

#define FAULT_KEYWORD_COUNT  (sizeof(s_fault_keywords) / sizeof(s_fault_keywords[0]))

const char *seq_state_name(seq_state_t state)
{
    if (state < sizeof(s_state_names) / sizeof(s_state_names[0]) && s_state_names[state]) {
        return s_state_names[state];
    }
    return "UNKNOWN";
}

const char *seq_fault_name(seq_fault_t fault)
{
    if (fault < sizeof(s_fault_names) / sizeof(s_fault_names[0]) && s_fault_names[fault]) {
        return s_fault_names[fault];
    }
    return "UNKNOWN";
}

bool seq_fault_parse(const char *str, seq_fault_t *out)
{
    for (size_t i = 0; i < FAULT_KEYWORD_COUNT; i++) {
        if (strcmp(str, s_fault_keywords[i].key) == 0) {
            *out = s_fault_keywords[i].fault;
            return true;
        }
    }
    return false;
}

/* ---------------------------------------------------------
 * Module state
 * --------------------------------------------------------- */
static QueueHandle_t s_queue = NULL;
static seq_state_t s_state = SEQ_STATE_RX;
static seq_fault_t s_fault = SEQ_FAULT_NONE;
static app_config_t s_cfg;

/* Config update staging area — written by caller, consumed by task */
static app_config_t s_cfg_pending;
static volatile bool s_cfg_pending_flag = false;
static TaskHandle_t s_cfg_ack_task = NULL;

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
            ESP_LOGW(TAG, "Discarding event %d during sequence", evt.type);
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
    relay_set(s_cfg.pa_relay_id, false);

    /* Execute RX sequence to restore safe relay state */
    run_sequence(s_cfg.rx_steps, s_cfg.rx_num_steps);

    s_fault = fault_code;
    s_state = SEQ_STATE_FAULT;
    system_state_set_sequencer(s_state, s_fault);
}

/* ---------------------------------------------------------
 * Public API
 * --------------------------------------------------------- */

esp_err_t sequencer_init(void)
{
    if (s_queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    config_snapshot(&s_cfg);

    s_queue = xQueueCreate(EVENT_QUEUE_LEN, sizeof(seq_event_t));
    if (s_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create event queue");
        return ESP_ERR_NO_MEM;
    }

    s_state = SEQ_STATE_RX;
    s_fault = SEQ_FAULT_NONE;
    system_state_set_sequencer(s_state, s_fault);

    config_register_apply_cb(sequencer_update_config);

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

esp_err_t sequencer_update_config(const app_config_t *cfg)
{
    if (s_state != SEQ_STATE_RX) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Stage the new config and record caller for ack */
    memcpy(&s_cfg_pending, cfg, sizeof(s_cfg_pending));
    s_cfg_pending_flag = true;
    s_cfg_ack_task = xTaskGetCurrentTaskHandle();

    /* Queue the update event */
    seq_event_t ev = { .type = SEQ_EVENT_CONFIG_UPDATE };
    if (xQueueSend(s_queue, &ev, 0) != pdTRUE) {
        s_cfg_pending_flag = false;
        s_cfg_ack_task = NULL;
        return ESP_FAIL;
    }

    /* Block until the task acks or timeout (PTT raced in) */
    uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
    if (notified == 0) {
        s_cfg_pending_flag = false;
        s_cfg_ack_task = NULL;
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t sequencer_inject_fault(seq_fault_t fault)
{
    seq_event_t ev = {
        .type = (fault == SEQ_FAULT_EMERGENCY)
                    ? SEQ_EVENT_EMERGENCY_PA_OFF
                    : SEQ_EVENT_FAULT,
        .data = (uint32_t)fault,
    };
    if (xQueueSend(s_queue, &ev, 0) != pdTRUE) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t sequencer_clear_fault(void)
{
    if (s_state != SEQ_STATE_FAULT) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Run the configured RX sequence to restore the correct relay state.
     * relays_all_off() would only be correct for the default config;
     * a user-configured RX sequence may leave some relays ON. */
    run_sequence(s_cfg.rx_steps, s_cfg.rx_num_steps);
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
                if (!run_sequence(s_cfg.tx_steps, s_cfg.tx_num_steps)) {
                    break; /* fault re-queued — handled next iteration */
                }
                /* run_sequence() drains the queue and may have consumed a
                 * PTT release.  Re-read the GPIO to reconcile. If PTT is
                 * already released, sequence back to RX immediately rather
                 * than promoting to TX where no release event is left. */
                goto reconcile;

            case SEQ_EVENT_FAULT:
            case SEQ_EVENT_EMERGENCY_PA_OFF:
                emergency_shutdown(evt.type == SEQ_EVENT_FAULT
                                   ? (seq_fault_t)evt.data
                                   : SEQ_FAULT_EMERGENCY);
                break;

            case SEQ_EVENT_CONFIG_UPDATE:
                if (s_cfg_pending_flag) {
                    memcpy(&s_cfg, &s_cfg_pending, sizeof(s_cfg));
                    s_cfg_pending_flag = false;
                    TaskHandle_t ack = s_cfg_ack_task;
                    s_cfg_ack_task = NULL;
                    if (ack) {
                        xTaskNotifyGive(ack);
                    }
                    ESP_LOGI(TAG, "Config updated (TX:%d steps, RX:%d steps)",
                             s_cfg.tx_num_steps, s_cfg.rx_num_steps);
                }
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
                if (!run_sequence(s_cfg.rx_steps, s_cfg.rx_num_steps)) {
                    break; /* fault re-queued — handled next iteration */
                }
                goto reconcile;

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
        continue;

        /*
         * Reconcile FSM with live PTT GPIO after a sequence completes.
         *
         * run_sequence() drains the event queue between steps, discarding
         * PTT edges.  A release that arrives during TX sequencing (or an
         * assert during RX sequencing) is consumed there.  Without this
         * check the FSM would promote to a steady state with no matching
         * event left to trigger the reverse transition — the device gets
         * stuck in TX or misses a TX request.
         *
         * Loop handles rapid PTT toggling: each iteration runs one
         * sequence in the direction needed to match the GPIO, then
         * re-checks.  Bounded by real time — each pass takes at least as
         * long as the relay sequence delays.
         */
reconcile:
        for (;;) {
            bool ptt = (gpio_get_level(HW_PTT_GPIO) == 0);

            if (ptt && s_state != SEQ_STATE_TX) {
                ESP_LOGW(TAG, "PTT still active after sequence — sequencing TX");
                s_state = SEQ_STATE_SEQUENCING_TX;
                system_state_set_sequencer(s_state, s_fault);
                if (!run_sequence(s_cfg.tx_steps, s_cfg.tx_num_steps)) {
                    break; /* fault re-queued */
                }
            } else if (!ptt && s_state != SEQ_STATE_RX) {
                ESP_LOGW(TAG, "PTT released after sequence — sequencing RX");
                s_state = SEQ_STATE_SEQUENCING_RX;
                system_state_set_sequencer(s_state, s_fault);
                if (!run_sequence(s_cfg.rx_steps, s_cfg.rx_num_steps)) {
                    break; /* fault re-queued */
                }
            } else {
                /* GPIO matches desired state — settle */
                s_state = ptt ? SEQ_STATE_TX : SEQ_STATE_RX;
                system_state_set_sequencer(s_state, s_fault);
                ESP_LOGI(TAG, "%s active", ptt ? "TX" : "RX");
                break;
            }
        }
    }
}
