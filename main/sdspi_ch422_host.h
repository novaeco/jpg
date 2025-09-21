/*
 * SPDX-FileCopyrightText: 2015-2021 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "sdmmc_types.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/sdspi_host.h"
#include "freertos/FreeRTOS.h"
#include "ch422_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Handle representing an SD SPI device using CH422 controlled chip select
typedef int sdspi_ch422_dev_handle_t;

/**
 * @brief Device configuration for SD SPI host with CH422-controlled chip select.
 */
typedef struct {
    sdspi_device_config_t base;      ///< Standard SDSPI device configuration
    ch422_handle_t *expander;        ///< Optional CH422 handle controlling chip select
    ch422_pin_t expander_cs_pin;     ///< Expander output pin used for CS (ignored if expander == NULL)
    bool expander_cs_active_low;     ///< True if the expander drives CS low for active state
} sdspi_ch422_device_config_t;

#define SDSPI_CH422_DEVICE_CONFIG_DEFAULT()                                    \
    (sdspi_ch422_device_config_t){                                            \
        .base = {                                                             \
            .host_id = SDSPI_DEFAULT_HOST,                                    \
            .gpio_cs = SDSPI_SLOT_NO_CS,                                      \
            .gpio_cd = SDSPI_SLOT_NO_CD,                                      \
            .gpio_wp = SDSPI_SLOT_NO_WP,                                      \
            .gpio_int = SDSPI_SLOT_NO_INT,                                    \
        },                                                                    \
        .expander = NULL,                                                     \
        .expander_cs_pin = CH422_PIN_SD_CS,                                   \
        .expander_cs_active_low = true,                                       \
    }

/**
 * @brief Default host configuration structure for CH422 SDSPI driver.
 */
#define SDSPI_CH422_HOST_DEFAULT()                                            \
    (sdmmc_host_t){                                                           \
        .flags = SDMMC_HOST_FLAG_SPI | SDMMC_HOST_FLAG_DEINIT_ARG,            \
        .slot = SDSPI_DEFAULT_HOST,                                           \
        .max_freq_khz = SDMMC_FREQ_DEFAULT,                                   \
        .io_voltage = 3.3f,                                                   \
        .init = &sdspi_ch422_host_init,                                       \
        .set_bus_width = NULL,                                                \
        .get_bus_width = NULL,                                                \
        .set_bus_ddr_mode = NULL,                                             \
        .set_card_clk = &sdspi_ch422_host_set_card_clk,                       \
        .do_transaction = &sdspi_ch422_host_do_transaction,                  \
        .deinit_p = &sdspi_ch422_host_remove_device,                          \
        .io_int_enable = &sdspi_ch422_host_io_int_enable,                     \
        .io_int_wait = &sdspi_ch422_host_io_int_wait,                         \
        .command_timeout_ms = 0,                                              \
    }

esp_err_t sdspi_ch422_host_init(void);
esp_err_t sdspi_ch422_host_deinit(void);
esp_err_t sdspi_ch422_host_init_device(const sdspi_ch422_device_config_t* dev_config,
                                       sdspi_ch422_dev_handle_t* out_handle);
esp_err_t sdspi_ch422_host_remove_device(sdspi_ch422_dev_handle_t handle);
esp_err_t sdspi_ch422_host_do_transaction(sdspi_ch422_dev_handle_t handle, sdmmc_command_t *cmdinfo);
esp_err_t sdspi_ch422_host_set_card_clk(sdspi_ch422_dev_handle_t handle, uint32_t freq_khz);
esp_err_t sdspi_ch422_host_io_int_enable(sdspi_ch422_dev_handle_t handle);
esp_err_t sdspi_ch422_host_io_int_wait(sdspi_ch422_dev_handle_t handle, TickType_t timeout_ticks);

#ifdef __cplusplus
}
#endif

