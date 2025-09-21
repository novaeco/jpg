#include "comm_usb.h"
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "sdkconfig.h"
#include "tinyusb.h"
#include "tusb_cdc_acm.h"

static const char *TAG = "usb";
static comm_usb_rx_cb_t s_rx_cb = NULL;
static void *s_rx_ctx = NULL;
static tinyusb_cdcacm_itf_t s_cdc_itf = TINYUSB_CDC_ACM_0;

#ifndef CONFIG_TINYUSB_CDC_RX_BUFSIZE
#define CONFIG_TINYUSB_CDC_RX_BUFSIZE 64
#endif

#define USB_RX_BUFFER_SIZE CONFIG_TINYUSB_CDC_RX_BUFSIZE

static void cdc_rx_callback(int itf, cdcacm_event_t *event)
{
    if (!event || event->type != CDC_EVENT_RX) {
        return;
    }
    uint8_t buf[USB_RX_BUFFER_SIZE];
    size_t rx_size = 0;
    do {
        esp_err_t ret = tinyusb_cdcacm_read(itf, buf, sizeof(buf), &rx_size);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "CDC read failed: %s", esp_err_to_name(ret));
            break;
        }
        if (!rx_size) {
            break;
        }
        if (s_rx_cb) {
            s_rx_cb(buf, rx_size, s_rx_ctx);
        }
    } while (rx_size == sizeof(buf));
}

esp_err_t comm_usb_init(comm_usb_rx_cb_t rx_cb, void *ctx)
{
    s_rx_cb = rx_cb;
    s_rx_ctx = ctx;

    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,
        .string_descriptor = NULL,
        .external_phy = false,
#if (TUD_OPT_HIGH_SPEED)
        .fs_configuration_descriptor = NULL,
        .hs_configuration_descriptor = NULL,
        .qualifier_descriptor = NULL,
#else
        .configuration_descriptor = NULL,
#endif
    };
    ESP_RETURN_ON_ERROR(tinyusb_driver_install(&tusb_cfg), TAG, "tinyusb install failed");

    tinyusb_config_cdcacm_t cdc_cfg = {
        .usb_dev = TINYUSB_USBDEV_0,
        .cdc_port = s_cdc_itf,
        .rx_unread_buf_sz = CONFIG_TINYUSB_CDC_RX_BUFSIZE,
        .callback_rx = NULL,
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = NULL,
    };
    ESP_RETURN_ON_ERROR(tusb_cdc_acm_init(&cdc_cfg), TAG, "cdc init failed");
    ESP_RETURN_ON_ERROR(tinyusb_cdcacm_register_callback(s_cdc_itf, CDC_EVENT_RX, cdc_rx_callback), TAG, "cdc cb failed");
    ESP_LOGI(TAG, "USB CDC ready");
    return ESP_OK;
}

esp_err_t comm_usb_write(const uint8_t *data, size_t len)
{
    if (!data || !len) {
        return ESP_ERR_INVALID_ARG;
    }
    if (tusb_cdc_acm_write_queue(s_cdc_itf, data, len) != ESP_OK) {
        return ESP_FAIL;
    }
    return tusb_cdc_acm_write_flush(s_cdc_itf, 0);
}

esp_err_t comm_usb_write_line(const char *line)
{
    if (!line) {
        return ESP_ERR_INVALID_ARG;
    }
    return comm_usb_write((const uint8_t *)line, strlen(line));
}
