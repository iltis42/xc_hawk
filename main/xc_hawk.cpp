/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_flash.h"
#include "driver/gpio.h"



#include "driver/i2c.h"
#include "esp_log.h"

#define I2C_MASTER_SCL_IO 10
#define I2C_MASTER_SDA_IO 9
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 100000
#define I2C_MASTER_TIMEOUT_MS 1000
#define QMC5883_ADDR          0x2C

static const char *TAG = "i2c_utils";

extern "C" {
#include "driver/i2c.h"
#include "esp_log.h"
}


#define ICM20602_ADDR     0x68  // or 0x69 depending on AD0 pin
#define ICM20602_WHO_AM_I 0x75


esp_err_t icm20602_check_whoami()
{
    uint8_t who_am_i = 0;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ICM20602_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, ICM20602_WHO_AM_I, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ICM20602_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, &who_am_i, I2C_MASTER_NACK);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);

    if (ret == ESP_OK) {
        ESP_LOGI("ICM20602", "WHO_AM_I = 0x%02X", who_am_i);
        if (who_am_i == 0x12) {
            ESP_LOGI("ICM20602", "✅ Device verified: ICM-20602 detected.");
        } else {
            ESP_LOGW("ICM20602", "⚠️ Unexpected WHO_AM_I value.");
        }
    } else {
        ESP_LOGE("ICM20602", "❌ Failed to read WHO_AM_I: %s", esp_err_to_name(ret));
    }

    return ret;
}

// Schreibe 1 Byte in ein Register
esp_err_t qmc5883_write_register(uint8_t reg, uint8_t val) {
    uint8_t data[2] = {reg, val};
    return i2c_master_write_to_device(I2C_MASTER_NUM, QMC5883_ADDR, data, sizeof(data), I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

// Lese mehrere Bytes ab Start-Register
esp_err_t qmc5883_read_registers(uint8_t start_reg, uint8_t *data, size_t len) {
    return i2c_master_write_read_device(I2C_MASTER_NUM, QMC5883_ADDR, &start_reg, 1, data, len, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

bool qmc5883_check_presence() {
    uint8_t dummy;
    esp_err_t ret = qmc5883_read_registers(0x0D, &dummy, 1);  // Try reading any valid register
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "QMC5883 gefunden (Antwort auf I2C).");
        return true;
    } else {
        ESP_LOGE(TAG, "QMC5883 NICHT gefunden (keine Antwort auf I2C).");
        return false;
    }
}

void qmc5883_init() {
    // Continuous mode, 200Hz, 2G, 16-bit
    qmc5883_write_register(0x0B, 0b00011101);
    // Set/Reset Period
    qmc5883_write_register(0x0A, 0x01);
}

void qmc5883_read_xyz(int16_t *x, int16_t *y, int16_t *z) {
    uint8_t data[6];
    if (qmc5883_read_registers(0x00, data, 6) == ESP_OK) {
        *x = (int16_t)(data[1] << 8 | data[0]);
        *y = (int16_t)(data[3] << 8 | data[2]);
        *z = (int16_t)(data[5] << 8 | data[4]);
    } else {
        ESP_LOGE(TAG, "I2C read error");
    }
}

esp_err_t i2c_master_init(void) {
    i2c_config_t conf;
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_MASTER_SDA_IO;
    conf.scl_io_num = I2C_MASTER_SCL_IO;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = I2C_MASTER_FREQ_HZ;

    esp_err_t err;
    err = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (err != ESP_OK) return err;

    return i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

void i2c_scan_devices_all(void) {
    ESP_LOGI(TAG, "Scanning I2C bus...");

    for (uint8_t addr = 0; addr <= 127; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
        i2c_cmd_link_delete(cmd);

        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Found I2C device at address 0x%02X", addr);
        }else{
        }
    }

    ESP_LOGI(TAG, "I2C scan complete.");
}

void test_psram_raw_access() {
    ESP_LOGI("psram_test", "Testing raw PSRAM access...");

    // Testgröße: 1 KB
    const size_t test_size = 1024;
    
    // PSRAM allokieren mit SPIRAM-spezifischen Heap-Fähigkeiten
    uint8_t *psram_buf = (uint8_t *)heap_caps_malloc(test_size, MALLOC_CAP_SPIRAM);
    if (!psram_buf) {
        ESP_LOGE("psram_test", "Allocation failed!");
        return;
    }

    // Muster schreiben
    for (int i = 0; i < test_size; i++) {
        psram_buf[i] = (uint8_t)(i & 0xFF);
    }

    // Verifizieren
    for (int i = 0; i < test_size; i++) {
        if (psram_buf[i] != (uint8_t)(i & 0xFF)) {
            ESP_LOGE("psram_test", "Data mismatch at %d: got %02x, expected %02x", i, psram_buf[i], (uint8_t)(i & 0xFF));
            free(psram_buf);
            return;
        }
    }

    ESP_LOGI("psram_test", "PSRAM test passed.");
    free(psram_buf);
}


extern "C" void app_main(void)
{
    printf("Hello world!\n");

    /* Print chip information */
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    printf("This is %s chip with %d CPU core(s), %s%s%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
           (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
           (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    printf("silicon revision v%d.%d, ", major_rev, minor_rev);
    if(esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        printf("Get flash size failed");
        return;
    }

    printf("%" PRIu32 "MB %s flash\n", flash_size / (uint32_t)(1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    esp_err_t ret = esp_psram_init();
    if (ret == ESP_OK) {
       printf("PSRAM is available and initialized.\n");
    } else {
       printf("PSRAM initialization failed.\n");
    }

    test_psram_raw_access();

    ESP_ERROR_CHECK(i2c_master_init());
    i2c_scan_devices_all();

    icm20602_check_whoami();

    qmc5883_init();
    qmc5883_check_presence();

    int16_t x, y, z;

    qmc5883_read_xyz(&x, &y, &z);
    ESP_LOGI(TAG, "QMC5883 X: %d, Y: %d, Z: %d", x, y, z);

    printf("Minimum free heap size: %" PRIu32 " bytes\n", esp_get_minimum_free_heap_size());
    gpio_set_direction(GPIO_NUM_46, GPIO_MODE_INPUT);   // Set GPIO46 as input
    gpio_pullup_en(GPIO_NUM_46);                         // Enable pull-up resistor
    gpio_pulldown_dis(GPIO_NUM_46);

    for (int i = 10; i >= 0; i--) {
        printf("Restarting in %d seconds...\n", i);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    printf("Restarting now.\n");
    fflush(stdout);
    esp_restart();
}
