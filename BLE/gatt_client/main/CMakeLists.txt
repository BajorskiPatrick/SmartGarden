idf_component_register(SRCS "gattc_demo.c"
                    REQUIRES bt
                    PRIV_REQUIRES bt nvs_flash
                    INCLUDE_DIRS ".")
