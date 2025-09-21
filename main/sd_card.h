#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "sdmmc_cmd.h"
#include "ch422_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *mount_point;
    bool format_if_mount_failed;
} sd_card_config_t;

esp_err_t sd_card_mount(const sd_card_config_t *config, ch422_handle_t *expander);
void sd_card_unmount(void);
bool sd_card_is_mounted(void);
sdmmc_card_t *sd_card_get(void);

#ifdef __cplusplus
}
#endif
