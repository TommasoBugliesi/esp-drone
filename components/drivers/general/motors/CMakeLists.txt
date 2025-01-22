idf_component_register(SRCS "motors.c" "motors_brushed.c" "motors_brushless.c"
                       INCLUDE_DIRS "." "include"
                        REQUIRES crazyflie platform config driver)