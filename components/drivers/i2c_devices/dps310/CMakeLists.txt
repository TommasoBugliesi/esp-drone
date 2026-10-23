idf_component_register(SRCS "dps310.c"
                       INCLUDE_DIRS "include"
                       REQUIRES i2c_bus
                       PRIV_REQUIRES crazyflie platform)
