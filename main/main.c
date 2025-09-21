#include <stdio.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "display_driver.h"
#include "sd_card.h"
#include "gallery.h"
#include "ui.h"
#include "comm_usb.h"
#include "comm_can.h"
#include "comm_rs485.h"
#include "app_config.h"

static const char *TAG = "app";
static display_driver_handles_t s_display;

static void gallery_cb(const gallery_event_t *event, void *user_ctx)
{
    LV_UNUSED(user_ctx);
    ui_handle_gallery_event(event);
}

static void usb_rx_handler(const uint8_t *data, size_t len, void *ctx)
{
    LV_UNUSED(ctx);
    ESP_LOGI(TAG, "USB RX %.*s", (int)len, (const char *)data);
}

static void can_rx_handler(const twai_message_t *message, void *ctx)
{
    LV_UNUSED(ctx);
    ESP_LOGI(TAG, "CAN RX id=0x%03x len=%d", message->identifier, message->data_length_code);
}

static void rs485_rx_handler(const uint8_t *data, size_t len, void *ctx)
{
    LV_UNUSED(ctx);
    ESP_LOGI(TAG, "RS485 RX %d bytes", (int)len);
}

static esp_err_t ensure_directory(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        return ESP_OK;
    }
    return mkdir(path, 0775) == 0 ? ESP_OK : ESP_FAIL;
}

void app_main(void)
{
    esp_log_level_set("gpio", ESP_LOG_WARN);
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(nvs_flash_init());

    sd_card_config_t sd_cfg = {
        .mount_point = "/sdcard",
        .format_if_mount_failed = false,
    };
    ESP_ERROR_CHECK(display_driver_init(&s_display));
    ESP_ERROR_CHECK(sd_card_mount(&sd_cfg, display_driver_expander()));
    ensure_directory(APP_GALLERY_ROOT_PATH);
    ensure_directory(APP_GALLERY_THUMBNAIL_PATH);

    gallery_config_t gallery_cfg = {
        .root_path = APP_GALLERY_ROOT_PATH,
        .event_cb = gallery_cb,
        .event_ctx = NULL,
        .slideshow_interval_ms = APP_GALLERY_SLIDESHOW_INTERVAL_MS,
        .thumb_long_side = APP_GALLERY_THUMBNAIL_LONG_SIDE,
        .thumb_short_side = APP_GALLERY_THUMBNAIL_SHORT_SIDE,
    };
    esp_err_t gallery_err = gallery_start(&gallery_cfg);

    ESP_ERROR_CHECK(ui_init(&s_display));
    ui_update_gallery_items();
    ui_set_slideshow_enabled(gallery_is_slideshow_enabled());

    if (gallery_err == ESP_OK) {
        esp_err_t refresh = gallery_refresh_thumbnails();
        if (refresh != ESP_OK) {
            ESP_LOGW(TAG, "Thumbnail refresh failed: %s", esp_err_to_name(refresh));
        }
        esp_err_t load0 = gallery_goto(0);
        if (load0 != ESP_OK) {
            ESP_LOGW(TAG, "Initial image load failed: %s", esp_err_to_name(load0));
        }
    } else {
        ESP_LOGE(TAG, "Gallery start failed: %s", esp_err_to_name(gallery_err));
    }

    comm_usb_init(usb_rx_handler, NULL);

    comm_can_config_t can_cfg = {
        .bitrate = APP_CAN_DEFAULT_BAUDRATE,
        .rx_cb = can_rx_handler,
        .user_ctx = NULL,
        .expander = display_driver_expander(),
    };
    comm_can_start(&can_cfg);

    comm_rs485_config_t rs_cfg = {
        .port = APP_RS485_UART_NUM,
        .tx_pin = APP_RS485_TX_GPIO,
        .rx_pin = APP_RS485_RX_GPIO,
        .de_pin = APP_RS485_DE_GPIO,
        .baudrate = APP_RS485_DEFAULT_BAUDRATE,
        .buffer_size = 256,
    };
    comm_rs485_init(&rs_cfg, rs485_rx_handler, NULL);

    ESP_LOGI(TAG, "System initialized. Images: %u", (unsigned)gallery_image_count());
}
