idf_component_register(SRCS "gatt_client.c"
                    REQUIRES bt
                    PRIV_REQUIRES bt nvs_flash
                    INCLUDE_DIRS ".")
