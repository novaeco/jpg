#include "jpeg_decoder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "tjpgd.h"

#define WORKBUF_SIZE 4096

typedef struct {
    FILE *file;
    uint8_t workbuf[WORKBUF_SIZE];
    jpeg_image_t *image;
} jpeg_decoder_ctx_t;

static size_t tj_input(JDEC *jd, uint8_t *buf, size_t len)
{
    jpeg_decoder_ctx_t *ctx = (jpeg_decoder_ctx_t *)jd->device;
    if (buf) {
        return fread(buf, 1, len, ctx->file);
    }
    fseek(ctx->file, len, SEEK_CUR);
    return len;
}

static uint32_t tj_output(JDEC *jd, void *bitmap, JRECT *rect)
{
    jpeg_decoder_ctx_t *ctx = (jpeg_decoder_ctx_t *)jd->device;
    jpeg_image_t *img = ctx->image;
    uint16_t *src = (uint16_t *)bitmap;
    for (int y = rect->top; y <= rect->bottom; ++y) {
        uint16_t *dst = (uint16_t *)(img->pixels + y * img->stride * sizeof(uint16_t)) + rect->left;
        memcpy(dst, src, (rect->right - rect->left + 1) * sizeof(uint16_t));
        src += (rect->right - rect->left + 1);
    }
    return 1;
}

static void default_options(jpeg_decode_options_t *opts)
{
    opts->max_width = 0;
    opts->max_height = 0;
    opts->reduce_to_fit = true;
    opts->use_psram = true;
}

esp_err_t jpeg_decode_file(const char *path, const jpeg_decode_options_t *options, jpeg_image_t *out_image)
{
    if (!path || !out_image) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_image, 0, sizeof(*out_image));

    jpeg_decode_options_t opts;
    if (options) {
        opts = *options;
    } else {
        default_options(&opts);
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        ESP_LOGE("jpeg", "Failed to open %s", path);
        return ESP_FAIL;
    }

    jpeg_decoder_ctx_t ctx = {
        .file = fp,
        .image = out_image,
    };
    JDEC decoder;
    JRESULT res = jd_prepare(&decoder, tj_input, ctx.workbuf, sizeof(ctx.workbuf), &ctx);
    if (res != JDR_OK) {
        ESP_LOGE("jpeg", "jd_prepare failed %d", res);
        fclose(fp);
        return ESP_FAIL;
    }

    uint16_t out_width = decoder.width;
    uint16_t out_height = decoder.height;

    uint8_t scale = 0;
    if (opts.reduce_to_fit && (opts.max_width || opts.max_height)) {
        while (((out_width >> scale) > opts.max_width && opts.max_width) ||
               ((out_height >> scale) > opts.max_height && opts.max_height)) {
            if (scale < 3) {
                scale++;
            } else {
                break;
            }
        }
    }
    decoder.scale = scale;
    out_width = decoder.width >> scale;
    out_height = decoder.height >> scale;

    size_t stride = out_width;
    size_t buffer_size = stride * out_height * sizeof(uint16_t);
    uint32_t caps = opts.use_psram ? (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : MALLOC_CAP_8BIT;
    uint8_t *buffer = heap_caps_malloc(buffer_size, caps);
    if (!buffer) {
        ESP_LOGE("jpeg", "Failed to allocate %u bytes", (unsigned)buffer_size);
        fclose(fp);
        return ESP_ERR_NO_MEM;
    }

    out_image->pixels = buffer;
    out_image->width = out_width;
    out_image->height = out_height;
    out_image->stride = stride;
    out_image->buffer_size = buffer_size;

    res = jd_decomp(&decoder, tj_output, scale);
    fclose(fp);
    if (res != JDR_OK) {
        ESP_LOGE("jpeg", "jd_decomp failed %d", res);
        jpeg_image_release(out_image);
        return ESP_FAIL;
    }
    return ESP_OK;
}

void jpeg_image_release(jpeg_image_t *image)
{
    if (!image) {
        return;
    }
    free(image->pixels);
    memset(image, 0, sizeof(*image));
}
