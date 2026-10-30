idf_component_register(SRCS "app_main.c" "sensor.c" "relay.c" "wifi_connection.c"
                            "mqtt_connection.c" "mqtt_command.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_adc esp_driver_gpio esp_common freertos log
                                     esp_wifi esp_netif esp_event nvs_flash esp_timer
                                     espressif__mqtt espressif__cjson)
