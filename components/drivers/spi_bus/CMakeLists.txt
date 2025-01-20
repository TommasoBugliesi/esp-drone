idf_component_register(SRCS "spi_drv.c" "spidev_esp32.c"
                       INCLUDE_DIRS "include"
                       REQUIRES crazyflie platform driver
                       PRIV_REQUIRES config)