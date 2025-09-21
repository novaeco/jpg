#include "sd_card.h"
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_vfs_fat.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "esp_idf_version.h"
#include "app_config.h"
#include "sdspi_ch422_host.h"

static const char *TAG = "sdcard";
static sdmmc_card_t *s_card = NULL;
static bool s_mounted = false;
static const char *s_mount_point = "/sdcard";
static ch422_handle_t *s_expander = NULL;
static char *s_vfs_path_dup = NULL;

#define CHECK_EXECUTE_RESULT(err, str) do { \
    if ((err) != ESP_OK) { \
        ESP_LOGE(TAG, str " (0x%x).", err); \
        goto cleanup; \
    } \
} while (0)

static esp_err_t mount_prepare_mem(const char *base_path,
                                   BYTE *out_pdrv,
                                   char **out_dup_path,
                                   sdmmc_card_t **out_card)
{
    esp_err_t err = ESP_OK;
    char *dup_path = NULL;
    sdmmc_card_t *card = NULL;

    BYTE pdrv = FF_DRV_NOT_USED;
    if (ff_diskio_get_drive(&pdrv) != ESP_OK || pdrv == FF_DRV_NOT_USED) {
        ESP_LOGD(TAG, "the maximum count of volumes is already mounted");
        return ESP_ERR_NO_MEM;
    }

    card = (sdmmc_card_t *)malloc(sizeof(sdmmc_card_t));
    if (!card) {
        ESP_LOGD(TAG, "could not allocate sdmmc_card_t");
        err = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    dup_path = strdup(base_path);
    if (!dup_path) {
        ESP_LOGD(TAG, "could not duplicate base_path");
        err = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    *out_card = card;
    *out_pdrv = pdrv;
    *out_dup_path = dup_path;
    return ESP_OK;

cleanup:
    free(card);
    free(dup_path);
    return err;
}

static esp_err_t partition_card(const esp_vfs_fat_mount_config_t *mount_config,
                                const char *drv, sdmmc_card_t *card, BYTE pdrv)
{
    const size_t workbuf_size = 4096;
    void *workbuf = ff_memalloc(workbuf_size);
    if (!workbuf) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = ESP_OK;
    LBA_t plist[] = {100, 0, 0, 0};
    FRESULT res = f_fdisk(pdrv, plist, workbuf);
    if (res != FR_OK) {
        ESP_LOGD(TAG, "f_fdisk failed (%d)", res);
        err = ESP_FAIL;
        goto fail;
    }

    size_t alloc_unit_size = esp_vfs_fat_get_allocation_unit_size(
        card->csd.sector_size,
        mount_config->allocation_unit_size);
    ESP_LOGW(TAG, "formatting card, allocation unit size=%d", alloc_unit_size);
    const MKFS_PARM opt = {(BYTE)FM_ANY, 0, 0, 0, alloc_unit_size};
    res = f_mkfs(drv, &opt, workbuf, workbuf_size);
    if (res != FR_OK) {
        ESP_LOGD(TAG, "f_mkfs failed (%d)", res);
        err = ESP_FAIL;
    }

fail:
    free(workbuf);
    return err;
}

static esp_err_t mount_to_vfs_fat(const esp_vfs_fat_mount_config_t *mount_config,
                                  sdmmc_card_t *card,
                                  BYTE pdrv,
                                  const char *base_path)
{
    FATFS *fs = NULL;
    ff_diskio_register_sdmmc(pdrv, card);
    ff_sdmmc_set_disk_status_check(pdrv, mount_config->disk_status_check_enable);

    char drv[3] = {(char)('0' + pdrv), ':', 0};
    esp_err_t err = esp_vfs_fat_register(base_path, drv, mount_config->max_files, &fs);
    if (err == ESP_ERR_INVALID_STATE) {
        // already registered
    } else if (err != ESP_OK) {
        ESP_LOGD(TAG, "esp_vfs_fat_register failed (0x%x)", err);
        goto fail;
    }

    FRESULT res = f_mount(fs, drv, 1);
    if (res != FR_OK) {
        ESP_LOGW(TAG, "failed to mount card (%d)", res);
        err = ESP_FAIL;
        if (!((res == FR_NO_FILESYSTEM || res == FR_INT_ERR) && mount_config->format_if_mount_failed)) {
            goto fail;
        }

        err = partition_card(mount_config, drv, card, pdrv);
        if (err != ESP_OK) {
            goto fail;
        }

        ESP_LOGW(TAG, "mounting again");
        res = f_mount(fs, drv, 0);
        if (res != FR_OK) {
            ESP_LOGD(TAG, "f_mount failed after formatting (%d)", res);
            err = ESP_FAIL;
            goto fail;
        }
    }

    return ESP_OK;

fail:
    if (fs) {
        f_mount(NULL, drv, 0);
    }
    esp_vfs_fat_unregister_path(base_path);
    ff_diskio_unregister(pdrv);
    return err;
}

static void call_host_deinit(const sdmmc_host_t *host_config)
{
    if (host_config->flags & SDMMC_HOST_FLAG_DEINIT_ARG) {
        host_config->deinit_p(host_config->slot);
    } else if (host_config->deinit) {
        host_config->deinit();
    }
}

static esp_err_t esp_vfs_fat_sdspi_mount_ch422(const char *base_path,
                                               const sdmmc_host_t *host_config_input,
                                               const sdspi_ch422_device_config_t *slot_config,
                                               const esp_vfs_fat_mount_config_t *mount_config,
                                               sdmmc_card_t **out_card)
{
    const sdmmc_host_t *host_config = host_config_input;
    esp_err_t err;
    int card_handle = -1;
    bool host_inited = false;
    BYTE pdrv = FF_DRV_NOT_USED;
    sdmmc_card_t *card = NULL;
    char *dup_path = NULL;

    err = mount_prepare_mem(base_path, &pdrv, &dup_path, &card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mount_prepare failed");
        return err;
    }

    err = (*host_config->init)();
    CHECK_EXECUTE_RESULT(err, "host init failed");

    err = sdspi_ch422_host_init_device(slot_config, &card_handle);
    CHECK_EXECUTE_RESULT(err, "slot init failed");
    host_inited = true;

    sdmmc_host_t new_config;
    if (card_handle != host_config->slot) {
        new_config = *host_config_input;
        new_config.slot = card_handle;
        host_config = &new_config;
    }

    err = sdmmc_card_init(host_config, card);
    CHECK_EXECUTE_RESULT(err, "sdmmc_card_init failed");

    err = mount_to_vfs_fat(mount_config, card, pdrv, dup_path);
    CHECK_EXECUTE_RESULT(err, "mount_to_vfs failed");

    if (out_card) {
        *out_card = card;
    }

    if (s_vfs_path_dup == NULL) {
        s_vfs_path_dup = dup_path;
    } else {
        free(dup_path);
    }
    return ESP_OK;

cleanup:
    if (host_inited) {
        call_host_deinit(host_config);
    }
    free(card);
    free(dup_path);
    return err;
}


esp_err_t sd_card_mount(const sd_card_config_t *config, ch422_handle_t *expander)
{
    if (s_mounted) {
        return ESP_OK;
    }
    if (!expander) {
        return ESP_ERR_INVALID_ARG;
    }

    s_expander = expander;
    s_mount_point = (config && config->mount_point) ? config->mount_point : "/sdcard";

    const spi_bus_config_t *bus_cfg = app_config_sd_spi_bus();
    ESP_RETURN_ON_ERROR(spi_bus_initialize(APP_SD_HOST, bus_cfg, APP_SD_SPI_DMA_CH), TAG, "SPI init failed");

    sdmmc_host_t host = SDSPI_CH422_HOST_DEFAULT();
    host.slot = APP_SD_HOST;

    sdspi_ch422_device_config_t slot_config = SDSPI_CH422_DEVICE_CONFIG_DEFAULT();
    slot_config.base.host_id = APP_SD_HOST;
    slot_config.base.gpio_cs = SDSPI_SLOT_NO_CS;
    slot_config.base.gpio_cd = SDSPI_SLOT_NO_CD;
    slot_config.base.gpio_wp = SDSPI_SLOT_NO_WP;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
    slot_config.base.gpio_wp_polarity = SDSPI_IO_ACTIVE_LOW;
#endif
    slot_config.expander = expander;
    slot_config.expander_cs_pin = CH422_PIN_SD_CS;
    slot_config.expander_cs_active_low = true;

    const esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = config ? config->format_if_mount_failed : false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = true
    };

    esp_err_t err = esp_vfs_fat_sdspi_mount_ch422(s_mount_point, &host, &slot_config, &mount_cfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card (%s)", esp_err_to_name(err));
        spi_bus_free(APP_SD_HOST);
        return err;
    }

    s_mounted = true;
    ESP_LOGI(TAG, "Mounted SD card on %s", s_mount_point);
    return ESP_OK;
}

void sd_card_unmount(void)
{
    if (!s_mounted) {
        return;
    }
    esp_vfs_fat_sdcard_unmount(s_mount_point, s_card);
    s_card = NULL;
    if (s_vfs_path_dup) {
        free(s_vfs_path_dup);
        s_vfs_path_dup = NULL;
    }
    spi_bus_free(APP_SD_HOST);
    if (s_expander) {
        ch422_set_pin_level(s_expander, CH422_PIN_SD_CS, true);
    }
    s_mounted = false;
}

bool sd_card_is_mounted(void)
{
    return s_mounted;
}

sdmmc_card_t *sd_card_get(void)
{
    return s_card;
}
