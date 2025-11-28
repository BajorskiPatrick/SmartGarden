idf_component_register(SRCS "gatts_demo.c"
                    REQUIRES bt
                    PRIV_REQUIRES bt nvs_flash
                    INCLUDE_DIRS ".")