#include "ch422_driver.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_rom/ets_sys.h"

#define CH422_ADDR_SYSCTRL   (0x48 >> 1)
#define CH422_ADDR_IO0       (0x70 >> 1)

static const char *TAG = "ch422";

struct ch422_handle_t {
    i2c_port_t port;
    SemaphoreHandle_t mutex;
    uint8_t io_state;
    bool driver_installed;
};

static esp_err_t ch422_write_byte(ch422_handle_t *handle, uint8_t addr, uint8_t data)
{
    return i2c_master_write_to_device(handle->port, addr, &data, 1, pdMS_TO_TICKS(100));
}

esp_err_t ch422_init(const ch422_config_t *config, ch422_handle_t **out_handle)
{
    if (!config || !out_handle) {
        return ESP_ERR_INVALID_ARG;
    }

    ch422_handle_t *handle = calloc(1, sizeof(ch422_handle_t));
    if (!handle) {
        return ESP_ERR_NO_MEM;
    }

    handle->port = config->port;
    handle->io_state = config->io_default_level;
    handle->mutex = xSemaphoreCreateMutex();
    if (!handle->mutex) {
        free(handle);
        return ESP_ERR_NO_MEM;
    }

    if (config->install_driver) {
        esp_err_t err = i2c_driver_install(config->port, I2C_MODE_MASTER, 0, 0, 0);
        if (err != ESP_OK) {
            vSemaphoreDelete(handle->mutex);
            free(handle);
            return err;
        }
        handle->driver_installed = true;
    }

    /* Enable IO outputs */
    esp_err_t err = ch422_write_byte(handle, CH422_ADDR_SYSCTRL, 0x01);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable IO outputs (%s)", esp_err_to_name(err));
        ch422_deinit(handle);
        return err;
    }

    /* Initialize IO state */
    err = ch422_write_byte(handle, CH422_ADDR_IO0, handle->io_state);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize IO state (%s)", esp_err_to_name(err));
        ch422_deinit(handle);
        return err;
    }

    *out_handle = handle;
    return ESP_OK;
}

esp_err_t ch422_deinit(ch422_handle_t *handle)
{
    if (!handle) {
        return ESP_OK;
    }
    if (handle->driver_installed) {
        i2c_driver_delete(handle->port);
    }
    if (handle->mutex) {
        vSemaphoreDelete(handle->mutex);
    }
    free(handle);
    return ESP_OK;
}

esp_err_t ch422_update_masked(ch422_handle_t *handle, uint8_t mask, uint8_t value)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(handle->mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    uint8_t next = (handle->io_state & ~mask) | (value & mask);
    esp_err_t err = ch422_write_byte(handle, CH422_ADDR_IO0, next);
    if (err == ESP_OK) {
        handle->io_state = next;
    }
    xSemaphoreGive(handle->mutex);
    return err;
}

esp_err_t ch422_set_pin_level(ch422_handle_t *handle, ch422_pin_t pin, bool level)
{
    if (!handle || pin < CH422_PIN_TOUCH_RESET || pin > CH422_PIN_EXT1) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t mask = (1u << (uint8_t)pin);
    uint8_t value = level ? mask : 0;
    return ch422_update_masked(handle, mask, value);
}

esp_err_t ch422_get_pin_level(ch422_handle_t *handle, ch422_pin_t pin, bool *level)
{
    if (!handle || !level || pin < CH422_PIN_TOUCH_RESET || pin > CH422_PIN_EXT1) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(handle->mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    *level = (handle->io_state & (1u << (uint8_t)pin)) != 0;
    xSemaphoreGive(handle->mutex);
    return ESP_OK;
}

esp_err_t ch422_pulse(ch422_handle_t *handle, ch422_pin_t pin, uint32_t low_delay_us, uint32_t high_delay_us)
{
    esp_err_t err = ch422_set_pin_level(handle, pin, false);
    if (err != ESP_OK) {
        return err;
    }
    if (low_delay_us) {
        ets_delay_us(low_delay_us);
    }
    err = ch422_set_pin_level(handle, pin, true);
    if (err != ESP_OK) {
        return err;
    }
    if (high_delay_us) {
        ets_delay_us(high_delay_us);
    }
    return ESP_OK;
}

uint8_t ch422_cached_state(const ch422_handle_t *handle)
{
    if (!handle) {
        return 0;
    }
    return handle->io_state;
}
