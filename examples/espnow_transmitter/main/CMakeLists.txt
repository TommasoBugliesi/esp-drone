idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "." "../../../components/core/crazyflie/hal/interface"
                       REQUIRES esp_wifi nvs_flash esp_event esp_netif)
