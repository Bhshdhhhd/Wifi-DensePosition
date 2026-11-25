idf_component_register(
    SRCS
        "main.c"
        "presence_detector.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_wifi
        esp_event
        esp_netif
        esp_timer
        nvs_flash
        lwip
)
