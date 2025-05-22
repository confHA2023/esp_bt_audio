#include "driver/i2c.h"
#include "esp_log.h"
#include "es8388.h"

#define ES8388_I2C_PORT I2C_NUM_0
#define ES8388_ADDR 0x10

//static const char *TAG = "es8388";

static esp_err_t es8388_write_reg(uint8_t reg, uint8_t val) {
    uint8_t data[2] = {reg, val};
    return i2c_master_write_to_device(ES8388_I2C_PORT, ES8388_ADDR, data, 2, pdMS_TO_TICKS(100));
}

esp_err_t es8388_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = 18,
        .scl_io_num = 23,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    i2c_param_config(ES8388_I2C_PORT, &conf);
    i2c_driver_install(ES8388_I2C_PORT, conf.mode, 0, 0, 0);

    vTaskDelay(pdMS_TO_TICKS(100));

    // Reset codec
    es8388_write_reg(0x00, 0x80); vTaskDelay(pdMS_TO_TICKS(10));
    es8388_write_reg(0x00, 0x00);

    // Power up DAC and output mixer
    es8388_write_reg(0x02, 0xF3);
    es8388_write_reg(0x03, 0x09);
    es8388_write_reg(0x04, 0x00);
    es8388_write_reg(0x08, 0x00);

    // DAC volume
    es8388_write_reg(0x2E, 0x1C); // LOUT1
    es8388_write_reg(0x2F, 0x1C); // ROUT1

    // Output routing: enable DAC to HP
    es8388_write_reg(0x27, 0xB8);
    es8388_write_reg(0x2A, 0x00);

    return ESP_OK;
}

esp_err_t es8388_set_volume(uint8_t volume) {
    volume &= 0x3F;
    es8388_write_reg(0x2E, volume);
    es8388_write_reg(0x2F, volume);
    return ESP_OK;
}
