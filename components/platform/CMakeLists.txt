idf_component_register(SRCS "platform_boards.c" "platform_esp32.c" "platform.c"
                       INCLUDE_DIRS "."
                       REQUIRES crazyflie motors config)