#include <stdlib.h>

#include "esp_log.h"

#include "ads1115.h"

static const char *TAG = "ads1115";

/* ADS1115 register addresses */
#define REG_CONVERSION  0x00
#define REG_CONFIG      0x01
#define REG_LO_THRESH   0x02
#define REG_HI_THRESH   0x03

/* Config register field masks / values */
#define CFG_OS_START    (1u << 15)  /* Write: start single-shot conversion */
#define CFG_MODE_SINGLE (1u <<  8)  /* Single-shot / power-down mode       */
#define CFG_DR_8SPS     (0u <<  5)  /* 8 samples/sec = 125 ms/sample      */
#define CFG_COMP_QUE_1  (0u <<  0)  /* Assert ALERT after 1 conversion     */

/* MUX: single-ended AINx vs GND starts at 0x4 */
#define CFG_MUX_AIN(ch) ((uint16_t)(0x4u + (ch)) << 12)
#define CFG_PGA(pga)    ((uint16_t)(pga) << 9)

/* Full-scale voltage per PGA setting (index = ads1115_pga_t) */
static const float s_fsr[] = {6.144f, 4.096f, 2.048f, 1.024f, 0.512f, 0.256f};

struct ads1115_dev {
    i2c_master_dev_handle_t i2c_dev;
    ads1115_pga_t           pga;
};

/* ---- helpers ------------------------------------------------------------ */

static esp_err_t write_reg(ads1115_handle_t h, uint8_t reg, uint16_t val)
{
    uint8_t buf[3] = {reg, (uint8_t)(val >> 8), (uint8_t)(val & 0xFF)};
    return i2c_master_transmit(h->i2c_dev, buf, sizeof(buf), 100);
}

static esp_err_t read_reg(ads1115_handle_t h, uint8_t reg, uint16_t *out)
{
    uint8_t data[2];
    esp_err_t ret = i2c_master_transmit_receive(h->i2c_dev, &reg, 1,
                                                data, sizeof(data), 100);
    if (ret == ESP_OK) {
        *out = ((uint16_t)data[0] << 8) | data[1];
    }
    return ret;
}

/* ---- public API --------------------------------------------------------- */

esp_err_t ads1115_init(i2c_master_bus_handle_t bus, uint8_t addr,
                       ads1115_pga_t pga, ads1115_handle_t *out_handle)
{
    struct ads1115_dev *dev = calloc(1, sizeof(*dev));
    if (!dev) {
        return ESP_ERR_NO_MEM;
    }

    dev->pga = pga;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = 400000,
    };
    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, &dev->i2c_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bus_add_device(0x%02x) failed: %s", addr, esp_err_to_name(ret));
        free(dev);
        return ret;
    }

    /* Configure ALERT/RDY as conversion-ready:
     * Hi_thresh MSB = 1, Lo_thresh MSB = 0 (per ADS1115 datasheet §9.3.8) */
    ret = write_reg(dev, REG_HI_THRESH, 0x8000);
    if (ret != ESP_OK) goto fail;
    ret = write_reg(dev, REG_LO_THRESH, 0x0000);
    if (ret != ESP_OK) goto fail;

    ESP_LOGI(TAG, "initialised at 0x%02x", addr);
    *out_handle = dev;
    return ESP_OK;

fail:
    ESP_LOGE(TAG, "threshold write failed: %s", esp_err_to_name(ret));
    i2c_master_bus_rm_device(dev->i2c_dev);
    free(dev);
    return ret;
}

esp_err_t ads1115_start_single_shot(ads1115_handle_t handle,
                                    ads1115_channel_t channel)
{
    uint16_t cfg = CFG_OS_START
                 | CFG_MUX_AIN(channel)
                 | CFG_PGA(handle->pga)
                 | CFG_MODE_SINGLE
                 | CFG_DR_8SPS
                 | CFG_COMP_QUE_1;
    return write_reg(handle, REG_CONFIG, cfg);
}

esp_err_t ads1115_read_raw(ads1115_handle_t handle, int16_t *out_raw)
{
    uint16_t raw;
    esp_err_t ret = read_reg(handle, REG_CONVERSION, &raw);
    if (ret == ESP_OK) {
        *out_raw = (int16_t)raw;
    }
    return ret;
}

float ads1115_raw_to_voltage(ads1115_handle_t handle, int16_t raw)
{
    /* 16-bit signed, positive full scale = 32767 counts */
    return (float)raw * s_fsr[handle->pga] / 32767.0f;
}

void ads1115_deinit(ads1115_handle_t handle)
{
    if (!handle) {
        return;
    }
    i2c_master_bus_rm_device(handle->i2c_dev);
    free(handle);
}
