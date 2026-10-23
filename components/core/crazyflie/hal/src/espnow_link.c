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
 * espnow_link.c - ESP-NOW control link between a transmitter and the drone.
 * The wire format and the radio setup are described in espnow_link.h.
 *
 * The receive callback runs in the Wi-Fi task and only validates the frame
 * and hands the latest one to espnowLinkTask. The task owns all the state:
 * arming, kill latch, failsafe and the setpoint that is fed to the commander.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_idf_version.h"
#include "esp_now.h"
#include <math.h>

#include "esp_wifi.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "config.h"
#include "espnow_link.h"
#include "commander.h"
#include "crtp.h"
#include "crtp_commander.h"
#include "stabilizer.h"
#include "system.h"
#include "pm_esplane.h"
#include "queuemonitor.h"
#include "log.h"
#include "stm32_legacy.h"
#define DEBUG_MODULE "ESPNOW_LINK"
#include "debug_cf.h"
#include "static_mem.h"

#ifdef CONFIG_ESPNOW_CTRL_ENABLE

#define LINK_TIMEOUT        M2T(CONFIG_ESPNOW_CTRL_LINK_TIMEOUT_MS)
#define STATUS_PERIOD       M2T(CONFIG_ESPNOW_CTRL_STATUS_PERIOD_MS)
#define TELEMETRY_PERIOD    M2T(CONFIG_ESPNOW_CTRL_TELEMETRY_PERIOD_MS)   // 0: no telemetry

#define CRTP_RPYT_SIZE      14 // roll, pitch, yaw (float) + thrust (uint16)

// Full throttle on the transmitter never exceeds the commander thrust limit
#if defined(CONFIG_MOTOR_THRUST_MAX) && (CONFIG_ESPNOW_CTRL_THRUST_MAX > CONFIG_MOTOR_THRUST_MAX)
  #define THRUST_FULL       CONFIG_MOTOR_THRUST_MAX
#else
  #define THRUST_FULL       CONFIG_ESPNOW_CTRL_THRUST_MAX
#endif
#define KEY_LEN             16 // ESP-NOW PMK / LMK length

typedef struct {
  espnowLinkControl_t pkt;
  uint8_t mac[ESP_NOW_ETH_ALEN];
  TickType_t rxTick;
} rxItem_t;

static bool isInit = false;

STATIC_MEM_TASK_ALLOC(espnowLinkTask, ESPNOW_LINK_TASK_STACKSIZE);
static xQueueHandle rxQueue;
STATIC_MEM_QUEUE_ALLOC(rxQueue, 1, sizeof(rxItem_t));

/* Set at init, read by the callback */
static uint8_t peerMac[ESP_NOW_ETH_ALEN];
static bool peerMacSet = false;

/* Only touched by espnowLinkTask */
static setpoint_t setpoint;
static bool active = false;
static bool failsafeActive = false;
static bool killLatched = false;
static bool prevArm = false;
static bool haveSeq = false;
static uint16_t lastSeq;
static uint16_t seqAck;
static TickType_t lastRxTick;
static TickType_t lastStatusTick;
static TickType_t lastTelemetryTick;
static uint16_t telemetrySeq;
static uint8_t statusPeer[ESP_NOW_ETH_ALEN];
static bool haveStatusPeer = false;

/* Statistics, written from both contexts, only used for logging */
static volatile uint32_t rxCount;
static volatile uint32_t badCount;
static volatile uint32_t deniedCount;
static volatile uint32_t staleCount;
static volatile uint8_t activeLog;

static void espnowLinkTask(void *param);

static bool parseMac(const char *str, uint8_t *mac)
{
  unsigned int m[ESP_NOW_ETH_ALEN];

  if (sscanf(str, "%2x:%2x:%2x:%2x:%2x:%2x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) != ESP_NOW_ETH_ALEN) {
    return false;
  }

  for (int i = 0; i < ESP_NOW_ETH_ALEN; i++) {
    mac[i] = (uint8_t)m[i];
  }

  return true;
}

static void handleReceive(const uint8_t *mac, const uint8_t *data, int len)
{
  rxItem_t item;

  if (mac == NULL || data == NULL || len != sizeof(espnowLinkControl_t)) {
    badCount++;
    return;
  }

  memcpy(&item.pkt, data, sizeof(item.pkt));

  if (item.pkt.magic != ESPNOW_LINK_MAGIC ||
      item.pkt.version != ESPNOW_LINK_VERSION ||
      item.pkt.type != ESPNOW_LINK_PKT_CONTROL ||
      item.pkt.crc != espnowLinkCrc16(data, sizeof(item.pkt) - sizeof(item.pkt.crc))) {
    badCount++;
    return;
  }

  if (peerMacSet && memcmp(mac, peerMac, ESP_NOW_ETH_ALEN) != 0) {
    deniedCount++;
    return;
  }

  memcpy(item.mac, mac, ESP_NOW_ETH_ALEN);
  item.rxTick = xTaskGetTickCount();
  rxCount++;

  // Keep only the newest frame, an old setpoint is worthless
  xQueueOverwrite(rxQueue, &item);
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
static void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
  handleReceive(info->src_addr, data, len);
}
#else
static void onReceive(const uint8_t *mac, const uint8_t *data, int len)
{
  handleReceive(mac, data, len);
}
#endif

static float axisToFloat(int16_t value, float scale)
{
  if (value > ESPNOW_LINK_AXIS_MAX) {
    value = ESPNOW_LINK_AXIS_MAX;
  } else if (value < -ESPNOW_LINK_AXIS_MAX) {
    value = -ESPNOW_LINK_AXIS_MAX;
  }

  return (float)value * scale / ESPNOW_LINK_AXIS_MAX;
}

/**
 * Feed a setpoint through the same decoder the CRTP commander port uses so
 * thrust locking and the flight modes behave identically.
 */
static void pushSetpoint(float roll, float pitch, float yaw, uint16_t thrust)
{
  CRTPPacket pk;

  memset(&pk, 0, sizeof(pk));
  pk.header = CRTP_HEADER(CRTP_PORT_SETPOINT, 0);
  pk.size = CRTP_RPYT_SIZE;
  memcpy(&pk.data[0], &roll, 4);
  memcpy(&pk.data[4], &pitch, 4);
  memcpy(&pk.data[8], &yaw, 4);
  memcpy(&pk.data[12], &thrust, 2);

  crtpCommanderRpytDecodeSetpoint(&setpoint, &pk);
  commanderSetSetpoint(&setpoint, COMMANDER_PRIORITY_CRTP);
}

static void applyControl(const espnowLinkControl_t *c)
{
  const bool wantArm = (c->flags & ESPNOW_LINK_FLAG_ARM) != 0;
  const bool wantKill = (c->flags & ESPNOW_LINK_FLAG_KILL) != 0;

  if (wantKill) {
    if (!killLatched) {
      killLatched = true;
      stabilizerSetEmergencyStop();
      DEBUG_PRINTW("Kill received, motors stopped");
    }
    systemSetArmed(false);
  } else if (killLatched && !wantArm) {
    killLatched = false;
    stabilizerResetEmergencyStop();
    DEBUG_PRINTI("Kill released");
  }

  if (!killLatched) {
    if (wantArm && !prevArm) {
      if (c->thrust == 0) {
        systemSetArmed(true);
        DEBUG_PRINTI("Armed");
      } else {
        DEBUG_PRINTW("Arm refused, thrust is not zero");
      }
    } else if (!wantArm) {
      systemSetArmed(false);
    }
  }
  prevArm = wantArm;

  uint16_t thrust = 0;
  if (systemIsArmed() && !killLatched) {
    uint32_t t = (c->thrust > ESPNOW_LINK_THRUST_MAX) ? ESPNOW_LINK_THRUST_MAX : c->thrust;
    thrust = (uint16_t)(t * THRUST_FULL / ESPNOW_LINK_THRUST_MAX);
  }

  pushSetpoint(axisToFloat(c->roll, CONFIG_ESPNOW_CTRL_MAX_ANGLE_DEG),
               axisToFloat(c->pitch, CONFIG_ESPNOW_CTRL_MAX_ANGLE_DEG),
               axisToFloat(c->yaw, CONFIG_ESPNOW_CTRL_MAX_YAW_RATE_DPS),
               thrust);
}

static void enterFailsafe(void)
{
  active = false;
  failsafeActive = true;
  activeLog = 0;

  pushSetpoint(0.0f, 0.0f, 0.0f, 0);
#ifdef CONFIG_ESPNOW_CTRL_FAILSAFE_DISARM
  systemSetArmed(false);
#endif
  DEBUG_PRINTW("Link lost, failsafe");
}

static bool ensureStatusPeer(const uint8_t *mac)
{
  if (haveStatusPeer && memcmp(statusPeer, mac, ESP_NOW_ETH_ALEN) == 0) {
    return true;
  }

  // Without a configured peer any sender may show up, keep one peer at most
  if (haveStatusPeer && !peerMacSet) {
    esp_now_del_peer(statusPeer);
    haveStatusPeer = false;
  }

  if (!esp_now_is_peer_exist(mac)) {
    esp_now_peer_info_t peer = {0};

    memcpy(peer.peer_addr, mac, ESP_NOW_ETH_ALEN);
    peer.channel = 0; // follow the current channel
    peer.ifidx = WIFI_IF_AP;
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK) {
      return false;
    }
  }

  memcpy(statusPeer, mac, ESP_NOW_ETH_ALEN);
  haveStatusPeer = true;
  return true;
}

static void sendStatus(void)
{
  espnowLinkStatus_t st;

  if (!haveStatusPeer) {
    return;
  }

  st.magic = ESPNOW_LINK_MAGIC;
  st.version = ESPNOW_LINK_VERSION;
  st.type = ESPNOW_LINK_PKT_STATUS;
  st.flags = (systemIsArmed() ? ESPNOW_LINK_STATUS_ARMED : 0) |
             (killLatched ? ESPNOW_LINK_STATUS_KILLED : 0) |
             (pmIsBatteryLow() ? ESPNOW_LINK_STATUS_BATTERY_LOW : 0) |
             (failsafeActive ? ESPNOW_LINK_STATUS_FAILSAFE : 0);
  st.seqAck = seqAck;
  st.batteryMv = (uint16_t)(pmGetBatteryVoltage() * 1000.0f);
  st.crc = espnowLinkCrc16((const uint8_t *)&st, sizeof(st) - sizeof(st.crc));

  esp_now_send(statusPeer, (const uint8_t *)&st, sizeof(st));
}

static int16_t clampI16(float v)
{
  if (v > 32767.0f) {
    return 32767;
  }
  if (v < -32768.0f) {
    return -32768;
  }
  return (int16_t)lrintf(v);
}

static uint16_t clampU16(uint32_t v)
{
  return (v > 0xFFFF) ? 0xFFFF : (uint16_t)v;
}

/* Log variables read for the telemetry frame, resolved once */
static struct {
  bool resolved;
  logVarId_t roll, pitch, yaw, thrust;
  logVarId_t gyro[3];
  logVarId_t motor[4];
} tlmVars;

static float logFloatOrZero(logVarId_t id)
{
  return LOG_VARID_IS_VALID(id) ? logGetFloat(id) : 0.0f;
}

static void sendTelemetry(void)
{
  espnowLinkTelemetry_t t;
  static char *motorNames[4] = {"m1", "m2", "m3", "m4"};
  static char *axisNames[3] = {"x", "y", "z"};

  if (!haveStatusPeer) {
    return;
  }
  if (!tlmVars.resolved) {
    tlmVars.roll = logGetVarId("stabilizer", "roll");
    tlmVars.pitch = logGetVarId("stabilizer", "pitch");
    tlmVars.yaw = logGetVarId("stabilizer", "yaw");
    tlmVars.thrust = logGetVarId("stabilizer", "thrust");
    for (int i = 0; i < 3; i++) {
      tlmVars.gyro[i] = logGetVarId("gyro", axisNames[i]);
    }
    for (int i = 0; i < 4; i++) {
      tlmVars.motor[i] = logGetVarId("motor", motorNames[i]);
    }
    tlmVars.resolved = true;
  }

  t.magic = ESPNOW_LINK_MAGIC;
  t.version = ESPNOW_LINK_VERSION;
  t.type = ESPNOW_LINK_PKT_TELEMETRY;
  t.flags = (systemIsArmed() ? ESPNOW_LINK_STATUS_ARMED : 0) |
            (killLatched ? ESPNOW_LINK_STATUS_KILLED : 0) |
            (pmIsBatteryLow() ? ESPNOW_LINK_STATUS_BATTERY_LOW : 0) |
            (failsafeActive ? ESPNOW_LINK_STATUS_FAILSAFE : 0);
  t.seq = telemetrySeq++;
  t.roll = clampI16(logFloatOrZero(tlmVars.roll) * 100.0f);
  t.pitch = clampI16(logFloatOrZero(tlmVars.pitch) * 100.0f);
  t.yaw = clampI16(logFloatOrZero(tlmVars.yaw) * 100.0f);
  t.gyroX = clampI16(logFloatOrZero(tlmVars.gyro[0]) * 10.0f);
  t.gyroY = clampI16(logFloatOrZero(tlmVars.gyro[1]) * 10.0f);
  t.gyroZ = clampI16(logFloatOrZero(tlmVars.gyro[2]) * 10.0f);
  const float thrust = logFloatOrZero(tlmVars.thrust);
  t.thrust = clampU16(thrust > 0.0f ? (uint32_t)thrust : 0);
  for (int i = 0; i < 4; i++) {
    t.motor[i] = LOG_VARID_IS_VALID(tlmVars.motor[i]) ? clampU16(logGetUint(tlmVars.motor[i])) : 0;
  }
  t.batteryMv = (uint16_t)(pmGetBatteryVoltage() * 1000.0f);
  t.crc = espnowLinkCrc16((const uint8_t *)&t, sizeof(t) - sizeof(t.crc));

  esp_now_send(statusPeer, (const uint8_t *)&t, sizeof(t));
}

/* Ticks until a periodic event is due, at least 1 */
static TickType_t untilDue(TickType_t now, TickType_t last, TickType_t period)
{
  TickType_t elapsed = now - last;
  return (elapsed >= period) ? 1 : (period - elapsed);
}

static void espnowLinkTask(void *param)
{
  rxItem_t item;

  // The commander and the stabilizer are not ready before the system starts
  systemWaitStart();

  while (1) {
    TickType_t wait = portMAX_DELAY;

    if (active) {
      // Wake for the next status or telemetry frame or the link timeout, whichever is first
      TickType_t now = xTaskGetTickCount();
      TickType_t sinceRx = now - lastRxTick;
      TickType_t untilTimeout = (sinceRx >= LINK_TIMEOUT) ? 1 : (LINK_TIMEOUT - sinceRx + 1);
      TickType_t untilStatus = untilDue(now, lastStatusTick, STATUS_PERIOD);

      wait = (untilTimeout < untilStatus) ? untilTimeout : untilStatus;
      if (TELEMETRY_PERIOD > 0) {
        TickType_t untilTelemetry = untilDue(now, lastTelemetryTick, TELEMETRY_PERIOD);
        wait = (untilTelemetry < wait) ? untilTelemetry : wait;
      }
    }

    if (xQueueReceive(rxQueue, &item, wait) == pdTRUE) {
      TickType_t now = xTaskGetTickCount();

      if (now - item.rxTick > LINK_TIMEOUT) {
        // Waited in the queue while the system was starting
        staleCount++;
      } else if (active && haveSeq && (int16_t)(item.pkt.seq - lastSeq) <= 0) {
        // Duplicate or out of order
        staleCount++;
      } else {
        if (!active) {
          // New link: do not inherit arming from a frame that is already armed
          active = true;
          failsafeActive = false;
          prevArm = (item.pkt.flags & ESPNOW_LINK_FLAG_ARM) != 0;
          lastStatusTick = 0;
          lastTelemetryTick = 0;
          activeLog = 1;
          DEBUG_PRINTI("Link up");
        }

        haveSeq = true;
        lastSeq = item.pkt.seq;
        seqAck = item.pkt.seq;
        lastRxTick = item.rxTick;

        applyControl(&item.pkt);
        ensureStatusPeer(item.mac);
      }
    }

    if (active) {
      TickType_t now = xTaskGetTickCount();

      if (now - lastRxTick > LINK_TIMEOUT) {
        haveSeq = false;
        enterFailsafe();
      } else {
        if (now - lastStatusTick >= STATUS_PERIOD) {
          lastStatusTick = now;
          sendStatus();
        }
        if (TELEMETRY_PERIOD > 0 && now - lastTelemetryTick >= TELEMETRY_PERIOD) {
          lastTelemetryTick = now;
          sendTelemetry();
        }
      }
    }
  }
}

void espnowLinkInit(void)
{
  if (isInit) {
    return;
  }

  const char *macStr = CONFIG_ESPNOW_CTRL_PEER_MAC;

  if (macStr[0] != '\0') {
    if (!parseMac(macStr, peerMac)) {
      DEBUG_PRINTE("Invalid ESPNOW_CTRL_PEER_MAC '%s', link disabled", macStr);
      return;
    }
    peerMacSet = true;
  } else {
    DEBUG_PRINTW("No peer MAC configured, control accepted from ANY transmitter");
  }

  if (esp_now_init() != ESP_OK) {
    DEBUG_PRINTE("esp_now_init failed, link disabled");
    return;
  }

#ifdef CONFIG_ESPNOW_CTRL_ENCRYPT
  const char *pmk = CONFIG_ESPNOW_CTRL_PMK;
  const char *lmk = CONFIG_ESPNOW_CTRL_LMK;

  if (!peerMacSet || strlen(pmk) != KEY_LEN || strlen(lmk) != KEY_LEN) {
    DEBUG_PRINTE("Encryption needs a peer MAC and 16 character PMK and LMK, link disabled");
    esp_now_deinit();
    return;
  }

  esp_now_peer_info_t peer = {0};
  memcpy(peer.peer_addr, peerMac, ESP_NOW_ETH_ALEN);
  peer.channel = 0;
  peer.ifidx = WIFI_IF_AP;
  peer.encrypt = true;
  memcpy(peer.lmk, lmk, KEY_LEN);

  if (esp_now_set_pmk((const uint8_t *)pmk) != ESP_OK || esp_now_add_peer(&peer) != ESP_OK) {
    DEBUG_PRINTE("Failed to set up encryption, link disabled");
    esp_now_deinit();
    return;
  }
  memcpy(statusPeer, peerMac, ESP_NOW_ETH_ALEN);
  haveStatusPeer = true;
#endif

  rxQueue = STATIC_MEM_QUEUE_CREATE(rxQueue);
  DEBUG_QUEUE_MONITOR_REGISTER(rxQueue);

  STATIC_MEM_TASK_CREATE(espnowLinkTask, espnowLinkTask, ESPNOW_LINK_TASK_NAME, NULL, ESPNOW_LINK_TASK_PRI);

#ifdef CONFIG_ESPNOW_CTRL_START_DISARMED
  systemSetArmed(false);
#endif

  if (esp_now_register_recv_cb(onReceive) != ESP_OK) {
    DEBUG_PRINTE("Failed to register the receive callback");
    return;
  }

  isInit = true;
}

bool espnowLinkTest(void)
{
  return isInit;
}

bool espnowLinkIsActive(void)
{
  return active;
}

LOG_GROUP_START(espnowLink)
LOG_ADD(LOG_UINT32, rx, &rxCount)
LOG_ADD(LOG_UINT32, bad, &badCount)
LOG_ADD(LOG_UINT32, denied, &deniedCount)
LOG_ADD(LOG_UINT32, stale, &staleCount)
LOG_ADD(LOG_UINT8, active, &activeLog)
LOG_GROUP_STOP(espnowLink)

#else /* CONFIG_ESPNOW_CTRL_ENABLE */

void espnowLinkInit(void)
{
}

bool espnowLinkTest(void)
{
  return true;
}

bool espnowLinkIsActive(void)
{
  return false;
}

#endif /* CONFIG_ESPNOW_CTRL_ENABLE */
