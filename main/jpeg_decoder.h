#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t stride;
    size_t buffer_size;
    uint8_t *pixels;
} jpeg_image_t;

typedef struct {
    uint16_t max_width;
    uint16_t max_height;
    bool reduce_to_fit;
    bool use_psram;
} jpeg_decode_options_t;

esp_err_t jpeg_decode_file(const char *path, const jpeg_decode_options_t *options, jpeg_image_t *out_image);
void jpeg_image_release(jpeg_image_t *image);

#ifdef __cplusplus
}
#endif
