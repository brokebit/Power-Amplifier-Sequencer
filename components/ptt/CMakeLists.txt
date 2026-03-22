idf_component_register(
    SRCS "ptt.c"
    INCLUDE_DIRS "include"
    REQUIRES driver hw_config sequencer system_state
)
