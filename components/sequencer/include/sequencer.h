#pragma once

#include "esp_err.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "config.h"

/* =========================================================
 * sequencer.h — Core RF PA sequencer state machine
 *
 * The sequencer owns the central event queue. PTT ISR, button
 * ISR, and monitor_task all send events to this queue.
 * sequencer_task() is the sole consumer.
 * ========================================================= */

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------
 * Fault codes — carried in seq_event_t.data for FAULT events
 * and stored for display / clear-fault logic.
 * --------------------------------------------------------- */
typedef enum
{
    SEQ_FAULT_NONE = 0,
    SEQ_FAULT_HIGH_SWR = 1,
    SEQ_FAULT_OVER_TEMP1 = 2,
    SEQ_FAULT_OVER_TEMP2 = 3,
    SEQ_FAULT_EMERGENCY = 4
} seq_fault_t;

/* ---------------------------------------------------------
 * Event types
 * --------------------------------------------------------- */
typedef enum
{
    SEQ_EVENT_PTT_ASSERT,      /* PTT line went active (low) */
    SEQ_EVENT_PTT_RELEASE,     /* PTT line released (high) */
    SEQ_EVENT_FAULT,           /* Fault detected; data = seq_fault_t */
    SEQ_EVENT_EMERGENCY_PA_OFF, /* Emergency button pressed */
    SEQ_EVENT_CONFIG_UPDATE     /* Config apply — handled only in RX state */
} seq_event_type_t;

typedef struct
{
    seq_event_type_t type;
    uint32_t data; /* Optional payload, e.g. seq_fault_t for FAULT events */
} seq_event_t;

/* ---------------------------------------------------------
 * Sequencer states
 * --------------------------------------------------------- */
typedef enum
{
    SEQ_STATE_RX,            /* Idle -- RX path active */
    SEQ_STATE_SEQUENCING_TX, /* Executing TX relay sequence */
    SEQ_STATE_TX,            /* TX active */
    SEQ_STATE_SEQUENCING_RX, /* Executing RX relay sequence */
    SEQ_STATE_FAULT          /* Latched fault -- manual clear required */
} seq_state_t;

/* ---------------------------------------------------------
 * API
 * --------------------------------------------------------- */

/**
 * Initialise the sequencer: creates the event queue, snapshots config.
 * Call after config_init() and before starting sequencer_task or any producer.
 */
esp_err_t sequencer_init(void);

/**
 * FreeRTOS task entry point. Register with xTaskCreate at priority 10,
 * stack 4096. Pass NULL as pvParameters.
 *
 *   xTaskCreate(sequencer_task, "sequencer", 4096, NULL, 10, NULL);
 */
void sequencer_task(void *arg);

/**
 * Return the event queue handle for use by producers (ptt, buttons, monitor).
 * Valid only after sequencer_init().
 */
QueueHandle_t sequencer_get_event_queue(void);

/**
 * Thread-safe read of the current sequencer state.
 */
seq_state_t sequencer_get_state(void);

/**
 * Clear a latched fault and return to RX state.
 * No-op if not in SEQ_STATE_FAULT. Safe to call from any task.
 */
esp_err_t sequencer_clear_fault(void);

/**
 * Return the fault code of the last fault, or SEQ_FAULT_NONE.
 */
seq_fault_t sequencer_get_fault(void);

/**
 * Update the sequencer's config with new sequences and thresholds.
 * Only safe in SEQ_STATE_RX. Returns ESP_ERR_INVALID_STATE otherwise.
 */
esp_err_t sequencer_update_config(const app_config_t *cfg);

/**
 * Queue a fault event.  Builds the event struct and sends to the
 * sequencer queue.  Replaces duplicated logic in CLI and web handlers.
 * Returns ESP_FAIL if the queue is full.
 */
esp_err_t sequencer_inject_fault(seq_fault_t fault);

/* ---------------------------------------------------------
 * Enum ↔ string helpers — single source of truth for names
 * used by CLI, web API, and logging.
 * --------------------------------------------------------- */

/** Return display name for a sequencer state ("RX", "SEQ_TX", …). */
const char *seq_state_name(seq_state_t state);

/** Return display name for a fault code ("none", "HIGH_SWR", …). */
const char *seq_fault_name(seq_fault_t fault);

/**
 * Parse a fault injection keyword ("swr", "temp1", "temp2", "emergency")
 * into the corresponding seq_fault_t.  Returns true on match.
 */
bool seq_fault_parse(const char *str, seq_fault_t *out);

#ifdef __cplusplus
}
#endif
