idf_component_register(SRCS "app_main.c" "sensors.c" "mqtt_app.c" "wifi_prov.c"
                    PRIV_REQUIRES mqtt nvs_flash esp_netif json driver veml7700 esp_adc bt esp_wifi esp_timer
                    INCLUDE_DIRS ".")
