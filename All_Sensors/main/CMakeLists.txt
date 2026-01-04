idf_component_register(SRCS "app_main.c"
                    PRIV_REQUIRES mqtt nvs_flash esp_netif json driver veml7700 esp_adc
                    INCLUDE_DIRS ".")
