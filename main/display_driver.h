#pragma once

#include "lvgl.h"
#include "esp_err.h"
#include "esp_event.h"
#include "app_config.h"
#include "ch422_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lv_display_t *display;
    lv_indev_t *touch;
    ch422_handle_t *expander;
} display_driver_handles_t;

esp_err_t display_driver_init(display_driver_handles_t *out_handles);
void display_driver_shutdown(void);
esp_err_t display_driver_set_backlight_percent(uint8_t percent);
uint8_t display_driver_get_backlight_percent(void);
ch422_handle_t *display_driver_expander(void);
lv_display_t *display_driver_display(void);
lv_indev_t *display_driver_touch(void);
esp_err_t display_driver_lock_lvgl(uint32_t timeout_ms);
void display_driver_unlock_lvgl(void);

#ifdef __cplusplus
}
#endif
