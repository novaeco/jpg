#include "display_driver.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_lvgl_port.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_gt911.h"

typedef struct {
    lv_display_t *display;
    lv_indev_t *touch;
    ch422_handle_t *expander;
    esp_lcd_panel_handle_t panel_handle;
    esp_lcd_touch_handle_t touch_handle;
    esp_lcd_panel_io_handle_t touch_io;
    esp_timer_handle_t backlight_timer;
    uint8_t backlight_percent;
    uint8_t pwm_counter;
    bool pwm_high;
} display_driver_ctx_t;

static const char *TAG = "disp";
static display_driver_ctx_t s_ctx = {0};

static void backlight_timer_cb(void *arg)
{
    display_driver_ctx_t *ctx = (display_driver_ctx_t *)arg;
    const uint8_t duty = ctx->backlight_percent;
    if (duty >= 100) {
        esp_err_t err = ch422_set_pin_level(ctx->expander, CH422_PIN_BACKLIGHT, true);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Backlight timer failed to force high: %s", esp_err_to_name(err));
        }
        return;
    }
    if (duty == 0) {
        esp_err_t err = ch422_set_pin_level(ctx->expander, CH422_PIN_BACKLIGHT, false);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Backlight timer failed to force low: %s", esp_err_to_name(err));
        }
        return;
    }
    ctx->pwm_counter = (ctx->pwm_counter + 1) % 100;
    bool level = ctx->pwm_counter < duty;
    esp_err_t err = ch422_set_pin_level(ctx->expander, CH422_PIN_BACKLIGHT, level);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Backlight timer failed to update PWM: %s", esp_err_to_name(err));
        return;
    }
}

static esp_err_t init_backlight_pwm(display_driver_ctx_t *ctx)
{
    const esp_timer_create_args_t args = {
        .callback = backlight_timer_cb,
        .arg = ctx,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "bl_pwm"
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&args, &ctx->backlight_timer), TAG, "timer create failed");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(ctx->backlight_timer, APP_UI_BACKLIGHT_TIMER_PERIOD_US), TAG, "timer start failed");
    return ESP_OK;
}

static esp_err_t init_lvgl(display_driver_ctx_t *ctx)
{
    const lvgl_port_cfg_t cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&cfg), TAG, "LVGL init failed");

    const app_display_pin_config_t *pins = app_config_display_pins();
    esp_lcd_rgb_panel_config_t panel_cfg = {
        .clk_src = LCD_CLK_SRC_PLL160M,
        .data_width = 16,
        .bits_per_pixel = 16,
        .de_gpio_num = pins->de,
        .pclk_gpio_num = pins->pclk,
        .vsync_gpio_num = pins->vsync,
        .hsync_gpio_num = pins->hsync,
        .disp_gpio_num = GPIO_NUM_NC,
        .data_gpio_nums = {
            pins->data[0], pins->data[1], pins->data[2], pins->data[3],
            pins->data[4], pins->data[5], pins->data[6], pins->data[7],
            pins->data[8], pins->data[9], pins->data[10], pins->data[11],
            pins->data[12], pins->data[13], pins->data[14], pins->data[15],
        },
        .timings = {
            .pclk_hz = APP_LCD_PIXEL_CLOCK_HZ,
            .h_res = APP_LCD_H_RES,
            .v_res = APP_LCD_V_RES,
            .hsync_pulse_width = APP_LCD_HSYNC_PULSE_WIDTH,
            .hsync_back_porch = APP_LCD_HSYNC_BACK_PORCH,
            .hsync_front_porch = APP_LCD_HSYNC_FRONT_PORCH,
            .vsync_pulse_width = APP_LCD_VSYNC_PULSE_WIDTH,
            .vsync_back_porch = APP_LCD_VSYNC_BACK_PORCH,
            .vsync_front_porch = APP_LCD_VSYNC_FRONT_PORCH,
            .flags = {
                .pclk_active_neg = true,
                .hsync_idle_low = false,
                .vsync_idle_low = false,
                .de_idle_high = true,
            }
        },
        .flags = {
            .fb_in_psram = 1,
        },
        .num_fbs = 2,
        .bounce_buffer_size_px = APP_LCD_H_RES * 20,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&panel_cfg, &ctx->panel_handle), TAG, "RGB panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(ctx->panel_handle), TAG, "panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(ctx->panel_handle, true), TAG, "panel enable failed");

    const lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = ctx->panel_handle,
        .buffer_size = APP_LCD_H_RES * 50,
        .double_buffer = true,
        .hres = APP_LCD_H_RES,
        .vres = APP_LCD_V_RES,
        .monochrome = false,
#if LVGL_VERSION_MAJOR >= 9
        .color_format = LV_COLOR_FORMAT_RGB565,
#endif
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = true,
            .buff_spiram = true,
#if LVGL_VERSION_MAJOR >= 9
            .swap_bytes = false,
#endif
            .full_refresh = false,
        }
    };

    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode = true,
            .avoid_tearing = true,
        }
    };
    ctx->display = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    if (!ctx->display) {
        ESP_LOGE(TAG, "Failed to register LVGL display");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t init_touch(display_driver_ctx_t *ctx)
{
    const esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)APP_I2C_HOST, &io_cfg, &ctx->touch_io), TAG, "touch IO");

    const esp_lcd_touch_config_t touch_cfg = {
        .x_max = APP_LCD_H_RES,
        .y_max = APP_LCD_V_RES,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = APP_TOUCH_INT_GPIO,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(ctx->touch_io, &touch_cfg, &ctx->touch_handle), TAG, "touch init failed");

    const lvgl_port_touch_cfg_t touch_port_cfg = {
        .disp = ctx->display,
        .handle = ctx->touch_handle,
    };
    ctx->touch = lvgl_port_add_touch(&touch_port_cfg);
    if (!ctx->touch) {
        ESP_LOGE(TAG, "Failed to add LVGL touch input");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t display_driver_init(display_driver_handles_t *out_handles)
{
    if (s_ctx.display) {
        if (out_handles) {
            out_handles->display = s_ctx.display;
            out_handles->touch = s_ctx.touch;
            out_handles->expander = s_ctx.expander;
        }
        return ESP_OK;
    }

    const i2c_config_t *i2c_cfg = app_config_i2c();
    ESP_RETURN_ON_ERROR(i2c_param_config(APP_I2C_HOST, i2c_cfg), TAG, "I2C setup failed");
    ESP_RETURN_ON_ERROR(i2c_driver_install(APP_I2C_HOST, I2C_MODE_MASTER, 0, 0, 0), TAG, "I2C driver install failed");

    const ch422_config_t ch_cfg = {
        .port = APP_I2C_HOST,
        .io_default_level = 0x00,
        .install_driver = false,
    };
    ESP_RETURN_ON_ERROR(ch422_init(&ch_cfg, &s_ctx.expander), TAG, "CH422 init failed");

    ESP_LOGI(TAG, "Enabling LCD power rail (CH422_PIN_LCD_VDD_EN)");
    ESP_RETURN_ON_ERROR(ch422_set_pin_level(s_ctx.expander, CH422_PIN_LCD_VDD_EN, true), TAG, "Failed to enable LCD power");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "Resetting LCD panel via CH422 pulse (low=1000us, high=5000us)");
    ESP_RETURN_ON_ERROR(ch422_pulse(s_ctx.expander, CH422_PIN_LCD_RESET, 1000, 5000), TAG, "LCD reset pulse failed");
    ESP_LOGI(TAG, "Resetting touch controller via CH422 pulse (low=2000us, high=2000us)");
    ESP_RETURN_ON_ERROR(ch422_pulse(s_ctx.expander, CH422_PIN_TOUCH_RESET, 2000, 2000), TAG, "Touch reset pulse failed");
    ESP_LOGI(TAG, "Selecting USB interface on CH422 (PIN_USB_CAN_SEL level=%d)", APP_USB_SEL_ACTIVE_USB);
    ESP_RETURN_ON_ERROR(ch422_set_pin_level(s_ctx.expander, CH422_PIN_USB_CAN_SEL, APP_USB_SEL_ACTIVE_USB), TAG, "USB/CAN selection failed");
    ESP_LOGI(TAG, "Driving SD card CS high via CH422 to deselect the card");
    ESP_RETURN_ON_ERROR(ch422_set_pin_level(s_ctx.expander, CH422_PIN_SD_CS, true), TAG, "Failed to set SD CS high");
    ESP_RETURN_ON_ERROR(ch422_set_pin_level(s_ctx.expander, CH422_PIN_BACKLIGHT, false), TAG, "Failed to disable backlight");

    ESP_RETURN_ON_ERROR(init_lvgl(&s_ctx), TAG, "LVGL setup failed");
    ESP_RETURN_ON_ERROR(init_touch(&s_ctx), TAG, "Touch setup failed");
    ESP_RETURN_ON_ERROR(init_backlight_pwm(&s_ctx), TAG, "Backlight PWM init failed");

    display_driver_set_backlight_percent(60);

    if (out_handles) {
        out_handles->display = s_ctx.display;
        out_handles->touch = s_ctx.touch;
        out_handles->expander = s_ctx.expander;
    }
    return ESP_OK;
}

void display_driver_shutdown(void)
{
    if (s_ctx.backlight_timer) {
        esp_timer_stop(s_ctx.backlight_timer);
        esp_timer_delete(s_ctx.backlight_timer);
        s_ctx.backlight_timer = NULL;
    }
    if (s_ctx.touch) {
        lv_indev_delete(s_ctx.touch);
        s_ctx.touch = NULL;
    }
    if (s_ctx.display) {
        lv_display_delete(s_ctx.display);
        s_ctx.display = NULL;
    }
    if (s_ctx.touch_handle) {
        esp_lcd_touch_del(s_ctx.touch_handle);
        s_ctx.touch_handle = NULL;
    }
    if (s_ctx.touch_io) {
        esp_lcd_panel_io_del(s_ctx.touch_io);
        s_ctx.touch_io = NULL;
    }
    if (s_ctx.panel_handle) {
        esp_lcd_panel_del(s_ctx.panel_handle);
        s_ctx.panel_handle = NULL;
    }
    if (s_ctx.expander) {
        ch422_deinit(s_ctx.expander);
        s_ctx.expander = NULL;
    }
    i2c_driver_delete(APP_I2C_HOST);
}

esp_err_t display_driver_set_backlight_percent(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    s_ctx.backlight_percent = percent;
    return ESP_OK;
}

uint8_t display_driver_get_backlight_percent(void)
{
    return s_ctx.backlight_percent;
}

ch422_handle_t *display_driver_expander(void)
{
    return s_ctx.expander;
}

lv_display_t *display_driver_display(void)
{
    return s_ctx.display;
}

lv_indev_t *display_driver_touch(void)
{
    return s_ctx.touch;
}

esp_err_t display_driver_lock_lvgl(uint32_t timeout_ms)
{
    if (lvgl_port_lock(timeout_ms)) {
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

void display_driver_unlock_lvgl(void)
{
    lvgl_port_unlock();
}
