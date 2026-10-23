idf_component_register(SRCS "mmc5983ma.c"
                       INCLUDE_DIRS "include"
                       REQUIRES spi_bus
                       PRIV_REQUIRES crazyflie)
