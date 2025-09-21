idf_component_register(
    SRCS
        "main.c"
        "app_config.c"
        "ch422_driver.c"
        "display_driver.c"
        "sdspi_ch422_host.c"
        "sd_card.c"
        "jpeg_decoder.c"
        "gallery.c"
        "ui.c"
        "comm_can.c"
        "comm_rs485.c"
        "comm_usb.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        driver
        esp_event
        esp_lcd
        esp_lvgl_port
        lvgl
        esp_lcd_touch
        esp_lcd_touch_gt911
        fatfs
        sdmmc
        esp_timer
        esp_pm
        esp_psram
        nvs_flash
        esp_tinyusb
        usb
        esp_system
        esp_netif
        tjpgd
)

# Workaround for ESP-IDF v5.5 LVGL component missing the FATFS dependency
# declaration. Without this adjustment, lv_fs_fatfs.c fails to compile because
# the ff.h header (exposed via the FATFS component interface) is not reachable
# during compilation of the LVGL sources.  Explicitly stitch the dependency and
# propagate the include directories here until the upstream definition is
# corrected.
idf_component_get_property(lvgl_lib lvgl COMPONENT_LIB)
idf_component_get_property(fatfs_lib fatfs COMPONENT_LIB)
if(TARGET ${lvgl_lib} AND TARGET ${fatfs_lib})
    target_link_libraries(${lvgl_lib} PUBLIC ${fatfs_lib})
    target_include_directories(
        ${lvgl_lib}
        PUBLIC
        $<TARGET_PROPERTY:${fatfs_lib},INTERFACE_INCLUDE_DIRECTORIES>
    )
    target_compile_definitions(
        ${lvgl_lib}
        PUBLIC
            LV_USE_FS_FATFS=1
            LV_FS_FATFS_LETTER='0'
            LV_FS_FATFS_PATH="/sdcard"
    )
endif()
