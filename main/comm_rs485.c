#include "comm_rs485.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "app_config.h"

static const char *TAG = "rs485";
static comm_rs485_config_t s_config;
static comm_rs485_rx_cb_t s_rx_cb = NULL;
static void *s_rx_ctx = NULL;
static TaskHandle_t s_rs485_task = NULL;
static bool s_rs485_running = false;

static void rs485_task(void *arg)
{
    uint8_t *buf = malloc(s_config.buffer_size);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate RX buffer");
        vTaskDelete(NULL);
        return;
    }
    while (s_rs485_running) {
        int len = uart_read_bytes(s_config.port, buf, s_config.buffer_size, pdMS_TO_TICKS(100));
        if (len > 0 && s_rx_cb) {
            s_rx_cb(buf, len, s_rx_ctx);
        }
    }
    free(buf);
    vTaskDelete(NULL);
}

esp_err_t comm_rs485_init(const comm_rs485_config_t *config, comm_rs485_rx_cb_t rx_cb, void *ctx)
{
    if (s_rs485_running) {
        return ESP_OK;
    }
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    s_config = *config;
    if (s_config.port == UART_NUM_MAX) {
        s_config.port = APP_RS485_UART_NUM;
    }
    if (s_config.tx_pin < 0) {
        s_config.tx_pin = APP_RS485_TX_GPIO;
    }
    if (s_config.rx_pin < 0) {
        s_config.rx_pin = APP_RS485_RX_GPIO;
    }
    if (s_config.de_pin < 0) {
        s_config.de_pin = APP_RS485_DE_GPIO;
    }
    if (s_config.baudrate <= 0) {
        s_config.baudrate = APP_RS485_DEFAULT_BAUDRATE;
    }
    if (s_config.buffer_size <= 0) {
        s_config.buffer_size = 256;
    }
    s_rx_cb = rx_cb;
    s_rx_ctx = ctx;

    uart_config_t uart_cfg = {
        .baud_rate = s_config.baudrate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    ESP_RETURN_ON_ERROR(uart_driver_install(s_config.port, s_config.buffer_size * 2, s_config.buffer_size * 2, 0, NULL, 0), TAG, "uart install");
    ESP_RETURN_ON_ERROR(uart_param_config(s_config.port, &uart_cfg), TAG, "uart param");
    int de_pin = (s_config.de_pin == GPIO_NUM_NC) ? UART_PIN_NO_CHANGE : s_config.de_pin;
    ESP_RETURN_ON_ERROR(uart_set_pin(s_config.port, s_config.tx_pin, s_config.rx_pin, de_pin, UART_PIN_NO_CHANGE), TAG, "uart pins");
    ESP_RETURN_ON_ERROR(uart_set_mode(s_config.port, UART_MODE_RS485_HALF_DUPLEX), TAG, "uart mode");

    s_rs485_running = true;
    if (s_rx_cb) {
        xTaskCreate(rs485_task, "rs485", 4096, NULL, 5, &s_rs485_task);
    }
    ESP_LOGI(TAG, "RS485 ready on port %d", s_config.port);
    return ESP_OK;
}

void comm_rs485_deinit(void)
{
    if (!s_rs485_running) {
        return;
    }
    s_rs485_running = false;
    if (s_rs485_task) {
        vTaskDelay(pdMS_TO_TICKS(20));
        s_rs485_task = NULL;
    }
    uart_driver_delete(s_config.port);
}

esp_err_t comm_rs485_write(const uint8_t *data, size_t len)
{
    if (!s_rs485_running || !data || !len) {
        return ESP_ERR_INVALID_STATE;
    }
    int written = uart_write_bytes(s_config.port, (const char *)data, len);
    return (written == (int)len) ? ESP_OK : ESP_FAIL;
}
