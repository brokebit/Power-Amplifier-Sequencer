#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "ads1115.h"
#include "hw_config.h"
#include "sequencer.h"
#include "system_state.h"

#include "monitor.h"

static const char *TAG = "monitor";

/* Minimum forward power (W) below which SWR is not checked. */
#define MIN_FWD_POWER_FOR_SWR_W  0.1f

/* Assumed supply voltage for thermistor divider (VCC → R_series → NTC → GND) */
#define THERMISTOR_VCC  3.3f

/* ---- module state ------------------------------------------------------- */

static app_config_t             s_cfg;
static i2c_master_bus_handle_t  s_bus;
static ads1115_handle_t         s_chip[2];  /* [0]=0x48 reserved, [1]=0x49 active */

/* ALERT/RDY events from ISRs. Item = chip index (0 or 1).
 * Only chip 1 has an ISR installed; chip 0 is reserved for future use. */
static QueueHandle_t            s_adc_queue;

/* ---- ISR handler (chip 1 only) ----------------------------------------- */

static void IRAM_ATTR alert1_isr(void *arg)
{
    BaseType_t woken = pdFALSE;
    uint8_t chip = 1;
    xQueueSendFromISR(s_adc_queue, &chip, &woken);
    portYIELD_FROM_ISR(woken);
}

/* ---- math helpers ------------------------------------------------------- */

/* Steinhart-Hart beta equation.
 * Assumes voltage divider: VCC → R_series → NTC → GND.
 * v_adc: voltage measured across NTC (volts).
 * Returns temperature in °C, or NAN on bad input. */
static float voltage_to_temp_c(float v_adc, const app_config_t *cfg)
{
    float denom = THERMISTOR_VCC - v_adc;
    if (denom <= 0.0f || v_adc <= 0.0f) return NAN;

    float r_ntc = cfg->thermistor_r_series_ohms * v_adc / denom;
    float t_kelvin = 1.0f / (1.0f / 298.15f +
                             logf(r_ntc / cfg->thermistor_r0_ohms) / cfg->thermistor_beta);
    return t_kelvin - 273.15f;
}

/* Calculate SWR from forward and reflected voltages. */
static float calc_swr(float vf, float vr)
{
    if (vf < 0.001f) return 1.0f;
    float gamma = vr / vf;
    if (gamma >= 1.0f) return 99.9f;
    return (1.0f + gamma) / (1.0f - gamma);
}

/* ---- fault injection ---------------------------------------------------- */

static void inject_fault(seq_fault_t fault)
{
    seq_event_t ev = {.type = SEQ_EVENT_FAULT, .data = (uint32_t)fault};
    xQueueSend(sequencer_get_event_queue(), &ev, 0);
}

/* ---- public API --------------------------------------------------------- */

esp_err_t monitor_init(const app_config_t *cfg)
{
    memcpy(&s_cfg, cfg, sizeof(s_cfg));

    s_adc_queue = xQueueCreate(8, sizeof(uint8_t));
    if (!s_adc_queue) return ESP_ERR_NO_MEM;

    /* I2C master bus */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port            = HW_I2C_PORT,
        .sda_io_num          = HW_I2C_SDA_GPIO,
        .scl_io_num          = HW_I2C_SCL_GPIO,
        .clk_source          = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt   = 7,
        .flags.enable_internal_pullup = false,
    };
    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* chip 0 (0x48) — reserved for future use */
    ret = ads1115_init(s_bus, HW_ADS1115_0_ADDR, ADS1115_PGA_4096, &s_chip[0]);
    if (ret != ESP_OK) return ret;

    /* chip 1 (0x49) — active: AIN0=fwd, AIN1=ref, AIN2=temp-R, AIN3=temp-L */
    ret = ads1115_init(s_bus, HW_ADS1115_1_ADDR, ADS1115_PGA_4096, &s_chip[1]);
    if (ret != ESP_OK) return ret;

    /* ALERT/RDY GPIO — chip 1 only has an ISR; chip 0 is input+pullup, no handler */
    gpio_install_isr_service(0);   /* no-op if already installed */

    gpio_config_t io = {
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,       /* no ISR for chip 0 */
    };
    io.pin_bit_mask = 1ULL << HW_ADS1115_0_ALRT_GPIO;
    gpio_config(&io);

    io.intr_type    = GPIO_INTR_NEGEDGE;         /* ISR for chip 1 */
    io.pin_bit_mask = 1ULL << HW_ADS1115_1_ALRT_GPIO;
    gpio_config(&io);
    gpio_isr_handler_add(HW_ADS1115_1_ALRT_GPIO, alert1_isr, NULL);

    ESP_LOGI(TAG, "initialised");
    return ESP_OK;
}

/**
 * Read one channel from chip 1 via single-shot conversion.
 * Triggers conversion, waits for ALERT/RDY, reads result.
 * Returns voltage (clamped >= 0) or -1.0f on failure.
 */
static float read_channel(ads1115_channel_t ch)
{
    /* Drain any stale ALERT event */
    uint8_t dummy;
    while (xQueueReceive(s_adc_queue, &dummy, 0) == pdTRUE) {}

    if (ads1115_start_single_shot(s_chip[1], ch) != ESP_OK) {
        ESP_LOGW(TAG, "start failed ch %d", ch);
        return -1.0f;
    }

    /* Wait for ALERT/RDY — 250ms timeout (conversion takes ~125ms at 8 SPS) */
    if (xQueueReceive(s_adc_queue, &dummy, pdMS_TO_TICKS(250)) != pdTRUE) {
        ESP_LOGW(TAG, "timeout ch %d", ch);
        return -1.0f;
    }

    int16_t raw;
    if (ads1115_read_raw(s_chip[1], &raw) != ESP_OK) {
        ESP_LOGW(TAG, "read failed ch %d", ch);
        return -1.0f;
    }

    return fmaxf(ads1115_raw_to_voltage(s_chip[1], raw), 0.0f);
}

void monitor_task(void *arg)
{
    /*
     * Cycle all four channels in single-shot mode:
     *   AIN0 = forward power   AIN1 = reflected power
     *   AIN2 = temp right PA   AIN3 = temp left PA
     */
    float last_fwd_w = 0.0f, last_ref_w = 0.0f, last_swr = 1.0f;
    float last_temp1 = 0.0f, last_temp2 = 0.0f;

    for (;;) {
        /* ---- Forward + Reflected power ---- */
        float fwd_v = read_channel(ADS1115_CHANNEL_0);
        float ref_v = read_channel(ADS1115_CHANNEL_1);

        if (fwd_v >= 0.0f && ref_v >= 0.0f) {
            last_fwd_w = s_cfg.fwd_power_cal_factor * fwd_v * fwd_v;
            last_ref_w = s_cfg.ref_power_cal_factor * ref_v * ref_v;
            last_swr   = calc_swr(fwd_v, ref_v);

            system_state_set_sensors(last_fwd_w, last_ref_w, last_swr,
                                     last_temp1, last_temp2);

            if (last_fwd_w >= MIN_FWD_POWER_FOR_SWR_W && last_swr > s_cfg.swr_fault_threshold) {
                ESP_LOGW(TAG, "high SWR %.1f (threshold %.1f)",
                         last_swr, s_cfg.swr_fault_threshold);
                inject_fault(SEQ_FAULT_HIGH_SWR);
            }
        }

        /* ---- Temperature — right side PA ---- */
        float t1_v = read_channel(ADS1115_CHANNEL_2);
        if (t1_v > 0.0f) {
            float temp_c = voltage_to_temp_c(t1_v, &s_cfg);
            last_temp1 = temp_c;
            system_state_set_sensors(last_fwd_w, last_ref_w, last_swr,
                                     last_temp1, last_temp2);
            if (!isnan(temp_c) && temp_c > s_cfg.temp_fault_threshold_c) {
                ESP_LOGW(TAG, "over-temp R: %.1f°C (threshold %.1f)",
                         temp_c, s_cfg.temp_fault_threshold_c);
                inject_fault(SEQ_FAULT_OVER_TEMP1);
            }
        }

        /* ---- Temperature — left side PA ---- */
        float t2_v = read_channel(ADS1115_CHANNEL_3);
        if (t2_v > 0.0f) {
            float temp_c = voltage_to_temp_c(t2_v, &s_cfg);
            last_temp2 = temp_c;
            system_state_set_sensors(last_fwd_w, last_ref_w, last_swr,
                                     last_temp1, last_temp2);
            if (!isnan(temp_c) && temp_c > s_cfg.temp_fault_threshold_c) {
                ESP_LOGW(TAG, "over-temp L: %.1f°C (threshold %.1f)",
                         temp_c, s_cfg.temp_fault_threshold_c);
                inject_fault(SEQ_FAULT_OVER_TEMP2);
            }
        }
    }
}
