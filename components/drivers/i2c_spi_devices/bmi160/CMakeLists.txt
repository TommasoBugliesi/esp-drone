idf_component_register(SRCS "bmi160.c"
                       INCLUDE_DIRS "include"
                       REQUIRES spi_bus
                       PRIV_REQUIRES crazyflie platform config)