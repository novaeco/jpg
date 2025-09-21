#pragma once

#include "esp_err.h"
#include "driver/uart.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uart_port_t port;
    int tx_pin;
    int rx_pin;
    int de_pin;
    int baudrate;
    int buffer_size;
} comm_rs485_config_t;

typedef void (*comm_rs485_rx_cb_t)(const uint8_t *data, size_t len, void *ctx);

esp_err_t comm_rs485_init(const comm_rs485_config_t *config, comm_rs485_rx_cb_t rx_cb, void *ctx);
void comm_rs485_deinit(void);
esp_err_t comm_rs485_write(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
