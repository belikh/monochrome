idf_component_register(SRCS "main.c" "knot.c" "netfb.c" INCLUDE_DIRS "." REQUIRES esp_lcd esp_psram esp_driver_gpio esp_mm esp_timer esp_wifi esp_netif esp_event nvs_flash lwip)
