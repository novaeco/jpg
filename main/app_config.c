#include "app_config.h"
#include "esp_heap_caps.h"

static const app_display_pin_config_t s_display_pins = {
    .hsync = GPIO_NUM_46,
    .vsync = GPIO_NUM_3,
    .de = GPIO_NUM_5,
    .pclk = GPIO_NUM_7,
    .data = {
        GPIO_NUM_14, /* B3 -> D0 */
        GPIO_NUM_38, /* B4 -> D1 */
        GPIO_NUM_18, /* B5 -> D2 */
        GPIO_NUM_17, /* B6 -> D3 */
        GPIO_NUM_10, /* B7 -> D4 */
        GPIO_NUM_39, /* G2 -> D5 */
        GPIO_NUM_0,  /* G3 -> D6 */
        GPIO_NUM_45, /* G4 -> D7 */
        GPIO_NUM_48, /* G5 -> D8 */
        GPIO_NUM_47, /* G6 -> D9 */
        GPIO_NUM_21, /* G7 -> D10 */
        GPIO_NUM_1,  /* R3 -> D11 */
        GPIO_NUM_2,  /* R4 -> D12 */
        GPIO_NUM_42, /* R5 -> D13 */
        GPIO_NUM_41, /* R6 -> D14 */
        GPIO_NUM_40, /* R7 -> D15 */
    }
};

static const app_ch422_pin_config_t s_expander_pins = {
    .usb_can_sel = CH422_PIN_USB_CAN_SEL,
    .touch_reset = CH422_PIN_TOUCH_RESET,
    .lcd_reset = CH422_PIN_LCD_RESET,
    .lcd_vdd_en = CH422_PIN_LCD_VDD_EN,
    .backlight_enable = CH422_PIN_BACKLIGHT,
    .sd_cs = CH422_PIN_SD_CS,
};

static const i2c_config_t s_i2c_cfg = {
    .mode = I2C_MODE_MASTER,
    .sda_io_num = APP_I2C_SDA_GPIO,
    .sda_pullup_en = GPIO_PULLUP_DISABLE,
    .scl_io_num = APP_I2C_SCL_GPIO,
    .scl_pullup_en = GPIO_PULLUP_DISABLE,
    .master.clk_speed = APP_I2C_SPEED_HZ,
    .clk_flags = 0,
};

static const spi_bus_config_t s_sd_spi_bus_cfg = {
    .mosi_io_num = APP_SD_SPI_MOSI_GPIO,
    .miso_io_num = APP_SD_SPI_MISO_GPIO,
    .sclk_io_num = APP_SD_SPI_SCLK_GPIO,
    .quadwp_io_num = GPIO_NUM_NC,
    .quadhd_io_num = GPIO_NUM_NC,
    .max_transfer_sz = 64 * 1024,
};

const app_display_pin_config_t *app_config_display_pins(void)
{
    return &s_display_pins;
}

const app_ch422_pin_config_t *app_config_expander_pins(void)
{
    return &s_expander_pins;
}

const i2c_config_t *app_config_i2c(void)
{
    return &s_i2c_cfg;
}

const spi_bus_config_t *app_config_sd_spi_bus(void)
{
    return &s_sd_spi_bus_cfg;
}

void *app_lvgl_psram_alloc(size_t size)
{
    if (size == 0) {
        return NULL;
    }
    return heap_caps_malloc_prefer(
        size,
        2,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
        MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
