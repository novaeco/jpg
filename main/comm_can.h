#pragma once

#include "esp_err.h"
#include "driver/twai.h"
#include "ch422_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*comm_can_rx_cb_t)(const twai_message_t *message, void *ctx);

typedef struct {
    uint32_t bitrate;
    comm_can_rx_cb_t rx_cb;
    void *user_ctx;
    ch422_handle_t *expander;
} comm_can_config_t;

esp_err_t comm_can_start(const comm_can_config_t *config);
void comm_can_stop(void);
esp_err_t comm_can_send(const twai_message_t *message, TickType_t ticks_to_wait);

#ifdef __cplusplus
}
#endif
