idf_component_register(SRCS "display.cpp"
                       PRIV_REQUIRES esp_driver_i2c esp_netif esp_event nvs_flash lwip
                       INCLUDE_DIRS ".")
