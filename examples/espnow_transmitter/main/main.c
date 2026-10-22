/*
 * Reference transmitter for the ESPDrone ESP-NOW control link.
 *
 * Sends a control frame every 10 ms and prints the status frames the drone
 * answers with. The stick values come from readInputs(), replace it with your
 * ADC joystick, gamepad or phone bridge. As shipped it sends zero thrust and
 * raises ARM after a few seconds, so it is safe to test with the props off.
 *
 * Before flashing:
 *  - set DRONE_AP_MAC to the MAC at the end of the drone's Wi-Fi SSID
 *    (ESP-DRONE_<MAC>), or leave it all zero to send broadcast
 *  - set LINK_CHANNEL to the drone's CONFIG_WIFI_CHANNEL (default 6)
 *  - copy the STA MAC this firmware prints at boot into the drone's
 *    ESPNOW_CTRL_PEER_MAC (menuconfig: Receiver & Link -> ESP-NOW control link)
 *
 * The wire format is shared with the drone through espnow_link.h.
 */
#include <stdbool.h>
#include <string.h>

#include "esp_event.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#define ESPNOW_LINK_TRANSMITTER_BUILD
#include "espnow_link.h"

#define DRONE_AP_MAC     {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
#define LINK_CHANNEL     6
#define SEND_PERIOD_MS   10
#define ARM_AFTER_MS     3000

static const char *TAG = "TX";
static const uint8_t BROADCAST_MAC[ESP_NOW_ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static uint8_t droneMac[ESP_NOW_ETH_ALEN] = DRONE_AP_MAC;

typedef struct {
    int16_t roll;      // -1000..1000, positive = right
    int16_t pitch;     // -1000..1000, positive = forward
    int16_t yaw;       // -1000..1000, positive = clockwise rate
    uint16_t thrust;   // 0..1000
    bool arm;
    bool kill;
} inputs_t;

static volatile espnowLinkStatus_t lastStatus;
static volatile bool statusSeen;

/* Replace with the real sticks. Thrust stays 0 here on purpose. */
static void readInputs(inputs_t *in, uint32_t nowMs)
{
    memset(in, 0, sizeof(*in));
    in->arm = nowMs > ARM_AFTER_MS;
}

static void handleStatus(const uint8_t *data, int len)
{
    espnowLinkStatus_t status;

    if (len != sizeof(status)) {
        return;
    }
    memcpy(&status, data, sizeof(status));

    if (status.magic != ESPNOW_LINK_MAGIC || status.version != ESPNOW_LINK_VERSION ||
        status.type != ESPNOW_LINK_PKT_STATUS ||
        status.crc != espnowLinkCrc16(data, sizeof(status) - sizeof(status.crc))) {
        return;
    }
    lastStatus = status;
    statusSeen = true;
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
static void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    handleStatus(data, len);
}
#else
static void onReceive(const uint8_t *mac, const uint8_t *data, int len)
{
    handleStatus(data, len);
}
#endif

static void sendTask(void *param)
{
    const bool broadcast = memcmp(droneMac, "\0\0\0\0\0\0", ESP_NOW_ETH_ALEN) == 0;
    const uint8_t *dest = broadcast ? BROADCAST_MAC : droneMac;
    TickType_t wake = xTaskGetTickCount();
    uint32_t lastPrintMs = 0;
    uint16_t seq = 0;

    while (1) {
        const uint32_t nowMs = xTaskGetTickCount() * portTICK_PERIOD_MS;
        inputs_t in;
        espnowLinkControl_t frame;

        readInputs(&in, nowMs);

        frame.magic = ESPNOW_LINK_MAGIC;
        frame.version = ESPNOW_LINK_VERSION;
        frame.type = ESPNOW_LINK_PKT_CONTROL;
        frame.flags = (in.arm ? ESPNOW_LINK_FLAG_ARM : 0) | (in.kill ? ESPNOW_LINK_FLAG_KILL : 0);
        frame.seq = seq++;
        frame.roll = in.roll;
        frame.pitch = in.pitch;
        frame.yaw = in.yaw;
        frame.thrust = in.thrust;
        frame.crc = espnowLinkCrc16((const uint8_t *)&frame, sizeof(frame) - sizeof(frame.crc));

        esp_now_send(dest, (const uint8_t *)&frame, sizeof(frame));

        if (nowMs - lastPrintMs >= 1000) {
            lastPrintMs = nowMs;
            if (statusSeen) {
                espnowLinkStatus_t s = lastStatus;
                ESP_LOGI(TAG, "drone: %s%s%s%s battery %u mV, acked seq %u",
                         (s.flags & ESPNOW_LINK_STATUS_ARMED) ? "ARMED " : "disarmed ",
                         (s.flags & ESPNOW_LINK_STATUS_KILLED) ? "KILLED " : "",
                         (s.flags & ESPNOW_LINK_STATUS_BATTERY_LOW) ? "BATTERY-LOW " : "",
                         (s.flags & ESPNOW_LINK_STATUS_FAILSAFE) ? "FAILSAFE " : "",
                         s.batteryMv, s.seqAck);
            } else {
                ESP_LOGW(TAG, "no status from the drone yet");
            }
        }

        vTaskDelayUntil(&wake, pdMS_TO_TICKS(SEND_PERIOD_MS));
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(LINK_CHANNEL, WIFI_SECOND_CHAN_NONE));

    uint8_t mac[ESP_NOW_ETH_ALEN];
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, mac));
    ESP_LOGI(TAG, "STA MAC for ESPNOW_CTRL_PEER_MAC: %02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(onReceive));

    const bool broadcast = memcmp(droneMac, "\0\0\0\0\0\0", ESP_NOW_ETH_ALEN) == 0;
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, broadcast ? BROADCAST_MAC : droneMac, ESP_NOW_ETH_ALEN);
    peer.channel = 0;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

    xTaskCreate(sendTask, "send", 3072, NULL, 5, NULL);
}
