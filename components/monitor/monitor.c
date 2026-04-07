#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "ads1115.h"
#include "hw_config.h"
#include "sequencer.h"
#include "system_state.h"

#include "monitor.h"

static const char *TAG = "monitor";

/* Minimum forward power (W) below which SWR is not checked. */
#define MIN_FWD_POWER_FOR_SWR_W  0.5f

/* Assumed supply voltage for thermistor divider (VCC → R_series → NTC → GND) */
#define THERMISTOR_VCC  3.3f

/* ---- module state ------------------------------------------------------- */

static app_config_t s_cfg;
static portMUX_TYPE s_cfg_mux = portMUX_INITIALIZER_UNLOCKED;
static i2c_master_bus_handle_t s_bus;
static ads1115_handle_t s_chip[2];  /* [0]=0x48, [1]=0x49 */

/* ALERT/RDY events from ISRs — one queue per chip. */
static QueueHandle_t s_adc_queue[2];

/* Mutex protecting ADC access — shared between monitor_task and CLI */
static SemaphoreHandle_t s_adc_mutex;

/* ---- ISR handlers ------------------------------------------------------ */

static void IRAM_ATTR alert0_isr(void *arg)
{
    BaseType_t woken = pdFALSE;
    uint8_t chip = 0;
    xQueueSendFromISR(s_adc_queue[0], &chip, &woken);
    portYIELD_FROM_ISR(woken);
}

static void IRAM_ATTR alert1_isr(void *arg)
{
    BaseType_t woken = pdFALSE;
    uint8_t chip = 1;
    xQueueSendFromISR(s_adc_queue[1], &chip, &woken);
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
    if (denom <= 0.0f || v_adc <= 0.0f) {
        return NAN;
    }

    float r_ntc = cfg->thermistor_r_series_ohms * v_adc / denom;
    float t_kelvin = 1.0f / (1.0f / 298.15f +
                             logf(r_ntc / cfg->thermistor_r0_ohms) / cfg->thermistor_beta);
    return t_kelvin - 273.15f;
}

/* Convert ADC voltage to dBm via the log-linear detector model.
 * Returns -999.0f (no-signal sentinel) when v_adc is negligible. */
static float adc_voltage_to_dbm(float v_adc, float slope, float intercept,
                                float coupling_db, float atten_db,
                                float r_top, float r_bottom)
{
    if (v_adc < 0.001f) return -999.0f;

    float divider_ratio = r_bottom / (r_top + r_bottom);
    float v_det_mv = (v_adc / divider_ratio) * 1000.0f;
    float dbm_det = (v_det_mv / slope) + intercept;
    return dbm_det + atten_db + fabsf(coupling_db);
}

/* Convert dBm to watts. */
static float dbm_to_watts(float dbm)
{
    if (dbm <= -999.0f) return 0.0f;
    return powf(10.0f, (dbm - 30.0f) / 10.0f);
}

/* Calculate SWR from forward and reflected power in watts.
   SWR = (1 + √(Pr/Pf)) / (1 − √(Pr/Pf))                */
static float calc_swr(float pf, float pr)
{
    if (pf < MIN_FWD_POWER_FOR_SWR_W) {
        return 1.0f;
    }
    float gamma = sqrtf(pr / pf);
    if (gamma >= 1.0f) {
        return 99.9f;
    }
    return (1.0f + gamma) / (1.0f - gamma);
}

/* ---- forward declarations ----------------------------------------------- */

static float read_channel(int chip, ads1115_channel_t ch);

/* ---- fault injection ---------------------------------------------------- */

static void inject_fault(seq_fault_t fault)
{
    seq_event_t ev = {.type = SEQ_EVENT_FAULT, .data = (uint32_t)fault};
    xQueueSend(sequencer_get_event_queue(), &ev, 0);
}

/* ---- public API --------------------------------------------------------- */

esp_err_t monitor_read_channel(int chip, ads1115_channel_t ch, float *out_voltage)
{
    if (!s_adc_mutex) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_adc_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    float v = read_channel(chip, ch);

    xSemaphoreGive(s_adc_mutex);

    if (v < 0.0f) {
        return ESP_FAIL;
    }
    *out_voltage = v;
    return ESP_OK;
}

esp_err_t monitor_update_config(const app_config_t *cfg)
{
    portENTER_CRITICAL(&s_cfg_mux);
    memcpy(&s_cfg, cfg, sizeof(s_cfg));
    portEXIT_CRITICAL(&s_cfg_mux);
    ESP_LOGI(TAG, "Config updated");
    return ESP_OK;
}

esp_err_t monitor_init(void)
{
    config_snapshot(&s_cfg);

    s_adc_queue[0] = xQueueCreate(4, sizeof(uint8_t));
    if (!s_adc_queue[0]) return ESP_ERR_NO_MEM;

    s_adc_queue[1] = xQueueCreate(4, sizeof(uint8_t));
    if (!s_adc_queue[1]) return ESP_ERR_NO_MEM;

    s_adc_mutex = xSemaphoreCreateMutex();
    if (!s_adc_mutex) {
        return ESP_ERR_NO_MEM;
    }

    /* I2C master bus */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = HW_I2C_PORT,
        .sda_io_num = HW_I2C_SDA_GPIO,
        .scl_io_num = HW_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,
    };
    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* chip 0 (0x48) — general-purpose ADC channels */
    ret = ads1115_init(s_bus, HW_ADS1115_0_ADDR, ADS1115_PGA_4096, &s_chip[0]);
    if (ret != ESP_OK) {
        return ret;
    }

    /* chip 1 (0x49) — active: AIN0=fwd, AIN1=ref, AIN2=temp-R, AIN3=temp-L */
    ret = ads1115_init(s_bus, HW_ADS1115_1_ADDR, ADS1115_PGA_4096, &s_chip[1]);
    if (ret != ESP_OK) {
        return ret;
    }

    /* ALERT/RDY GPIO — falling-edge ISR for both chips */
    gpio_install_isr_service(0);   /* no-op if already installed */

    gpio_config_t io = {
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    io.pin_bit_mask = 1ULL << HW_ADS1115_0_ALRT_GPIO;
    gpio_config(&io);
    gpio_isr_handler_add(HW_ADS1115_0_ALRT_GPIO, alert0_isr, NULL);

    io.pin_bit_mask = 1ULL << HW_ADS1115_1_ALRT_GPIO;
    gpio_config(&io);
    gpio_isr_handler_add(HW_ADS1115_1_ALRT_GPIO, alert1_isr, NULL);

    config_register_apply_cb(monitor_update_config);

    ESP_LOGI(TAG, "initialised");
    return ESP_OK;
}

/**
 * Read one channel via single-shot conversion.
 * Triggers conversion, waits for ALERT/RDY, reads result.
 * Returns voltage (clamped >= 0) or -1.0f on failure.
 */
static float read_channel(int chip, ads1115_channel_t ch)
{
    /* Drain any stale ALERT event */
    uint8_t dummy;
    while (xQueueReceive(s_adc_queue[chip], &dummy, 0) == pdTRUE) {}

    if (ads1115_start_single_shot(s_chip[chip], ch) != ESP_OK) {
        ESP_LOGW(TAG, "start failed chip %d ch %d", chip, ch);
        return -1.0f;
    }

    /* Wait for ALERT/RDY — 250ms timeout (conversion takes ~125ms at 8 SPS) */
    if (xQueueReceive(s_adc_queue[chip], &dummy, pdMS_TO_TICKS(250)) != pdTRUE) {
        ESP_LOGW(TAG, "timeout chip %d ch %d", chip, ch);
        return -1.0f;
    }

    int16_t raw;
    if (ads1115_read_raw(s_chip[chip], &raw) != ESP_OK) {
        ESP_LOGW(TAG, "read failed chip %d ch %d", chip, ch);
        return -1.0f;
    }

    return fmaxf(ads1115_raw_to_voltage(s_chip[chip], raw), 0.0f);
}

void monitor_task(void *arg)
{
    /*
     * Cycle all four channels in single-shot mode:
     *   AIN0 = forward power   AIN1 = reflected power
     *   AIN2 = temp1 PA   AIN3 = temp2 PA
     */
    float last_fwd_w = 0.0f, last_ref_w = 0.0f, last_swr = 1.0f;
    float last_fwd_dbm = -999.0f, last_ref_dbm = -999.0f;
    float last_temp1 = 0.0f, last_temp2 = 0.0f;

    for (;;) {
        /* Snapshot config under spinlock so the entire ADC cycle sees
         * a consistent set of thresholds and calibration values. */
        app_config_t cfg;
        portENTER_CRITICAL(&s_cfg_mux);
        memcpy(&cfg, &s_cfg, sizeof(cfg));
        portEXIT_CRITICAL(&s_cfg_mux);

        xSemaphoreTake(s_adc_mutex, portMAX_DELAY);

        /* ---- Forward + Reflected power ---- */
        float fwd_v = read_channel(1, ADS1115_CHANNEL_0);
        float ref_v = read_channel(1, ADS1115_CHANNEL_1);

        if (fwd_v >= 0.0f && ref_v >= 0.0f) {
            last_fwd_dbm = adc_voltage_to_dbm(fwd_v,
                cfg.fwd_slope_mv_per_db, cfg.fwd_intercept_dbm,
                cfg.fwd_coupling_db, cfg.fwd_attenuator_db,
                cfg.adc_1a_r_top_ohms, cfg.adc_1a_r_bottom_ohms);
            last_ref_dbm = adc_voltage_to_dbm(ref_v,
                cfg.ref_slope_mv_per_db, cfg.ref_intercept_dbm,
                cfg.ref_coupling_db, cfg.ref_attenuator_db,
                cfg.adc_1b_r_top_ohms, cfg.adc_1b_r_bottom_ohms);
            last_fwd_w = dbm_to_watts(last_fwd_dbm);
            last_ref_w = dbm_to_watts(last_ref_dbm);
            last_swr   = calc_swr(last_fwd_w, last_ref_w);

            system_state_set_sensors(last_fwd_w, last_ref_w,
                                     last_fwd_dbm, last_ref_dbm,
                                     last_swr, last_temp1, last_temp2);

            if (last_fwd_w >= MIN_FWD_POWER_FOR_SWR_W && last_swr > cfg.swr_fault_threshold) {
                ESP_LOGW(TAG, "high SWR %.1f (threshold %.1f)",
                         last_swr, cfg.swr_fault_threshold);
                inject_fault(SEQ_FAULT_HIGH_SWR);
            }
        }

        /* ---- Temperature 1 ---- */
        float t1_v = read_channel(1, ADS1115_CHANNEL_2);
        if (t1_v > 0.0f) {
            float temp_c = voltage_to_temp_c(t1_v, &cfg);
            last_temp1 = temp_c;
            system_state_set_sensors(last_fwd_w, last_ref_w,
                                     last_fwd_dbm, last_ref_dbm,
                                     last_swr, last_temp1, last_temp2);
            if (!isnan(temp_c) && temp_c > cfg.temp1_fault_threshold_c) {
                ESP_LOGW(TAG, "over-temp 1: %.1f°C (threshold %.1f)",
                         temp_c, cfg.temp1_fault_threshold_c);
                inject_fault(SEQ_FAULT_OVER_TEMP1);
            }
        }

        /* ---- Temperature 2 ---- */
        float t2_v = read_channel(1, ADS1115_CHANNEL_3);
        if (t2_v > 0.0f) {
            float temp_c = voltage_to_temp_c(t2_v, &cfg);
            last_temp2 = temp_c;
            system_state_set_sensors(last_fwd_w, last_ref_w,
                                     last_fwd_dbm, last_ref_dbm,
                                     last_swr, last_temp1, last_temp2);
            if (!isnan(temp_c) && temp_c > cfg.temp2_fault_threshold_c) {
                ESP_LOGW(TAG, "over-temp 2: %.1f°C (threshold %.1f)",
                         temp_c, cfg.temp2_fault_threshold_c);
                inject_fault(SEQ_FAULT_OVER_TEMP2);
            }
        }

        /* ---- Chip 0 — four general-purpose channels ---- */
        {
            static const struct {
                ads1115_channel_t ch;
                size_t top_off;
                size_t bot_off;
            } ch0_map[] = {
                { ADS1115_CHANNEL_0, offsetof(app_config_t, adc_0a_r_top_ohms),
                                     offsetof(app_config_t, adc_0a_r_bottom_ohms) },
                { ADS1115_CHANNEL_1, offsetof(app_config_t, adc_0b_r_top_ohms),
                                     offsetof(app_config_t, adc_0b_r_bottom_ohms) },
                { ADS1115_CHANNEL_2, offsetof(app_config_t, adc_0c_r_top_ohms),
                                     offsetof(app_config_t, adc_0c_r_bottom_ohms) },
                { ADS1115_CHANNEL_3, offsetof(app_config_t, adc_0d_r_top_ohms),
                                     offsetof(app_config_t, adc_0d_r_bottom_ohms) },
            };
            float corrected[4] = {0};
            for (int i = 0; i < 4; i++) {
                float v = read_channel(0, ch0_map[i].ch);
                if (v >= 0.0f) {
                    float r_top = *(const float *)((const char *)&cfg + ch0_map[i].top_off);
                    float r_bot = *(const float *)((const char *)&cfg + ch0_map[i].bot_off);
                    float ratio = r_bot / (r_top + r_bot);
                    corrected[i] = (ratio > 0.0f) ? v / ratio : v;
                }
            }
            system_state_set_adc0(corrected[0], corrected[1],
                                  corrected[2], corrected[3]);
        }

        xSemaphoreGive(s_adc_mutex);

        /* Yield to let lower-priority tasks (e.g. CLI adc commands) acquire
         * the mutex. Without this delay the monitor task immediately re-takes
         * it and starves the REPL. */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
