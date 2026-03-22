idf_component_register(
    SRCS "sequencer.c"
    INCLUDE_DIRS "include"
    REQUIRES config relays system_state esp_timer
)
