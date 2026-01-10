idf_component_register(SRCS "veml7700.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver esp-idf-lib__i2cdev)