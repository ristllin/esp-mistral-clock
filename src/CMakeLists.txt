idf_component_register(SRCS "main.c"
                            "display.c"
                            "console.c"
                            "console_cmds.c"
                            "fbdump.c"
                            "touch.c"
                            "ui_home.c"
                            "ui_settings.c"
                            "ui_loc.c"
                            "ui_common.c"
                            "scene.c"
                            "scene_chaton.c"
                            "saver.c"
                            "theme_mgr.c"
                            "wifi_mgr.c"
                            "time_mgr.c"
                            "weather_mgr.c"
                            "fonts/font_px8.c"
                            "fonts/font_px16.c"
                            "fonts/font_px24.c"
                            "fonts/font_silk8.c"
                    INCLUDE_DIRS "." "assets" "fonts"
                    REQUIRES esp_bsp esp_lcd_jd9853 esp_lcd_touch_axs5106
                             esp_lcd_touch esp_lvgl_port lvgl nvs_flash esp_driver_i2c
                             esp_driver_usb_serial_jtag console esp_system
                             esp_wifi esp_netif esp_event lwip esp_timer
                             esp_http_client mbedtls json)
