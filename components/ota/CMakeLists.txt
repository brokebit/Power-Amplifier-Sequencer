idf_component_register(
    SRCS "ota.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_https_ota esp_http_client app_update nvs_flash wifi_sta esp_partition bootloader_support mbedtls spiffs web_server
)
