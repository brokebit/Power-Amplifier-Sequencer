#include <string.h>

#include "freertos/FreeRTOS.h"

#include "system_state.h"

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static system_state_t s_state = {
    .swr = 1.0f,
};

/* ---- writers ------------------------------------------------------------ */

void system_state_set_relay(uint8_t relay_id, bool on)
{
    if (relay_id < 1 || relay_id > HW_RELAY_COUNT) {
        return;
    }
    portENTER_CRITICAL(&s_mux);
    if (on) {
        s_state.relay_states |=  (1u << (relay_id - 1));
    } else {
        s_state.relay_states &= ~(1u << (relay_id - 1));
    }
    portEXIT_CRITICAL(&s_mux);
}

void system_state_set_relays_all_off(void)
{
    portENTER_CRITICAL(&s_mux);
    s_state.relay_states = 0;
    portEXIT_CRITICAL(&s_mux);
}

void system_state_set_ptt(bool active)
{
    portENTER_CRITICAL(&s_mux);
    s_state.ptt_active = active;
    portEXIT_CRITICAL(&s_mux);
}

void system_state_set_sequencer(uint8_t state, uint8_t fault)
{
    portENTER_CRITICAL(&s_mux);
    s_state.seq_state = state;
    s_state.seq_fault = fault;
    portEXIT_CRITICAL(&s_mux);
}

void system_state_set_sensors(float fwd_w, float ref_w,
                              float fwd_dbm, float ref_dbm,
                              float swr, float temp1_c, float temp2_c)
{
    portENTER_CRITICAL(&s_mux);
    s_state.fwd_power_w = fwd_w;
    s_state.ref_power_w = ref_w;
    s_state.fwd_power_dbm = fwd_dbm;
    s_state.ref_power_dbm = ref_dbm;
    s_state.swr = swr;
    s_state.temp1_c = temp1_c;
    s_state.temp2_c = temp2_c;
    portEXIT_CRITICAL(&s_mux);
}

void system_state_set_wifi(bool connected, uint32_t ip_addr, int8_t rssi)
{
    portENTER_CRITICAL(&s_mux);
    s_state.wifi_connected = connected;
    s_state.wifi_ip_addr = ip_addr;
    s_state.wifi_rssi = rssi;
    portEXIT_CRITICAL(&s_mux);
}

/* ---- reader ------------------------------------------------------------- */

void system_state_get(system_state_t *out)
{
    portENTER_CRITICAL(&s_mux);
    memcpy(out, &s_state, sizeof(s_state));
    portEXIT_CRITICAL(&s_mux);
}
