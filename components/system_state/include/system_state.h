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
    float fwd_power_dbm;
    float ref_power_dbm;
    float swr;
    float temp1_c;
    float temp2_c;

    /* Chip 1 ADC — raw voltages at ADC input */
    float adc_1_ch0;
    float adc_1_ch1;
    float adc_1_ch2;
    float adc_1_ch3;

    /* Chip 0 ADC — corrected (pre-divider) voltages */
    float adc_0_ch0;
    float adc_0_ch1;
    float adc_0_ch2;
    float adc_0_ch3;

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
void system_state_set_sensors(float fwd_w, float ref_w,
                              float fwd_dbm, float ref_dbm,
                              float swr, float temp1_c, float temp2_c);
void system_state_set_adc1_raw(float ch0, float ch1, float ch2, float ch3);
void system_state_set_adc0(float ch0, float ch1, float ch2, float ch3);
void system_state_set_wifi(bool connected, uint32_t ip_addr, int8_t rssi);

/* ---------------------------------------------------------
 * Reader — copies a consistent snapshot into *out
 * --------------------------------------------------------- */
void system_state_get(system_state_t *out);

#ifdef __cplusplus
}
#endif
