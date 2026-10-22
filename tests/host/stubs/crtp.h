#pragma once
typedef struct { uint8_t size; union { struct { uint8_t header; uint8_t data[30]; }; uint8_t raw[31]; }; } __attribute__((packed)) CRTPPacket;
#define CRTP_HEADER(port, channel) (((port & 0x0F) << 4) | (channel & 0x0F))
#define CRTP_PORT_SETPOINT 3
