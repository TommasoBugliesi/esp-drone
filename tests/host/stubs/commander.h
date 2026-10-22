#pragma once
typedef struct { float x; } setpoint_t;
#define COMMANDER_PRIORITY_CRTP 1
void commanderSetSetpoint(setpoint_t *setpoint, int priority);
