#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CH422_PIN_TOUCH_RESET = 0,
    CH422_PIN_BACKLIGHT = 1,
    CH422_PIN_LCD_RESET = 2,
    CH422_PIN_SD_CS = 3,
    CH422_PIN_USB_CAN_SEL = 4,
    CH422_PIN_LCD_VDD_EN = 5,
    CH422_PIN_EXT0 = 6,
    CH422_PIN_EXT1 = 7,
} ch422_pin_t;

typedef struct ch422_handle_t ch422_handle_t;

typedef struct {
    i2c_port_t port;
    uint8_t io_default_level;
    bool install_driver;
} ch422_config_t;

esp_err_t ch422_init(const ch422_config_t *config, ch422_handle_t **out_handle);
esp_err_t ch422_deinit(ch422_handle_t *handle);
esp_err_t ch422_set_pin_level(ch422_handle_t *handle, ch422_pin_t pin, bool level);
esp_err_t ch422_get_pin_level(ch422_handle_t *handle, ch422_pin_t pin, bool *level);
esp_err_t ch422_update_masked(ch422_handle_t *handle, uint8_t mask, uint8_t value);
esp_err_t ch422_pulse(ch422_handle_t *handle, ch422_pin_t pin, uint32_t low_delay_us, uint32_t high_delay_us);
uint8_t ch422_cached_state(const ch422_handle_t *handle);

#ifdef __cplusplus
}
#endif
