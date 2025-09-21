#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "jpeg_decoder.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GALLERY_EVENT_IMAGE_READY = 0,
    GALLERY_EVENT_THUMBNAIL_READY,
    GALLERY_EVENT_ERROR,
    GALLERY_EVENT_IDLE
} gallery_event_id_t;

typedef struct {
    gallery_event_id_t id;
    size_t index;
    jpeg_image_t image;
    esp_err_t status;
    const char *message;
} gallery_event_t;

typedef void (*gallery_event_cb_t)(const gallery_event_t *event, void *user_ctx);

typedef struct {
    const char *root_path;
    gallery_event_cb_t event_cb;
    void *event_ctx;
    uint32_t slideshow_interval_ms;
    uint16_t thumb_long_side;
    uint16_t thumb_short_side;
} gallery_config_t;

esp_err_t gallery_start(const gallery_config_t *config);
void gallery_stop(void);
esp_err_t gallery_next(void);
esp_err_t gallery_prev(void);
esp_err_t gallery_goto(size_t index);
esp_err_t gallery_refresh_thumbnails(void);
esp_err_t gallery_set_slideshow_enabled(bool enabled);
bool gallery_is_slideshow_enabled(void);
size_t gallery_image_count(void);
size_t gallery_current_index(void);
const char *gallery_image_path(size_t index);
void gallery_release_image(jpeg_image_t *image);

#ifdef __cplusplus
}
#endif
