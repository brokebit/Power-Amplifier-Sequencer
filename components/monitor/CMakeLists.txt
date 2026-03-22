idf_component_register(
    SRCS "monitor.c"
    INCLUDE_DIRS "include"
    REQUIRES ads1115 config sequencer system_state driver hw_config freertos
)
