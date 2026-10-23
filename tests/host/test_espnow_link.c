/* Host test of the ESP-NOW control link state machine.
 * Includes espnow_link.c itself and replaces the OS and the flight code with
 * stubs, so the arming, kill, scaling, validation and failsafe logic that runs
 * on the drone is the code under test. Run: tests/host/run.sh */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "../../components/core/crazyflie/hal/src/espnow_link.c"

/* ---- stubs for everything espnow_link.c calls -------------------------- */
static bool g_armed, g_emergency, g_forceArm;
static int  g_setpoints;
static float g_roll, g_pitch, g_yaw;
static uint16_t g_thrust;
static rxItem_t g_queued; static int g_queuedCount;
static TickType_t g_tick;
static int g_statusSent;

void systemWaitStart(void) {}
void systemSetArmed(bool v) { g_armed = v; }
bool systemIsArmed(void) { return g_armed || g_forceArm; }
void stabilizerSetEmergencyStop(void) { g_emergency = true; }
void stabilizerResetEmergencyStop(void) { g_emergency = false; }
void commanderSetSetpoint(setpoint_t *sp, int prio) { (void)sp; (void)prio; g_setpoints++; }
void crtpCommanderRpytDecodeSetpoint(setpoint_t *sp, CRTPPacket *pk) {
  (void)sp;
  memcpy(&g_roll, &pk->data[0], 4); memcpy(&g_pitch, &pk->data[4], 4);
  memcpy(&g_yaw, &pk->data[8], 4);  memcpy(&g_thrust, &pk->data[12], 2);
}
bool pmIsBatteryLow(void) { return false; }
float pmGetBatteryVoltage(void) { return 11.1f; }
TickType_t xTaskGetTickCount(void) { return g_tick; }
BaseType_t xQueueOverwrite(xQueueHandle q, const void *item) { (void)q; memcpy(&g_queued, item, sizeof(g_queued)); g_queuedCount++; return pdTRUE; }
BaseType_t xQueueReceive(xQueueHandle q, void *item, TickType_t w) { (void)q; (void)item; (void)w; return 0; }
esp_err_t esp_now_init(void) { return ESP_OK; }
esp_err_t esp_now_deinit(void) { return ESP_OK; }
esp_err_t esp_now_register_recv_cb(esp_now_recv_cb_t cb) { (void)cb; return ESP_OK; }
esp_err_t esp_now_add_peer(const esp_now_peer_info_t *p) { (void)p; return ESP_OK; }
esp_err_t esp_now_del_peer(const uint8_t *a) { (void)a; return ESP_OK; }
bool esp_now_is_peer_exist(const uint8_t *a) { (void)a; return false; }
static uint8_t g_lastSent[64]; static size_t g_lastSentLen;
esp_err_t esp_now_send(const uint8_t *a, const uint8_t *d, size_t l) {
  (void)a; g_statusSent++;
  g_lastSentLen = l < sizeof(g_lastSent) ? l : sizeof(g_lastSent);
  memcpy(g_lastSent, d, g_lastSentLen);
  return ESP_OK;
}
/* Log variables for the telemetry frame: ids 1.. in the order they are looked up */
static const char *g_logNames[] = {"stabilizer.roll", "stabilizer.pitch", "stabilizer.yaw", "stabilizer.thrust",
                                   "gyro.x", "gyro.y", "gyro.z", "motor.m1", "motor.m2", "motor.m3", "motor.m4"};
static const float g_logValues[] = {12.34f, -5.5f, 179.99f, 30000.0f, 100.0f, -0.26f, 4000.0f, 48, 1000, 2047, 70000};
logVarId_t logGetVarId(char *group, char *name) {
  char full[48];
  snprintf(full, sizeof(full), "%s.%s", group, name);
  for (unsigned i = 0; i < sizeof(g_logNames) / sizeof(g_logNames[0]); i++) {
    if (strcmp(full, g_logNames[i]) == 0) return (logVarId_t)(i + 1);
  }
  return 0xffff;
}
float logGetFloat(logVarId_t id) { return g_logValues[id - 1]; }
unsigned int logGetUint(logVarId_t id) { return (unsigned int)g_logValues[id - 1]; }
esp_err_t esp_now_set_pmk(const uint8_t *k) { (void)k; return ESP_OK; }

/* ---- helpers ----------------------------------------------------------- */
static const uint8_t PEER[6] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
static const uint8_t OTHER[6] = {1, 2, 3, 4, 5, 6};

static espnowLinkControl_t frame(uint8_t flags, int16_t roll, int16_t pitch, int16_t yaw, uint16_t thrust)
{
  static uint16_t seq;
  espnowLinkControl_t f = { ESPNOW_LINK_MAGIC, ESPNOW_LINK_VERSION, ESPNOW_LINK_PKT_CONTROL, flags,
                            seq++, roll, pitch, yaw, thrust, 0 };
  f.crc = espnowLinkCrc16((const uint8_t *)&f, sizeof(f) - sizeof(f.crc));
  return f;
}

static void reset(void)
{
  g_armed = g_emergency = g_forceArm = false;
  killLatched = false; prevArm = false; g_setpoints = 0;
  g_roll = g_pitch = g_yaw = 0; g_thrust = 0; g_queuedCount = 0;
}

#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)
static int failures;

int main(void)
{
  espnowLinkControl_t f;

  /* init: MAC parsed, starts disarmed */
  g_armed = true;
  espnowLinkInit();
  CHECK(peerMacSet && memcmp(peerMac, PEER, 6) == 0);
  CHECK(!g_armed);
  CHECK(espnowLinkTest());

  /* arming needs a 0 -> 1 edge with zero thrust */
  reset(); f = frame(ESPNOW_LINK_FLAG_ARM, 0, 0, 0, 0);   applyControl(&f); CHECK(g_armed);
  reset(); f = frame(ESPNOW_LINK_FLAG_ARM, 0, 0, 0, 500); applyControl(&f); CHECK(!g_armed);
  reset(); prevArm = true; f = frame(ESPNOW_LINK_FLAG_ARM, 0, 0, 0, 0); applyControl(&f); CHECK(!g_armed);  /* no edge */
  reset(); f = frame(ESPNOW_LINK_FLAG_ARM, 0, 0, 0, 0); applyControl(&f);
  f = frame(0, 0, 0, 0, 0); applyControl(&f); CHECK(!g_armed);                                           /* disarm */

  /* thrust is forced to 0 while disarmed, scaled and clamped while armed */
  reset(); f = frame(0, 0, 0, 0, 800); applyControl(&f); CHECK(g_thrust == 0);
  reset(); f = frame(ESPNOW_LINK_FLAG_ARM, 0, 0, 0, 0); applyControl(&f);
  f = frame(ESPNOW_LINK_FLAG_ARM, 0, 0, 0, 1000); applyControl(&f); CHECK(g_thrust == 300);
  f = frame(ESPNOW_LINK_FLAG_ARM, 0, 0, 0, 500);  applyControl(&f); CHECK(g_thrust == 150);
  f = frame(ESPNOW_LINK_FLAG_ARM, 0, 0, 0, 60000); applyControl(&f); CHECK(g_thrust == 300);            /* clamped */

  /* axes: +-1000 is full scale, beyond is clamped */
  f = frame(ESPNOW_LINK_FLAG_ARM, 1000, -1000, 1000, 0); applyControl(&f);
  CHECK(fabsf(g_roll - 15.0f) < 1e-4f && fabsf(g_pitch + 15.0f) < 1e-4f && fabsf(g_yaw - 30.0f) < 1e-4f);
  f = frame(ESPNOW_LINK_FLAG_ARM, 5000, -5000, 0, 0); applyControl(&f);
  CHECK(fabsf(g_roll - 15.0f) < 1e-4f && fabsf(g_pitch + 15.0f) < 1e-4f);
  f = frame(ESPNOW_LINK_FLAG_ARM, 500, 0, 0, 0); applyControl(&f); CHECK(fabsf(g_roll - 7.5f) < 1e-4f);

  /* kill: stops, disarms, latches until ARM and KILL are both clear */
  reset(); f = frame(ESPNOW_LINK_FLAG_ARM, 0, 0, 0, 0); applyControl(&f); CHECK(g_armed);
  f = frame(ESPNOW_LINK_FLAG_ARM | ESPNOW_LINK_FLAG_KILL, 0, 0, 0, 400); applyControl(&f);
  CHECK(g_emergency && !g_armed && g_thrust == 0);
  f = frame(ESPNOW_LINK_FLAG_ARM, 0, 0, 0, 0); applyControl(&f);        /* KILL gone but ARM still up */
  CHECK(g_emergency && !g_armed);
  f = frame(0, 0, 0, 0, 0); applyControl(&f);                            /* both clear: released */
  CHECK(!g_emergency && !g_armed);
  f = frame(ESPNOW_LINK_FLAG_ARM, 0, 0, 0, 0); applyControl(&f); CHECK(g_armed);                          /* re-arm by edge */

  /* a stop that was not ours (tumble detection) is never reset by the link */
  reset(); g_emergency = true; f = frame(0, 0, 0, 0, 0); applyControl(&f); CHECK(g_emergency);

  /* failsafe: level, zero thrust, disarmed */
  reset(); f = frame(ESPNOW_LINK_FLAG_ARM, 0, 0, 0, 0); applyControl(&f);
  f = frame(ESPNOW_LINK_FLAG_ARM, 800, 800, 800, 1000); applyControl(&f); CHECK(g_thrust == 300);
  active = true; enterFailsafe();
  CHECK(!active && failsafeActive && !g_armed && g_thrust == 0 && g_roll == 0 && g_pitch == 0 && g_yaw == 0);

  /* receive validation */
  reset(); rxCount = badCount = deniedCount = 0;
  f = frame(0, 0, 0, 0, 0);
  handleReceive(PEER, (uint8_t *)&f, sizeof(f));                          CHECK(rxCount == 1 && g_queuedCount == 1);
  handleReceive(OTHER, (uint8_t *)&f, sizeof(f));                         CHECK(deniedCount == 1 && g_queuedCount == 1);
  handleReceive(PEER, (uint8_t *)&f, sizeof(f) - 1);                      CHECK(badCount == 1);
  espnowLinkControl_t bad = f; bad.thrust ^= 1;                           handleReceive(PEER, (uint8_t *)&bad, sizeof(bad));    CHECK(badCount == 2);
  bad = f; bad.magic = 0; bad.crc = espnowLinkCrc16((uint8_t *)&bad, sizeof(bad) - 2);
  handleReceive(PEER, (uint8_t *)&bad, sizeof(bad));                      CHECK(badCount == 3);
  bad = f; bad.version = 9; bad.crc = espnowLinkCrc16((uint8_t *)&bad, sizeof(bad) - 2);
  handleReceive(PEER, (uint8_t *)&bad, sizeof(bad));                      CHECK(badCount == 4);
  handleReceive(NULL, (uint8_t *)&f, sizeof(f));                          CHECK(badCount == 5);
  CHECK(g_queuedCount == 1);

  /* status frame is addressed to the sender and carries valid CRC */
  g_statusSent = 0; haveStatusPeer = false;
  CHECK(ensureStatusPeer(PEER)); seqAck = 42; sendStatus(); CHECK(g_statusSent == 1);

  if (failures) { printf("%d check(s) failed\n", failures); return 1; }
  /* Telemetry frame encoding */
  haveStatusPeer = true;
  killLatched = false;
  failsafeActive = false;
  g_armed = true;
  sendTelemetry();
  assert(g_lastSentLen == sizeof(espnowLinkTelemetry_t) && g_lastSentLen == 32);
  espnowLinkTelemetry_t t;
  memcpy(&t, g_lastSent, sizeof(t));
  assert(t.magic == ESPNOW_LINK_MAGIC && t.type == ESPNOW_LINK_PKT_TELEMETRY);
  assert(t.crc == espnowLinkCrc16(g_lastSent, sizeof(t) - sizeof(t.crc)));
  assert(t.flags & ESPNOW_LINK_STATUS_ARMED);
  assert(t.roll == 1234 && t.pitch == -550 && t.yaw == 17999);
  assert(t.gyroX == 1000 && t.gyroY == -3 && t.gyroZ == 32767);   // 400.0 deg/s clamps at 3276.7
  assert(t.thrust == 30000);
  assert(t.motor[0] == 48 && t.motor[1] == 1000 && t.motor[2] == 2047 && t.motor[3] == 0xFFFF);
  assert(t.batteryMv == 11100);
  uint16_t firstSeq = t.seq;
  sendTelemetry();
  memcpy(&t, g_lastSent, sizeof(t));
  assert(t.seq == (uint16_t)(firstSeq + 1));
  printf("Telemetry frame OK\n");

  printf("espnow_link host test: all checks passed\n");
  return 0;
}
