set(srcs "ledc_driver.c")

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_driver_ledc)
