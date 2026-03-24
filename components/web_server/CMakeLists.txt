idf_component_register(
    SRCS "web_server.c"
         "web_json.c"
         "web_ws.c"
         "api_state.c"
         "api_config.c"
         "api_relay.c"
         "api_fault.c"
         "api_seq.c"
         "api_adc.c"
         "api_wifi.c"
         "api_ota.c"
         "api_system.c"
         "api_static.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server
             spiffs
             json
             config
             system_state
             sequencer
             monitor
             ads1115
             relays
             wifi_sta
             ota
             hw_config
    PRIV_REQUIRES esp_app_format
                  esp_wifi
)
