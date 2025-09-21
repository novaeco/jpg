#include "gallery.h"
#include <dirent.h>
#include <sys/stat.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "app_config.h"

#define GALLERY_QUEUE_DEPTH 10
#define GALLERY_THUMB_BATCH 4

typedef enum {
    GALLERY_CMD_LOAD_INDEX = 0,
    GALLERY_CMD_NEXT,
    GALLERY_CMD_PREV,
    GALLERY_CMD_REFRESH,
    GALLERY_CMD_STOP
} gallery_cmd_id_t;

typedef struct {
    gallery_cmd_id_t id;
    size_t index;
} gallery_cmd_t;

typedef struct {
    char *path;
    size_t size;
    bool thumb_valid;
    jpeg_image_t thumb;
} gallery_entry_t;

static const char *TAG = "gallery";
static gallery_config_t s_config;
static gallery_entry_t *s_entries = NULL;
static size_t s_entry_count = 0;
static size_t s_current = 0;
static size_t s_refresh_cursor = 0;
static size_t s_pending_thumbs = 0;
static QueueHandle_t s_cmd_queue = NULL;
static TaskHandle_t s_task_handle = NULL;
static esp_timer_handle_t s_slideshow_timer = NULL;
static bool s_running = false;
static bool s_slideshow_enabled = false;

static bool has_jpg_extension(const char *name)
{
    const char *ext = strrchr(name, '.');
    if (!ext) {
        return false;
    }
    ext++;
    return strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0;
}

static void gallery_reset_entries(void);
static void gallery_verify_empty_state(const char *context);

static esp_err_t gallery_scan_directory(const char *path)
{
    DIR *dir = opendir(path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open %s", path);
        return ESP_FAIL;
    }
    struct dirent *entry;
    size_t capacity = 32;
    s_entries = calloc(capacity, sizeof(gallery_entry_t));
    if (!s_entries) {
        closedir(dir);
        return ESP_ERR_NO_MEM;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) {
            continue;
        }
        if (!has_jpg_extension(entry->d_name)) {
            continue;
        }
        if (s_entry_count >= capacity) {
            capacity *= 2;
            gallery_entry_t *tmp = realloc(s_entries, capacity * sizeof(gallery_entry_t));
            if (!tmp) {
                closedir(dir);
                gallery_reset_entries();
                gallery_verify_empty_state("directory scan realloc failure");
                return ESP_ERR_NO_MEM;
            }
            s_entries = tmp;
            memset(s_entries + s_entry_count, 0, (capacity - s_entry_count) * sizeof(gallery_entry_t));
        }
        gallery_entry_t *item = &s_entries[s_entry_count];
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        item->path = strdup(full_path);
        if (!item->path) {
            closedir(dir);
            gallery_reset_entries();
            gallery_verify_empty_state("directory scan strdup failure");
            return ESP_ERR_NO_MEM;
        }
        struct stat st = {0};
        if (stat(full_path, &st) == 0) {
            item->size = st.st_size;
        }
        s_entry_count++;
        if (s_entry_count >= APP_GALLERY_MAX_IMAGES) {
            break;
        }
    }
    closedir(dir);
    return s_entry_count > 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static void gallery_event_emit(gallery_event_id_t id, size_t index, jpeg_image_t *image, esp_err_t status, const char *message)
{
    if (!s_config.event_cb) {
        return;
    }
    gallery_event_t evt = {
        .id = id,
        .index = index,
        .status = status,
        .message = message,
    };
    if (image) {
        evt.image = *image;
    } else {
        memset(&evt.image, 0, sizeof(evt.image));
    }
    s_config.event_cb(&evt, s_config.event_ctx);
}

static size_t gallery_process_thumb_batch(const jpeg_decode_options_t *opts)
{
    if (!opts || !s_entries || s_entry_count == 0 || s_pending_thumbs == 0) {
        return 0;
    }
    size_t processed = 0;
    size_t scanned = 0;
    size_t start = s_refresh_cursor % s_entry_count;

    while (processed < GALLERY_THUMB_BATCH && scanned < s_entry_count && s_pending_thumbs > 0) {
        size_t idx = (start + scanned) % s_entry_count;
        gallery_entry_t *entry = &s_entries[idx];
        if (!entry->thumb_valid) {
            jpeg_image_t thumb;
            if (jpeg_decode_file(entry->path, opts, &thumb) == ESP_OK) {
                entry->thumb = thumb;
                entry->thumb_valid = true;
                if (s_pending_thumbs > 0) {
                    s_pending_thumbs--;
                }
                gallery_event_emit(GALLERY_EVENT_THUMBNAIL_READY, idx, &entry->thumb, ESP_OK, NULL);
                processed++;
            } else {
                gallery_event_emit(GALLERY_EVENT_ERROR, idx, NULL, ESP_FAIL, "thumbnail decode failed");
            }
        }
        scanned++;
        if (processed) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    s_refresh_cursor = (start + scanned) % (s_entry_count ? s_entry_count : 1);
    return processed;
}

static esp_err_t gallery_decode_at(size_t index, jpeg_decode_options_t *opts)
{
    if (index >= s_entry_count) {
        return ESP_ERR_INVALID_ARG;
    }
    jpeg_image_t img;
    esp_err_t err = jpeg_decode_file(s_entries[index].path, opts, &img);
    if (err == ESP_OK) {
        s_current = index;
        gallery_event_emit(GALLERY_EVENT_IMAGE_READY, index, &img, ESP_OK, NULL);
        // ownership of img pixels transferred to callback
    } else {
        gallery_event_emit(GALLERY_EVENT_ERROR, index, NULL, err, "decode failed");
    }
    return err;
}

static void gallery_task(void *arg)
{
    jpeg_decode_options_t full_opts = {
        .max_width = APP_LCD_H_RES,
        .max_height = APP_LCD_V_RES,
        .reduce_to_fit = true,
        .use_psram = true,
    };
    jpeg_decode_options_t thumb_opts = {
        .max_width = s_config.thumb_long_side,
        .max_height = s_config.thumb_short_side,
        .reduce_to_fit = true,
        .use_psram = true,
    };
    gallery_cmd_t cmd;
    while (s_running) {
        if (xQueueReceive(s_cmd_queue, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        switch (cmd.id) {
        case GALLERY_CMD_LOAD_INDEX:
            gallery_decode_at(cmd.index, &full_opts);
            break;
        case GALLERY_CMD_NEXT:
            if (s_entry_count) {
                size_t next = (s_current + 1) % s_entry_count;
                gallery_decode_at(next, &full_opts);
            }
            break;
        case GALLERY_CMD_PREV:
            if (s_entry_count) {
                size_t prev = (s_current + s_entry_count - 1) % s_entry_count;
                gallery_decode_at(prev, &full_opts);
            }
            break;
        case GALLERY_CMD_REFRESH:
            if (gallery_process_thumb_batch(&thumb_opts) > 0 && s_pending_thumbs > 0) {
                gallery_cmd_t more = {.id = GALLERY_CMD_REFRESH};
                xQueueSendToBack(s_cmd_queue, &more, 0);
            }
            break;
        case GALLERY_CMD_STOP:
            s_running = false;
            break;
        default:
            break;
        }
    }
    vTaskDelete(NULL);
}

static void slideshow_timer_cb(void *arg)
{
    gallery_next();
}

esp_err_t gallery_start(const gallery_config_t *config)
{
    if (s_running) {
        return ESP_OK;
    }
    if (!config || !config->root_path) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(&s_config, 0, sizeof(s_config));
    s_config = *config;
    if (!s_config.thumb_long_side) {
        s_config.thumb_long_side = APP_GALLERY_THUMBNAIL_LONG_SIDE;
    }
    if (!s_config.thumb_short_side) {
        s_config.thumb_short_side = APP_GALLERY_THUMBNAIL_SHORT_SIDE;
    }
    s_entry_count = 0;
    s_current = 0;
    s_refresh_cursor = 0;
    s_pending_thumbs = 0;
    s_slideshow_enabled = false;

    esp_err_t err = gallery_scan_directory(s_config.root_path);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No images found in %s", s_config.root_path);
        gallery_reset_entries();
        gallery_verify_empty_state("directory scan failure");
        return err;
    }

    s_pending_thumbs = s_entry_count;

    s_cmd_queue = xQueueCreate(GALLERY_QUEUE_DEPTH, sizeof(gallery_cmd_t));
    if (!s_cmd_queue) {
        gallery_reset_entries();
        gallery_verify_empty_state("queue create failure");
        return ESP_ERR_NO_MEM;
    }
    s_running = true;
    if (xTaskCreate(gallery_task, "gallery", 8192, NULL, 5, &s_task_handle) != pdPASS) {
        s_running = false;
        vQueueDelete(s_cmd_queue);
        s_cmd_queue = NULL;
        gallery_reset_entries();
        gallery_verify_empty_state("task create failure");
        return ESP_ERR_NO_MEM;
    }

    if (s_config.slideshow_interval_ms) {
        const esp_timer_create_args_t args = {
            .callback = slideshow_timer_cb,
            .dispatch_method = ESP_TIMER_TASK,
            .arg = NULL,
            .name = "gallery_ss"
        };
        esp_err_t timer_err = esp_timer_create(&args, &s_slideshow_timer);
        if (timer_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create slideshow timer (%s)", esp_err_to_name(timer_err));
        } else {
            uint64_t period_us = (uint64_t)s_config.slideshow_interval_ms * 1000ULL;
            timer_err = esp_timer_start_periodic(s_slideshow_timer, period_us);
            if (timer_err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start slideshow timer (%s)", esp_err_to_name(timer_err));
            } else {
                s_slideshow_enabled = true;
            }
        }
    }

    return ESP_OK;
}

static void gallery_reset_entries(void)
{
    if (s_entries) {
        for (size_t i = 0; i < s_entry_count; ++i) {
            free(s_entries[i].path);
            s_entries[i].path = NULL;
            if (s_entries[i].thumb_valid) {
                jpeg_image_release(&s_entries[i].thumb);
                s_entries[i].thumb_valid = false;
            }
        }
        free(s_entries);
        s_entries = NULL;
    }

    s_entry_count = 0;
    s_current = 0;
    s_refresh_cursor = 0;
    s_pending_thumbs = 0;
}

static void gallery_verify_empty_state(const char *context)
{
    size_t count = gallery_image_count();
    const char *path0 = gallery_image_path(0);

    if (count != 0 || path0 != NULL) {
        ESP_LOGE(TAG, "Gallery state inconsistent after %s (count=%zu, path0=%s)",
                 context ? context : "unknown", count, path0 ? path0 : "<non-null>");
        assert(count == 0);
        assert(path0 == NULL);
    } else if (context) {
        ESP_LOGD(TAG, "Gallery state cleared after %s", context);
    }
}

void gallery_stop(void)
{
    if (!s_running) {
        return;
    }
    gallery_cmd_t cmd = {.id = GALLERY_CMD_STOP};
    xQueueSend(s_cmd_queue, &cmd, portMAX_DELAY);
    if (s_slideshow_timer) {
        esp_timer_stop(s_slideshow_timer);
        esp_timer_delete(s_slideshow_timer);
        s_slideshow_timer = NULL;
    }
    if (s_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(20));
        s_task_handle = NULL;
    }
    if (s_cmd_queue) {
        vQueueDelete(s_cmd_queue);
        s_cmd_queue = NULL;
    }
    gallery_reset_entries();
    s_running = false;
    s_slideshow_enabled = false;
}

esp_err_t gallery_next(void)
{
    if (!s_running || !s_cmd_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    gallery_cmd_t cmd = {.id = GALLERY_CMD_NEXT};
    return xQueueSend(s_cmd_queue, &cmd, 0) == pdTRUE ? ESP_OK : ESP_FAIL;
}

esp_err_t gallery_prev(void)
{
    if (!s_running || !s_cmd_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    gallery_cmd_t cmd = {.id = GALLERY_CMD_PREV};
    return xQueueSend(s_cmd_queue, &cmd, 0) == pdTRUE ? ESP_OK : ESP_FAIL;
}

esp_err_t gallery_goto(size_t index)
{
    if (!s_running || !s_cmd_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    if (index >= s_entry_count) {
        return ESP_ERR_INVALID_ARG;
    }
    gallery_cmd_t cmd = {.id = GALLERY_CMD_LOAD_INDEX, .index = index};
    return xQueueSend(s_cmd_queue, &cmd, 0) == pdTRUE ? ESP_OK : ESP_FAIL;
}

esp_err_t gallery_refresh_thumbnails(void)
{
    if (!s_running || !s_cmd_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_entry_count == 0) {
        return ESP_OK;
    }
    size_t invalid = 0;
    for (size_t i = 0; i < s_entry_count; ++i) {
        if (!s_entries[i].thumb_valid) {
            ++invalid;
        }
    }
    s_pending_thumbs = invalid;
    if (!invalid) {
        return ESP_OK;
    }
    s_refresh_cursor = s_current % s_entry_count;
    gallery_cmd_t cmd = {.id = GALLERY_CMD_REFRESH};
    return xQueueSend(s_cmd_queue, &cmd, 0) == pdTRUE ? ESP_OK : ESP_FAIL;
}

esp_err_t gallery_set_slideshow_enabled(bool enabled)
{
    if (!s_running) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_slideshow_timer) {
        s_slideshow_enabled = false;
        return enabled ? ESP_ERR_INVALID_STATE : ESP_OK;
    }

    esp_err_t err = ESP_OK;
    bool active = esp_timer_is_active(s_slideshow_timer);

    if (enabled) {
        if (!active) {
            uint64_t period_us = (uint64_t)s_config.slideshow_interval_ms * 1000ULL;
            err = esp_timer_start_periodic(s_slideshow_timer, period_us);
            if (err == ESP_ERR_INVALID_STATE) {
                err = ESP_OK;
            }
        }
        if (err == ESP_OK) {
            s_slideshow_enabled = true;
        }
    } else {
        if (active) {
            err = esp_timer_stop(s_slideshow_timer);
            if (err == ESP_ERR_INVALID_STATE) {
                err = ESP_OK;
            }
        }
        if (err == ESP_OK) {
            s_slideshow_enabled = false;
        }
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to %s slideshow (%s)", enabled ? "enable" : "disable", esp_err_to_name(err));
    }

    return err;
}

bool gallery_is_slideshow_enabled(void)
{
    return s_slideshow_enabled;
}

size_t gallery_image_count(void)
{
    return s_entry_count;
}

size_t gallery_current_index(void)
{
    return s_current;
}

const char *gallery_image_path(size_t index)
{
    if (index >= s_entry_count || !s_entries) {
        return NULL;
    }
    return s_entries[index].path;
}

void gallery_release_image(jpeg_image_t *image)
{
    jpeg_image_release(image);
}
