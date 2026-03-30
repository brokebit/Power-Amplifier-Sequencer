#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hw_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Relay hardware state — bitmask, bit 0 = relay 1 */
    uint8_t relay_states;

    /* PTT input */
    bool ptt_active;

    /* Sequencer — cast to seq_state_t / seq_fault_t */
    uint8_t seq_state;
    uint8_t seq_fault;

    /* Sensor readings */
    float fwd_power_w;
    float ref_power_w;
    float swr;
    float temp1_c;
    float temp2_c;

    /* WiFi status */
    bool wifi_connected;
    uint32_t wifi_ip_addr; /* network byte order */
    int8_t wifi_rssi;
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
void system_state_set_wifi(bool connected, uint32_t ip_addr, int8_t rssi);

/* ---------------------------------------------------------
 * Reader — copies a consistent snapshot into *out
 * --------------------------------------------------------- */
void system_state_get(system_state_t *out);

#ifdef __cplusplus
}
#endif
