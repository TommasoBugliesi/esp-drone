/**
 * ESP-Drone Firmware
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, in version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * espnow_link.h - ESP-NOW control link between a transmitter and the drone
 *
 * This header is self-contained (only stdint) so the transmitter firmware can
 * include it unchanged to share the wire format.
 *
 * Radio setup
 *  - ESP-NOW rides on the Wi-Fi channel of the drone's soft AP
 *    (CONFIG_WIFI_CHANNEL). The transmitter must use the same channel.
 *  - The transmitter sends CONTROL frames to the drone's AP MAC (the six bytes
 *    at the end of the SSID, ESP-DRONE_<MAC>) or to the broadcast address.
 *    Encryption (CONFIG_ESPNOW_CTRL_ENCRYPT) requires unicast.
 *  - CONFIG_ESPNOW_CTRL_PEER_MAC is the transmitter's STA MAC.
 *  - The drone answers with STATUS frames to the transmitter's MAC, sent from
 *    the drone's AP MAC, and with TELEMETRY frames if
 *    CONFIG_ESPNOW_CTRL_TELEMETRY_PERIOD_MS is not 0. A transmitter that does
 *    not know a frame type ignores it.
 *
 * Wire format: packed, little endian, CRC-16/CCITT-FALSE over every byte that
 * precedes the crc field.
 *
 * Sign conventions follow cflib send_setpoint(): positive roll = right,
 * positive pitch = forward, positive yaw = clockwise rate.
 *
 * Arming: raise ESPNOW_LINK_FLAG_ARM while thrust is 0. A link that is
 * (re)established with the ARM flag already set does not arm, drop the flag
 * and raise it again. Dropping ARM disarms. ESPNOW_LINK_FLAG_KILL stops the
 * motors immediately and latches until a frame with ARM and KILL both clear.
 */

#ifndef ESPNOW_LINK_H_
#define ESPNOW_LINK_H_

#include <stdint.h>

#define ESPNOW_LINK_MAGIC    0xD7
#define ESPNOW_LINK_VERSION  1

#define ESPNOW_LINK_PKT_CONTROL    1
#define ESPNOW_LINK_PKT_STATUS     2
#define ESPNOW_LINK_PKT_TELEMETRY  3

/* CONTROL flags */
#define ESPNOW_LINK_FLAG_ARM   (1 << 0)
#define ESPNOW_LINK_FLAG_KILL  (1 << 1)

/* STATUS flags */
#define ESPNOW_LINK_STATUS_ARMED        (1 << 0)
#define ESPNOW_LINK_STATUS_KILLED       (1 << 1)
#define ESPNOW_LINK_STATUS_BATTERY_LOW  (1 << 2)
#define ESPNOW_LINK_STATUS_FAILSAFE     (1 << 3)

#define ESPNOW_LINK_AXIS_MAX    1000   // roll, pitch, yaw: -1000..+1000
#define ESPNOW_LINK_THRUST_MAX  1000   // thrust: 0..1000

/* Transmitter -> drone */
typedef struct {
  uint8_t  magic;     // ESPNOW_LINK_MAGIC
  uint8_t  version;   // ESPNOW_LINK_VERSION
  uint8_t  type;      // ESPNOW_LINK_PKT_CONTROL
  uint8_t  flags;     // ESPNOW_LINK_FLAG_*
  uint16_t seq;       // increments by one per frame, wraps
  int16_t  roll;
  int16_t  pitch;
  int16_t  yaw;
  uint16_t thrust;
  uint16_t crc;
} __attribute__((packed)) espnowLinkControl_t;

/* Drone -> transmitter */
typedef struct {
  uint8_t  magic;
  uint8_t  version;
  uint8_t  type;       // ESPNOW_LINK_PKT_STATUS
  uint8_t  flags;      // ESPNOW_LINK_STATUS_*
  uint16_t seqAck;     // seq of the last control frame that was applied
  uint16_t batteryMv;  // pack voltage
  uint16_t crc;
} __attribute__((packed)) espnowLinkStatus_t;

/* Drone -> transmitter, while the link is up */
typedef struct {
  uint8_t  magic;
  uint8_t  version;
  uint8_t  type;       // ESPNOW_LINK_PKT_TELEMETRY
  uint8_t  flags;      // ESPNOW_LINK_STATUS_*
  uint16_t seq;        // telemetry frame counter
  int16_t  roll;       // estimated attitude, 0.01 deg
  int16_t  pitch;
  int16_t  yaw;
  int16_t  gyroX;      // body rates, 0.1 deg/s
  int16_t  gyroY;
  int16_t  gyroZ;
  uint16_t thrust;     // thrust sent to the mixer
  uint16_t motor[4];   // mixer outputs M1-M4 (DShot 48-2047 on brushless)
  uint16_t batteryMv;  // pack voltage
  uint16_t crc;
} __attribute__((packed)) espnowLinkTelemetry_t;

static inline uint16_t espnowLinkCrc16(const uint8_t *data, unsigned int len)
{
  uint16_t crc = 0xFFFF;

  while (len--) {
    crc ^= (uint16_t)(*data++) << 8;
    for (int i = 0; i < 8; i++) {
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
  }

  return crc;
}

#ifndef ESPNOW_LINK_TRANSMITTER_BUILD

#include <stdbool.h>

/**
 * Start the ESP-NOW control link. Wi-Fi must already be started.
 * Safe to call more than once.
 */
void espnowLinkInit(void);

bool espnowLinkTest(void);

/**
 * True while control frames are arriving within the link timeout.
 */
bool espnowLinkIsActive(void);

#endif /* ESPNOW_LINK_TRANSMITTER_BUILD */

#endif /* ESPNOW_LINK_H_ */
