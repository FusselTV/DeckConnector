idf_component_register(SRCS db_dock.c INCLUDE_DIRS include
                       REQUIRES esp_common db_config
                       PRIV_REQUIRES db_core db_deck lwip)
