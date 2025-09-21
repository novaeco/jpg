#include "ui.h"
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "gallery.h"
#include "app_config.h"

typedef struct {
    display_driver_handles_t handles;
    lv_obj_t *home_screen;
    lv_obj_t *gallery_screen;
    lv_obj_t *viewer_screen;
    lv_obj_t *settings_screen;
    lv_obj_t *gallery_container;
    lv_obj_t *viewer_image;
    lv_obj_t *brightness_slider;
    lv_obj_t *slideshow_switch;
    lv_obj_t **thumbnail_imgs;
    lv_image_dsc_t *thumbnail_dscs;
    size_t thumb_count;
    jpeg_image_t current_image;
    lv_image_dsc_t current_image_dsc;
    uint16_t zoom_factor;
    uint16_t rotation;
    bool slideshow_toggle_guard;
} ui_context_t;

static const char *TAG = "ui";
static ui_context_t s_ui = {0};

static void ui_show_screen(lv_obj_t *screen)
{
    if (!screen) {
        return;
    }
    lv_screen_load(screen);
}

static void on_home_gallery(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_show_screen(s_ui.gallery_screen);
}

static void on_viewer_home(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_show_screen(s_ui.home_screen);
}

static void on_viewer_zoom(lv_event_t *e)
{
    LV_UNUSED(e);
    s_ui.zoom_factor = (s_ui.zoom_factor == 256) ? 512 : 256;
    lv_image_set_scale_x(s_ui.viewer_image, s_ui.zoom_factor);
    lv_image_set_scale_y(s_ui.viewer_image, s_ui.zoom_factor);
}

static void on_viewer_rotate(lv_event_t *e)
{
    LV_UNUSED(e);
    s_ui.rotation = (s_ui.rotation + 900) % 3600;
    lv_image_set_rotation(s_ui.viewer_image, s_ui.rotation);
}

static void on_viewer_gesture(lv_event_t *e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_LEFT) {
        gallery_next();
    } else if (dir == LV_DIR_RIGHT) {
        gallery_prev();
    }
}

static void on_gallery_thumb(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e);
    size_t index = (size_t)lv_event_get_user_data(e);
    lv_obj_t *img = s_ui.thumbnail_imgs[index];
    lv_obj_add_state(target, LV_STATE_DISABLED);
    gallery_goto(index);
    for (size_t i = 0; i < s_ui.thumb_count; ++i) {
        if (s_ui.thumbnail_imgs[i] == img) {
            continue;
        }
        lv_obj_clear_state(lv_obj_get_parent(s_ui.thumbnail_imgs[i]), LV_STATE_DISABLED);
    }
    LV_UNUSED(img);
}

static void on_brightness_changed(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t value = lv_slider_get_value(slider);
    display_driver_set_backlight_percent(value);
}

static void on_slideshow_toggle(lv_event_t *e)
{
    if (s_ui.slideshow_toggle_guard) {
        return;
    }
    lv_obj_t *sw = lv_event_get_target(e);
    bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    esp_err_t err = gallery_set_slideshow_enabled(enabled);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to %s slideshow (%s)", enabled ? "enable" : "disable", esp_err_to_name(err));
        s_ui.slideshow_toggle_guard = true;
        if (enabled) {
            lv_obj_clear_state(sw, LV_STATE_CHECKED);
        } else {
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        }
        s_ui.slideshow_toggle_guard = false;
    } else if (enabled) {
        gallery_refresh_thumbnails();
    }
}

static void create_home_screen(void)
{
    s_ui.home_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_ui.home_screen, lv_palette_lighten(LV_PALETTE_LIGHT_BLUE, 4), 0);
    lv_obj_clear_flag(s_ui.home_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn = lv_button_create(s_ui.home_screen);
    lv_obj_set_size(btn, 220, 80);
    lv_obj_center(btn);
    lv_obj_add_event_cb(btn, on_home_gallery, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, "Galerie");
    lv_obj_center(label);
}

static void create_gallery_screen(void)
{
    s_ui.gallery_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(s_ui.gallery_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_ui.gallery_screen, lv_color_hex(0xf3f5f7), 0);

    s_ui.gallery_container = lv_obj_create(s_ui.gallery_screen);
    lv_obj_set_size(s_ui.gallery_container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(s_ui.gallery_container, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_ui.gallery_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(s_ui.gallery_container, 16, 0);
    lv_obj_set_style_pad_row(s_ui.gallery_container, 16, 0);
    lv_obj_set_style_pad_column(s_ui.gallery_container, 16, 0);

}

static void ui_rebuild_gallery_items(void)
{
    if (!s_ui.gallery_container) {
        return;
    }

    lv_obj_clean(s_ui.gallery_container);
    free(s_ui.thumbnail_imgs);
    free(s_ui.thumbnail_dscs);
    s_ui.thumbnail_imgs = NULL;
    s_ui.thumbnail_dscs = NULL;
    s_ui.thumb_count = 0;

    size_t count = gallery_image_count();
    if (count == 0) {
        return;
    }

    s_ui.thumbnail_imgs = calloc(count, sizeof(lv_obj_t *));
    s_ui.thumbnail_dscs = calloc(count, sizeof(lv_image_dsc_t));
    if (!s_ui.thumbnail_imgs || !s_ui.thumbnail_dscs) {
        ESP_LOGE(TAG, "Failed to allocate thumbnail descriptors");
        free(s_ui.thumbnail_imgs);
        free(s_ui.thumbnail_dscs);
        s_ui.thumbnail_imgs = NULL;
        s_ui.thumbnail_dscs = NULL;
        return;
    }

    for (size_t i = 0; i < count; ++i) {
        lv_obj_t *btn = lv_button_create(s_ui.gallery_container);
        lv_obj_set_size(btn, 220, 160);
        lv_obj_add_event_cb(btn, on_gallery_thumb, LV_EVENT_CLICKED, (void *)i);

        lv_obj_t *img = lv_image_create(btn);
        lv_obj_center(img);
        lv_image_set_src(img, NULL);
        s_ui.thumbnail_imgs[i] = img;
        memset(&s_ui.thumbnail_dscs[i], 0, sizeof(lv_image_dsc_t));

        lv_obj_t *name = lv_label_create(btn);
        const char *path = gallery_image_path(i);
        const char *fname = path ? strrchr(path, '/') : NULL;
        fname = fname ? fname + 1 : path;
        lv_label_set_text_fmt(name, "%s", fname ? fname : "");
        lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -4);
    }

    s_ui.thumb_count = count;
}

static void create_viewer_screen(void)
{
    s_ui.viewer_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(s_ui.viewer_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_ui.viewer_screen, lv_color_black(), 0);
    lv_obj_add_event_cb(s_ui.viewer_screen, on_viewer_gesture, LV_EVENT_GESTURE, NULL);

    s_ui.viewer_image = lv_image_create(s_ui.viewer_screen);
    lv_obj_center(s_ui.viewer_image);
    s_ui.zoom_factor = 256;
    s_ui.rotation = 0;

    lv_obj_t *overlay = lv_obj_create(s_ui.viewer_screen);
    lv_obj_set_size(overlay, LV_PCT(100), 64);
    lv_obj_set_style_bg_color(overlay, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_40, 0);
    lv_obj_set_flex_flow(overlay, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(overlay, 8, 0);
    lv_obj_align(overlay, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *btn_home = lv_button_create(overlay);
    lv_label_set_text(lv_label_create(btn_home), LV_SYMBOL_HOME);
    lv_obj_add_event_cb(btn_home, on_viewer_home, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_zoom = lv_button_create(overlay);
    lv_label_set_text(lv_label_create(btn_zoom), LV_SYMBOL_EYE_OPEN);
    lv_obj_add_event_cb(btn_zoom, on_viewer_zoom, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_rotate = lv_button_create(overlay);
    lv_label_set_text(lv_label_create(btn_rotate), LV_SYMBOL_REFRESH);
    lv_obj_add_event_cb(btn_rotate, on_viewer_rotate, LV_EVENT_CLICKED, NULL);
}

static void create_settings_screen(void)
{
    s_ui.settings_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(s_ui.settings_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_ui.settings_screen, lv_palette_lighten(LV_PALETTE_GREY, 3), 0);

    lv_obj_t *column = lv_obj_create(s_ui.settings_screen);
    lv_obj_set_size(column, LV_PCT(90), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(column, 24, 0);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(column, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_align(column, LV_ALIGN_TOP_MID, 0, 32);

    lv_obj_t *brightness_label = lv_label_create(column);
    lv_label_set_text(brightness_label, "Luminosité");

    s_ui.brightness_slider = lv_slider_create(column);
    lv_slider_set_range(s_ui.brightness_slider, 0, 100);
    lv_slider_set_value(s_ui.brightness_slider, display_driver_get_backlight_percent(), LV_ANIM_OFF);
    lv_obj_set_width(s_ui.brightness_slider, LV_PCT(100));
    lv_obj_add_event_cb(s_ui.brightness_slider, on_brightness_changed, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *ss_label = lv_label_create(column);
    lv_label_set_text(ss_label, "Lecture automatique");

    s_ui.slideshow_switch = lv_switch_create(column);
    lv_obj_add_event_cb(s_ui.slideshow_switch, on_slideshow_toggle, LV_EVENT_VALUE_CHANGED, NULL);

    bool slideshow_enabled = gallery_is_slideshow_enabled();
    s_ui.slideshow_toggle_guard = true;
    if (slideshow_enabled) {
        lv_obj_add_state(s_ui.slideshow_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(s_ui.slideshow_switch, LV_STATE_CHECKED);
    }
    s_ui.slideshow_toggle_guard = false;
}

esp_err_t ui_init(const display_driver_handles_t *display)
{
    if (!display) {
        return ESP_ERR_INVALID_ARG;
    }
    ui_deinit();
    memset(&s_ui, 0, sizeof(s_ui));
    s_ui.handles = *display;

    ESP_RETURN_ON_ERROR(display_driver_lock_lvgl(0), TAG, "LVGL lock failed");
    create_home_screen();
    create_gallery_screen();
    create_viewer_screen();
    create_settings_screen();
    ui_rebuild_gallery_items();
    ui_show_screen(s_ui.home_screen);
    display_driver_unlock_lvgl();
    return ESP_OK;
}

void ui_deinit(void)
{
    if (display_driver_lock_lvgl(100) == ESP_OK) {
        if (s_ui.viewer_image) {
            lv_image_set_src(s_ui.viewer_image, NULL);
        }
        if (s_ui.thumbnail_imgs) {
            for (size_t i = 0; i < s_ui.thumb_count; ++i) {
                if (s_ui.thumbnail_imgs[i]) {
                    lv_image_set_src(s_ui.thumbnail_imgs[i], NULL);
                }
            }
        }
        if (s_ui.home_screen) {
            lv_obj_del(s_ui.home_screen);
        }
        if (s_ui.gallery_screen) {
            lv_obj_del(s_ui.gallery_screen);
        }
        if (s_ui.viewer_screen) {
            lv_obj_del(s_ui.viewer_screen);
        }
        if (s_ui.settings_screen) {
            lv_obj_del(s_ui.settings_screen);
        }
        display_driver_unlock_lvgl();
    }

    if (s_ui.current_image.pixels) {
        gallery_release_image(&s_ui.current_image);
        memset(&s_ui.current_image_dsc, 0, sizeof(s_ui.current_image_dsc));
    }
    free(s_ui.thumbnail_imgs);
    free(s_ui.thumbnail_dscs);
    s_ui.thumbnail_imgs = NULL;
    s_ui.thumbnail_dscs = NULL;
    s_ui.thumb_count = 0;
    memset(&s_ui, 0, sizeof(s_ui));
}

void ui_update_gallery_items(void)
{
    if (display_driver_lock_lvgl(100) != ESP_OK) {
        return;
    }
    ui_rebuild_gallery_items();
    display_driver_unlock_lvgl();
}

void ui_handle_gallery_event(const gallery_event_t *event)
{
    if (!event) {
        return;
    }
    if (display_driver_lock_lvgl(50) != ESP_OK) {
        return;
    }
    switch (event->id) {
    case GALLERY_EVENT_IMAGE_READY:
        if (s_ui.current_image.pixels) {
            gallery_release_image(&s_ui.current_image);
            memset(&s_ui.current_image_dsc, 0, sizeof(s_ui.current_image_dsc));
        }
        s_ui.current_image = event->image;
        s_ui.current_image_dsc = (lv_image_dsc_t) {
            .header = {
                .always_zero = 0,
                .w = s_ui.current_image.width,
                .h = s_ui.current_image.height,
                .cf = LV_COLOR_FORMAT_RGB565,
            },
            .data = s_ui.current_image.pixels,
            .data_size = s_ui.current_image.buffer_size,
        };
        lv_image_set_src(s_ui.viewer_image, &s_ui.current_image_dsc);
        ui_show_screen(s_ui.viewer_screen);
        break;
    case GALLERY_EVENT_THUMBNAIL_READY:
        if (event->index < s_ui.thumb_count && s_ui.thumbnail_imgs && s_ui.thumbnail_dscs) {
            lv_image_dsc_t *dsc = &s_ui.thumbnail_dscs[event->index];
            *dsc = (lv_image_dsc_t) {
                .header = {
                    .always_zero = 0,
                    .w = event->image.width,
                    .h = event->image.height,
                    .cf = LV_COLOR_FORMAT_RGB565,
                },
                .data = event->image.pixels,
                .data_size = event->image.buffer_size,
            };
            lv_image_set_src(s_ui.thumbnail_imgs[event->index], dsc);
        }
        break;
    case GALLERY_EVENT_ERROR:
        ESP_LOGE(TAG, "Gallery error index %d", (int)event->index);
        break;
    default:
        break;
    }
    display_driver_unlock_lvgl();
}

void ui_update_brightness_slider(uint8_t percent)
{
    if (!s_ui.brightness_slider) {
        return;
    }
    if (display_driver_lock_lvgl(10) == ESP_OK) {
        lv_slider_set_value(s_ui.brightness_slider, percent, LV_ANIM_OFF);
        display_driver_unlock_lvgl();
    }
}

void ui_set_slideshow_enabled(bool enabled)
{
    if (!s_ui.slideshow_switch) {
        return;
    }
    if (display_driver_lock_lvgl(10) == ESP_OK) {
        s_ui.slideshow_toggle_guard = true;
        if (enabled) {
            lv_obj_add_state(s_ui.slideshow_switch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(s_ui.slideshow_switch, LV_STATE_CHECKED);
        }
        s_ui.slideshow_toggle_guard = false;
        display_driver_unlock_lvgl();
    }
}
