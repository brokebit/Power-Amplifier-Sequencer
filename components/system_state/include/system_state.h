#pragma once

#include "hw_config.h"
#include <stdbool.h>
#include <stdint.h>

/* =========================================================
 * system_state.h — Observable system state snapshot
 *
 * A single struct aggregating live state from all subsystems.
 * Producers (sequencer, monitor, PTT ISR) publish via setter
 * functions.  Consumers (display, console logger) read an
 * atomic snapshot with system_state_get().
 *
 * All access is protected by a spinlock — hold time is a
 * single memcpy, so callers never block.
 *
 * seq_state and seq_fault are stored as uint8_t to avoid a
 * header dependency on sequencer.h.  Cast to seq_state_t /
 * seq_fault_t when reading.
 * ========================================================= */

typedef struct {
    /* Relay hardware state — bitmask, bit 0 = relay 1 */
    uint8_t     relay_states;

    /* PTT input */
    bool        ptt_active;

    /* Sequencer — cast to seq_state_t / seq_fault_t */
    uint8_t     seq_state;
    uint8_t     seq_fault;

    /* Sensor readings */
    float       fwd_power_w;
    float       ref_power_w;
    float       swr;
    float       temp1_c;
    float       temp2_c;
} system_state_t;

/* ---------------------------------------------------------
 * Writers — called by the owning subsystem
 * --------------------------------------------------------- */
void system_state_set_relay(uint8_t relay_id, bool on);
void system_state_set_relays_all_off(void);
void system_state_set_ptt(bool active);
void system_state_set_sequencer(uint8_t state, uint8_t fault);
void system_state_set_sensors(float fwd_w, float ref_w, float swr,
                              float temp1_c, float temp2_c);

/* ---------------------------------------------------------
 * Reader — copies a consistent snapshot into *out
 * --------------------------------------------------------- */
void system_state_get(system_state_t *out);
