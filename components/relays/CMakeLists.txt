idf_component_register(
    SRCS "relays.c"
    INCLUDE_DIRS "include"
    REQUIRES driver hw_config system_state
)
