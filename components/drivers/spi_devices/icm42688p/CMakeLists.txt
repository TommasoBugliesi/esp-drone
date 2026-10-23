idf_component_register(SRCS "icm42688p.c"
                       INCLUDE_DIRS "include"
                       REQUIRES spi_bus
                       PRIV_REQUIRES crazyflie)
