#include <math.h>
#include <stdio.h>
#include <stdarg.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hw_config.h"
#include "sequencer.h"
#include "system_state.h"
#include "nextion_display.h"

static void send_command(const char *format, ...)
{
    char command[96];
    va_list args;
    va_start(args, format);
    int length = vsnprintf(command, sizeof(command) - 2, format, args);
    va_end(args);
    if (length > 0 && length < sizeof(command) - 2) {
        command[length++] = (char)0xff;
        command[length++] = (char)0xff;
        command[length++] = (char)0xff;
        uart_write_bytes(HW_NEXTION_UART_PORT, command, length);
    }
}

static void send_text(const char *field, const char *value)
{
    send_command("%s.txt=\"%s\"", field, value);
}

static void send_reading(const char *field, float value, const char *unit)
{
    char text[32] = "--";
    if (isfinite(value) && value >= 0.0f) {
        int length = snprintf(text, sizeof(text), "%.1f%s", value, unit);
        if (length < 0 || length >= sizeof(text)) {
            send_text(field, "--");
            return;
        }
    }
    send_text(field, text);
}

/* Coordinates refer to picture 0 on the K9KJ 480x320 dashboard. */
static void send_temperature(unsigned int x, float celsius)
{
    char reading[24];
    int length = isfinite(celsius) ?
        snprintf(reading, sizeof(reading), "%.1f C", celsius) : -1;
    send_command("xstr %u,212,100,32,0,65535,0,1,1,0,\"%s\"",
                 x, length > 0 && length < sizeof(reading) ? reading : "--");
}

static void send_relays(uint8_t relay_states)
{
    static const unsigned int lamp_y[] = {74, 101, 128, 155, 182, 210};
    for (unsigned int i = 0; i < sizeof(lamp_y) / sizeof(lamp_y[0]); ++i) {
        if (relay_states & (1u << i)) {
            send_command("cirs 451,%u,5,GREEN", lamp_y[i]);
        } else {
            send_command("picq 445,%u,13,13,0", lamp_y[i] - 6);
        }
    }
}

static void display_task(void *arg)
{
    (void)arg;
    TickType_t state_since = xTaskGetTickCount();
    TickType_t last_refresh = state_since - pdMS_TO_TICKS(500);
    int previous_state = -1;
    for (;;) {
        system_state_t state;
        system_state_get(&state);
        TickType_t now = xTaskGetTickCount();
        bool state_changed = previous_state != state.seq_state;
        bool refresh = now - last_refresh >= pdMS_TO_TICKS(500);

        if (state_changed) {
            state_since = now;
            previous_state = state.seq_state;
        }

        if (state_changed || refresh) {
            send_text("tState", seq_state_name((seq_state_t)state.seq_state));
            send_text("tFault", state.seq_fault == SEQ_FAULT_NONE ?
                      "OK" : seq_fault_name((seq_fault_t)state.seq_fault));
            if (state.seq_state == SEQ_STATE_FAULT) {
                send_command("picq 379,276,92,32,0");
            } else {
                double seconds = (double)(now - state_since) / configTICK_RATE_HZ;
                send_command("xstr 379,276,92,32,0,65535,0,1,1,0,\"%.1f s\"", seconds);
            }
            send_relays(state.relay_states);
        }

        if (refresh) {
            last_refresh = now;
            send_reading("tFwd", state.fwd_power_w, " W");
            send_reading("tRef", state.ref_power_w, " W");
            send_reading("tSwr", state.swr, ":1");
            send_temperature(49, state.temp1_c);
            send_temperature(201, state.temp2_c);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void nextion_display_start(void)
{
    const uart_config_t config = {
        .baud_rate = HW_NEXTION_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t error = uart_driver_install(HW_NEXTION_UART_PORT, 256, 0, 0, NULL, 0);
    if (error == ESP_OK) {
        error = uart_param_config(HW_NEXTION_UART_PORT, &config);
        if (error == ESP_OK) {
            error = uart_set_pin(HW_NEXTION_UART_PORT, HW_NEXTION_TX_GPIO,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE);
        }
        if (error == ESP_OK &&
            xTaskCreate(display_task, "nextion", 3072, NULL, 3, NULL) == pdPASS) {
            return;
        }
        uart_driver_delete(HW_NEXTION_UART_PORT);
    }
    ESP_LOGW("nextion", "Display task unavailable");
}
