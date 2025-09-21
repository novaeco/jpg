#include "comm_can.h"
#include "esp_log.h"
#include "esp_check.h"
#include "app_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "can";
static comm_can_config_t s_config;
static bool s_can_started = false;

static void comm_can_task(void *arg)
{
    twai_message_t message;
    while (s_can_started) {
        esp_err_t err = twai_receive(&message, pdMS_TO_TICKS(100));
        if (err == ESP_OK && s_config.rx_cb) {
            s_config.rx_cb(&message, s_config.user_ctx);
        }
    }
    vTaskDelete(NULL);
}

esp_err_t comm_can_start(const comm_can_config_t *config)
{
    if (s_can_started) {
        return ESP_OK;
    }
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    s_config = *config;

    if (s_config.expander) {
        ch422_set_pin_level(s_config.expander, CH422_PIN_USB_CAN_SEL, APP_USB_SEL_ACTIVE_CAN);
    }

    twai_general_config_t g_cfg = TWAI_GENERAL_CONFIG_DEFAULT(APP_CAN_TX_GPIO, APP_CAN_RX_GPIO, TWAI_MODE_NORMAL);
    g_cfg.tx_queue_len = 10;
    g_cfg.rx_queue_len = 10;

    twai_timing_config_t t_cfg = TWAI_TIMING_CONFIG_500KBITS();
    if (s_config.bitrate == 250000) {
        t_cfg = TWAI_TIMING_CONFIG_250KBITS();
    } else if (s_config.bitrate == 125000) {
        t_cfg = TWAI_TIMING_CONFIG_125KBITS();
    }

    twai_filter_config_t f_cfg = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    ESP_RETURN_ON_ERROR(twai_driver_install(&g_cfg, &t_cfg, &f_cfg), TAG, "twai install failed");
    ESP_RETURN_ON_ERROR(twai_start(), TAG, "twai start failed");
    s_can_started = true;

    if (config->rx_cb) {
        xTaskCreate(comm_can_task, "can_rx", 4096, NULL, 6, NULL);
    }

    ESP_LOGI(TAG, "CAN started at %u bps", (unsigned)s_config.bitrate);
    return ESP_OK;
}

void comm_can_stop(void)
{
    if (!s_can_started) {
        return;
    }
    s_can_started = false;
    twai_stop();
    twai_driver_uninstall();
    if (s_config.expander) {
        ch422_set_pin_level(s_config.expander, CH422_PIN_USB_CAN_SEL, APP_USB_SEL_ACTIVE_USB);
    }
}

esp_err_t comm_can_send(const twai_message_t *message, TickType_t ticks_to_wait)
{
    if (!s_can_started || !message) {
        return ESP_ERR_INVALID_STATE;
    }
    return twai_transmit(message, ticks_to_wait);
}
