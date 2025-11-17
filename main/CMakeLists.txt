idf_component_register(SRCS "station_main.c" "http_client.c"
                    PRIV_REQUIRES esp_wifi nvs_flash driver
                    INCLUDE_DIRS ".")
