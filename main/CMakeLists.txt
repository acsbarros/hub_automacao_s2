idf_component_register(SRCS "hub_automacao_s2.c"
                    INCLUDE_DIRS "."
                    REQUIRES nvs_flash espressif__esp_rainmaker rmaker_app_network esp_driver_gpio)
