/*
 * Serial protocol between a host (laptop, phone over USB OTG) and the ESP-NOW bridge.
 *
 * The serial stream carries the ESP-NOW link frames of espnow_link.h unchanged,
 * plus two frames that stay between host and bridge. Every frame starts with
 * ESPNOW_LINK_MAGIC and ends with a CRC-16/CCITT-FALSE (espnowLinkCrc16) over the
 * bytes before it; its length follows from its type. A receiver that loses sync
 * drops one byte and looks for the next magic.
 *
 *  host -> bridge   CONTROL (1, 16 bytes)        forwarded to the drone as is
 *                   BRIDGE_CONFIG (0x10, 14 bytes)
 *  bridge -> host   STATUS (2, 10 bytes)         from the drone
 *                   TELEMETRY (3, 32 bytes)      from the drone
 *                   BRIDGE_INFO (0x11, 28 bytes) twice a second
 *
 * All fields little endian, packed. Mirrored in tools/groundstation/espdrone_link.py.
 */
#ifndef BRIDGE_PROTOCOL_H_
#define BRIDGE_PROTOCOL_H_

#include <stdint.h>

#define BRIDGE_BAUDRATE              460800

#define BRIDGE_PKT_CONFIG            0x10
#define BRIDGE_PKT_INFO              0x11

/* BRIDGE_CONFIG flags */
#define BRIDGE_CONFIG_SAVE           (1 << 0)   // also store in NVS

/* BRIDGE_INFO flags */
#define BRIDGE_INFO_UNICAST          (1 << 0)   // a drone MAC is set (else broadcast)
#define BRIDGE_INFO_HOST_ACTIVE      (1 << 1)   // control frames arrive from the host
#define BRIDGE_INFO_DRONE_ACTIVE     (1 << 2)   // frames arrive from the drone

/* Host -> bridge: which drone, which channel */
typedef struct {
  uint8_t  magic;
  uint8_t  version;
  uint8_t  type;        // BRIDGE_PKT_CONFIG
  uint8_t  flags;       // BRIDGE_CONFIG_*
  uint8_t  droneMac[6]; // the drone's AP MAC, all zero for broadcast
  uint8_t  channel;     // the drone's CONFIG_WIFI_CHANNEL, 1-13
  uint8_t  reserved;
  uint16_t crc;
} __attribute__((packed)) bridgeConfig_t;

/* Bridge -> host */
typedef struct {
  uint8_t  magic;
  uint8_t  version;
  uint8_t  type;          // BRIDGE_PKT_INFO
  uint8_t  flags;         // BRIDGE_INFO_*
  uint8_t  staMac[6];     // the bridge's STA MAC: the drone's ESPNOW_CTRL_PEER_MAC
  uint8_t  droneMac[6];
  uint8_t  channel;
  uint8_t  reserved;
  uint16_t txOk;          // control frames sent
  uint16_t txFail;        // esp_now_send errors
  uint16_t hostTimeouts;  // gaps of more than BRIDGE_HOST_TIMEOUT_MS between host frames
  uint16_t rxFromDrone;   // valid frames received from the drone
  uint16_t crc;
} __attribute__((packed)) bridgeInfo_t;

#endif /* BRIDGE_PROTOCOL_H_ */
