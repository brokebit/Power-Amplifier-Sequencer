#pragma once

#include "hal/i2c_types.h"
#include "hal/uart_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------
 * PTT Input
 * Active low, internal pull-up enabled
 * --------------------------------------------------------- */
#define HW_PTT_GPIO 13

/* ---------------------------------------------------------
 * Relay outputs (active high)
 * Relay IDs are 1-indexed to match schematic labels
 * --------------------------------------------------------- */
#define HW_RELAY_COUNT 6
#define HW_RELAY1_GPIO 39 /* RX/TX Path Select */
#define HW_RELAY2_GPIO 40 /* PA On/Off */
#define HW_RELAY3_GPIO 41 /* LNA Isolate */
#define HW_RELAY4_GPIO 42 /* Spare */
#define HW_RELAY5_GPIO 11 /* Spare */
#define HW_RELAY6_GPIO 12 /* Spare */

/* Relay GPIO array — index 0 = Relay 1 */
#define HW_RELAY_GPIOS { HW_RELAY1_GPIO, HW_RELAY2_GPIO, HW_RELAY3_GPIO, \
                         HW_RELAY4_GPIO, HW_RELAY5_GPIO, HW_RELAY6_GPIO }

/* ---------------------------------------------------------
 * Buttons (active low, internal pull-up enabled)
 * --------------------------------------------------------- */
#define HW_BUTTON_COUNT 6
#define HW_BTN1_GPIO 4  /* Emergency PA Off */
#define HW_BTN2_GPIO 5  /* Spare */
#define HW_BTN3_GPIO 6  /* Spare */
#define HW_BTN4_GPIO 7  /* Spare */
#define HW_BTN5_GPIO 48 /* Spare */
#define HW_BTN6_GPIO 47 /* Spare */

#define HW_BUTTON_GPIOS { HW_BTN1_GPIO, HW_BTN2_GPIO, HW_BTN3_GPIO, \
                          HW_BTN4_GPIO, HW_BTN5_GPIO, HW_BTN6_GPIO }

/* ---------------------------------------------------------
 * I2C — ADS1115 ADCs
 * --------------------------------------------------------- */
#define HW_I2C_PORT I2C_NUM_0
#define HW_I2C_SDA_GPIO 1
#define HW_I2C_SCL_GPIO 2
#define HW_I2C_FREQ_HZ 400000

#define HW_ADS1115_0_ADDR 0x48 /* Reserved for future use */
#define HW_ADS1115_1_ADDR 0x49 /* AIN0=fwd, AIN1=ref, AIN2=temp-R, AIN3=temp-L */

#define HW_ADS1115_0_ALRT_GPIO 16
#define HW_ADS1115_1_ALRT_GPIO 15

/* ---------------------------------------------------------
 * UART — Nextion display
 * --------------------------------------------------------- */
#define HW_NEXTION_UART_PORT UART_NUM_1
#define HW_NEXTION_TX_GPIO 17
#define HW_NEXTION_RX_GPIO 18
#define HW_NEXTION_BAUD_RATE 9600

#ifdef __cplusplus
}
#endif
