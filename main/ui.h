#pragma once

#include "esp_err.h"
#include "lvgl.h"
#include "display_driver.h"
#include "gallery.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ui_init(const display_driver_handles_t *display);
void ui_handle_gallery_event(const gallery_event_t *event);
void ui_update_brightness_slider(uint8_t percent);
void ui_set_slideshow_enabled(bool enabled);
void ui_deinit(void);
void ui_update_gallery_items(void);

#ifdef __cplusplus
}
#endif
