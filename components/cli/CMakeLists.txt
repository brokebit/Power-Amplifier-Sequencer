idf_component_register(
    SRCS "cli.c"
         "cmd_status.c"
         "cmd_system.c"
         "cmd_log.c"
         "cmd_config.c"
         "cmd_relay.c"
         "cmd_fault.c"
         "cmd_seq.c"
         "cmd_adc.c"
         "cmd_monitor.c"
         "cmd_wifi.c"
    INCLUDE_DIRS "include"
    REQUIRES console
             config
             sequencer
             monitor
             ads1115
             system_state
             hw_config
             relays
             wifi_sta
    PRIV_REQUIRES esp_app_format
                  driver
)
