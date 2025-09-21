#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*comm_usb_rx_cb_t)(const uint8_t *data, size_t len, void *ctx);

esp_err_t comm_usb_init(comm_usb_rx_cb_t rx_cb, void *ctx);
esp_err_t comm_usb_write(const uint8_t *data, size_t len);
esp_err_t comm_usb_write_line(const char *line);

#ifdef __cplusplus
}
#endif
