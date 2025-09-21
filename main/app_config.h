#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_common.h"
#include "driver/uart.h"
#include "driver/twai.h"
#include "ch422_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_LCD_H_RES                 (1024)
#define APP_LCD_V_RES                 (600)
#define APP_LCD_PIXEL_CLOCK_HZ        (51200000)
#define APP_LCD_HSYNC_PULSE_WIDTH     (20)
#define APP_LCD_HSYNC_BACK_PORCH      (160)
#define APP_LCD_HSYNC_FRONT_PORCH     (160)
#define APP_LCD_VSYNC_PULSE_WIDTH     (3)
#define APP_LCD_VSYNC_BACK_PORCH      (23)
#define APP_LCD_VSYNC_FRONT_PORCH     (12)

#define APP_I2C_HOST                  I2C_NUM_0
#define APP_I2C_SDA_GPIO              GPIO_NUM_8
#define APP_I2C_SCL_GPIO              GPIO_NUM_9
#define APP_I2C_SPEED_HZ              (400000)

#define APP_TOUCH_INT_GPIO            GPIO_NUM_4

#define APP_SD_HOST                   SPI2_HOST
#define APP_SD_SPI_MOSI_GPIO          GPIO_NUM_11
#define APP_SD_SPI_MISO_GPIO          GPIO_NUM_13
#define APP_SD_SPI_SCLK_GPIO          GPIO_NUM_12
#define APP_SD_SPI_DMA_CH             SPI_DMA_CH_AUTO

#define APP_USB_SEL_ACTIVE_CAN        1
#define APP_USB_SEL_ACTIVE_USB        0

#define APP_CAN_TX_GPIO               GPIO_NUM_20
#define APP_CAN_RX_GPIO               GPIO_NUM_19
#define APP_CAN_DEFAULT_BAUDRATE      (500000)

#define APP_RS485_UART_NUM            UART_NUM_1
#define APP_RS485_TX_GPIO             GPIO_NUM_15
#define APP_RS485_RX_GPIO             GPIO_NUM_16
#define APP_RS485_DE_GPIO             GPIO_NUM_NC
#define APP_RS485_DEFAULT_BAUDRATE    (115200)

#define APP_GALLERY_ROOT_PATH         "/sdcard/gallery"
#define APP_GALLERY_THUMBNAIL_PATH    "/sdcard/.thumbnails"
#define APP_GALLERY_SUPPORTED_EXT     "jpg"
#define APP_GALLERY_SLIDESHOW_INTERVAL_MS   (6000)
#define APP_GALLERY_THUMBNAIL_LONG_SIDE     (192)
#define APP_GALLERY_THUMBNAIL_SHORT_SIDE    (108)
#define APP_GALLERY_MAX_IMAGES        (512)

#define APP_UI_BACKLIGHT_PWM_FREQ_HZ  (1000)
#define APP_UI_BACKLIGHT_TIMER_PERIOD_US (1000)

typedef struct {
    gpio_num_t hsync;
    gpio_num_t vsync;
    gpio_num_t de;
    gpio_num_t pclk;
    gpio_num_t data[16];
} app_display_pin_config_t;

typedef struct {
    ch422_pin_t usb_can_sel;
    ch422_pin_t touch_reset;
    ch422_pin_t lcd_reset;
    ch422_pin_t lcd_vdd_en;
    ch422_pin_t backlight_enable;
    ch422_pin_t sd_cs;
} app_ch422_pin_config_t;

const app_display_pin_config_t *app_config_display_pins(void);
const app_ch422_pin_config_t *app_config_expander_pins(void);
const i2c_config_t *app_config_i2c(void);
const spi_bus_config_t *app_config_sd_spi_bus(void);
void *app_lvgl_psram_alloc(size_t size);

#ifdef __cplusplus
}
#endif
