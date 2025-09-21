#include "comm_usb.h"
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "tinyusb.h"
#include "tusb_cdc_acm.h"

static const char *TAG = "usb";
static comm_usb_rx_cb_t s_rx_cb = NULL;
static void *s_rx_ctx = NULL;
static tinyusb_cdcacm_itf_t s_cdc_itf = TINYUSB_CDC_ACM_0;

static void cdc_rx_callback(int itf, cdcacm_event_t *event)
{
    if (event->type != CDCACM_EVENT_RX) {
        return;
    }
    size_t rx_size = 0;
    tinyusb_cdcacm_get_rx_len(itf, &rx_size);
    if (!rx_size) {
        return;
    }
    uint8_t buf[64];
    while (rx_size) {
        size_t chunk = rx_size > sizeof(buf) ? sizeof(buf) : rx_size;
        tusb_cdc_acm_read(itf, buf, chunk);
        if (s_rx_cb) {
            s_rx_cb(buf, chunk, s_rx_ctx);
        }
        rx_size -= chunk;
    }
}

esp_err_t comm_usb_init(comm_usb_rx_cb_t rx_cb, void *ctx)
{
    s_rx_cb = rx_cb;
    s_rx_ctx = ctx;

    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,
        .string_descriptor = NULL,
        .external_phy = false,
        .configuration_descriptor = NULL,
    };
    ESP_RETURN_ON_ERROR(tinyusb_driver_install(&tusb_cfg), TAG, "tinyusb install failed");

    esp_err_t ret = tinyusb_cdcacm_register_callback(s_cdc_itf, CDCACM_EVENT_RX, cdc_rx_callback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register CDC RX callback: %s", esp_err_to_name(ret));
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "cdc cb failed");
    tinyusb_cdcacm_init_config_t cdc_cfg = {
        .usb_dev = TINYUSB_USBDEV_0,
        .cdc_port = s_cdc_itf,
        .tx_unblocking = true,
        .rx_unblocking = true,
    };
    ESP_RETURN_ON_ERROR(tusb_cdc_acm_init(&cdc_cfg), TAG, "cdc init failed");
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
