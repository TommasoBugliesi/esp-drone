/*
 * USB-to-ESP-NOW bridge for the ESPDrone control link.
 *
 * A host (tools/groundstation on a laptop today, a phone app over USB OTG
 * later) sends CONTROL frames over the serial port; the bridge checks them and
 * forwards each one to the drone at once. Frames from the drone (STATUS,
 * TELEMETRY) go back to the host. The protocol is in bridge_protocol.h.
 *
 * Safety: the bridge never makes up control frames. If the host stops, the
 * bridge stops, and the drone's own failsafe (ESPNOW_CTRL_LINK_TIMEOUT_MS)
 * levels and disarms it.
 *
 * Serial port: UART0 (the USB-UART chip of most ESP32 boards) at
 * BRIDGE_BAUDRATE, or the native USB of an ESP32-S3/C3 when the console is set
 * to USB-Serial-JTAG. Drone MAC and channel come from a BRIDGE_CONFIG frame
 * and are kept in NVS; by default the bridge broadcasts on channel 6.
 */
#include <stdbool.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_event.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#ifdef CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include "driver/usb_serial_jtag.h"
#endif

#define ESPNOW_LINK_TRANSMITTER_BUILD
#include "espnow_link.h"
#include "bridge_protocol.h"

#define DEFAULT_CHANNEL          6
#define BRIDGE_HOST_TIMEOUT_MS   200
#define DRONE_TIMEOUT_MS         500
#define INFO_PERIOD_MS           500
#define MAX_FRAME                64

static const char *TAG = "BRIDGE";
static const uint8_t BROADCAST_MAC[ESP_NOW_ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static const uint8_t ZERO_MAC[ESP_NOW_ETH_ALEN] = {0};

static uint8_t droneMac[ESP_NOW_ETH_ALEN];
static uint8_t channel = DEFAULT_CHANNEL;
static uint8_t staMac[ESP_NOW_ETH_ALEN];

static uint16_t txOk, txFail, hostTimeouts;
static volatile uint16_t rxFromDrone;
static TickType_t lastHostTick, lastDroneTick;
static bool hostActive;

typedef struct {
    uint8_t len;
    uint8_t data[MAX_FRAME];
} outFrame_t;
static QueueHandle_t outQueue;

/* ------------------------------------------------------------------ serial port */

static void serialInit(void)
{
#ifdef CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&cfg));
#else
    const uart_config_t cfg = {
        .baud_rate = BRIDGE_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 1024, 1024, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &cfg));
#endif
}

static int serialRead(uint8_t *buf, int len, TickType_t wait)
{
#ifdef CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    return usb_serial_jtag_read_bytes(buf, len, wait);
#else
    return uart_read_bytes(UART_NUM_0, buf, len, wait);
#endif
}

static void serialWrite(const uint8_t *buf, int len)
{
#ifdef CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    usb_serial_jtag_write_bytes(buf, len, pdMS_TO_TICKS(5));
#else
    uart_write_bytes(UART_NUM_0, (const char *)buf, len);
#endif
}

/* ------------------------------------------------------------------ frames */

static int frameLength(uint8_t type)
{
    switch (type) {
    case ESPNOW_LINK_PKT_CONTROL:   return sizeof(espnowLinkControl_t);
    case ESPNOW_LINK_PKT_STATUS:    return sizeof(espnowLinkStatus_t);
    case ESPNOW_LINK_PKT_TELEMETRY: return sizeof(espnowLinkTelemetry_t);
    case BRIDGE_PKT_CONFIG:         return sizeof(bridgeConfig_t);
    case BRIDGE_PKT_INFO:           return sizeof(bridgeInfo_t);
    default:                        return 0;
    }
}

static bool frameValid(const uint8_t *f, int len)
{
    uint16_t crc;
    if (len < 5 || f[0] != ESPNOW_LINK_MAGIC || f[1] != ESPNOW_LINK_VERSION || frameLength(f[2]) != len) {
        return false;
    }
    memcpy(&crc, &f[len - 2], sizeof(crc));
    return crc == espnowLinkCrc16(f, len - 2);
}

static void queueToHost(const uint8_t *data, int len)
{
    outFrame_t out;
    if (len > MAX_FRAME) {
        return;
    }
    out.len = len;
    memcpy(out.data, data, len);
    xQueueSend(outQueue, &out, 0);   // drop when the host does not keep up
}

/* ------------------------------------------------------------------ drone side */

static void setDrone(const uint8_t *mac, uint8_t chan)
{
    if (esp_now_is_peer_exist(droneMac)) {
        esp_now_del_peer(droneMac);
    }
    if (esp_now_is_peer_exist(BROADCAST_MAC)) {
        esp_now_del_peer(BROADCAST_MAC);
    }
    memcpy(droneMac, mac, ESP_NOW_ETH_ALEN);
    channel = chan;
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

    esp_now_peer_info_t peer = {0};
    const bool broadcast = memcmp(droneMac, ZERO_MAC, ESP_NOW_ETH_ALEN) == 0;
    memcpy(peer.peer_addr, broadcast ? BROADCAST_MAC : droneMac, ESP_NOW_ETH_ALEN);
    peer.channel = 0;                 // the current channel
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
}

static void loadConfig(void)
{
    nvs_handle_t nvs;
    uint8_t mac[ESP_NOW_ETH_ALEN] = {0};
    uint8_t chan = DEFAULT_CHANNEL;
    size_t len = sizeof(mac);

    if (nvs_open("bridge", NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_blob(nvs, "drone_mac", mac, &len);
        nvs_get_u8(nvs, "channel", &chan);
        nvs_close(nvs);
    }
    if (chan < 1 || chan > 13) {
        chan = DEFAULT_CHANNEL;
    }
    setDrone(mac, chan);
}

static void saveConfig(void)
{
    nvs_handle_t nvs;
    if (nvs_open("bridge", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_blob(nvs, "drone_mac", droneMac, sizeof(droneMac));
        nvs_set_u8(nvs, "channel", channel);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static void handleDroneFrame(const uint8_t *data, int len)
{
    if (frameValid(data, len) && (data[2] == ESPNOW_LINK_PKT_STATUS || data[2] == ESPNOW_LINK_PKT_TELEMETRY)) {
        rxFromDrone++;
        lastDroneTick = xTaskGetTickCount();
        queueToHost(data, len);
    }
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
static void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    handleDroneFrame(data, len);
}
#else
static void onReceive(const uint8_t *mac, const uint8_t *data, int len)
{
    handleDroneFrame(data, len);
}
#endif

/* ------------------------------------------------------------------ host side */

static void handleHostFrame(const uint8_t *f, int len)
{
    if (f[2] == ESPNOW_LINK_PKT_CONTROL) {
        const TickType_t now = xTaskGetTickCount();
        if (hostActive && now - lastHostTick > pdMS_TO_TICKS(BRIDGE_HOST_TIMEOUT_MS)) {
            hostTimeouts++;
        }
        lastHostTick = now;
        hostActive = true;

        const bool broadcast = memcmp(droneMac, ZERO_MAC, ESP_NOW_ETH_ALEN) == 0;
        if (esp_now_send(broadcast ? BROADCAST_MAC : droneMac, f, len) == ESP_OK) {
            txOk++;
        } else {
            txFail++;
        }
    } else if (f[2] == BRIDGE_PKT_CONFIG) {
        bridgeConfig_t cfg;
        memcpy(&cfg, f, sizeof(cfg));
        if (cfg.channel >= 1 && cfg.channel <= 13) {
            setDrone(cfg.droneMac, cfg.channel);
            if (cfg.flags & BRIDGE_CONFIG_SAVE) {
                saveConfig();
            }
        }
    }
}

static void hostRxTask(void *param)
{
    static uint8_t buf[2 * MAX_FRAME];
    int fill = 0;

    while (1) {
        int n = serialRead(buf + fill, sizeof(buf) - fill, pdMS_TO_TICKS(20));
        if (n > 0) {
            fill += n;
        }
        // Take every complete frame out of the buffer, resynchronise on bad bytes
        while (fill > 0) {
            if (buf[0] != ESPNOW_LINK_MAGIC) {
                memmove(buf, buf + 1, --fill);
                continue;
            }
            if (fill < 3) {
                break;
            }
            const int len = frameLength(buf[2]);
            if (len == 0 || (buf[2] != ESPNOW_LINK_PKT_CONTROL && buf[2] != BRIDGE_PKT_CONFIG)) {
                memmove(buf, buf + 1, --fill);
                continue;
            }
            if (fill < len) {
                break;
            }
            if (frameValid(buf, len)) {
                handleHostFrame(buf, len);
                fill -= len;
                memmove(buf, buf + len, fill);
            } else {
                memmove(buf, buf + 1, --fill);
            }
        }
        if (hostActive && xTaskGetTickCount() - lastHostTick > pdMS_TO_TICKS(BRIDGE_HOST_TIMEOUT_MS)) {
            hostActive = false;   // nothing is sent until the host is back
        }
    }
}

static void hostTxTask(void *param)
{
    outFrame_t out;
    TickType_t lastInfo = 0;

    while (1) {
        if (xQueueReceive(outQueue, &out, pdMS_TO_TICKS(50)) == pdTRUE) {
            serialWrite(out.data, out.len);
        }
        const TickType_t now = xTaskGetTickCount();
        if (now - lastInfo >= pdMS_TO_TICKS(INFO_PERIOD_MS)) {
            lastInfo = now;
            bridgeInfo_t info = {
                .magic = ESPNOW_LINK_MAGIC,
                .version = ESPNOW_LINK_VERSION,
                .type = BRIDGE_PKT_INFO,
                .flags = (memcmp(droneMac, ZERO_MAC, ESP_NOW_ETH_ALEN) ? BRIDGE_INFO_UNICAST : 0) |
                         (hostActive ? BRIDGE_INFO_HOST_ACTIVE : 0) |
                         (now - lastDroneTick < pdMS_TO_TICKS(DRONE_TIMEOUT_MS) && rxFromDrone ? BRIDGE_INFO_DRONE_ACTIVE : 0),
                .channel = channel,
                .txOk = txOk,
                .txFail = txFail,
                .hostTimeouts = hostTimeouts,
                .rxFromDrone = rxFromDrone,
            };
            memcpy(info.staMac, staMac, sizeof(staMac));
            memcpy(info.droneMac, droneMac, sizeof(droneMac));
            info.crc = espnowLinkCrc16((const uint8_t *)&info, sizeof(info) - sizeof(info.crc));
            serialWrite((const uint8_t *)&info, sizeof(info));
        }
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
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, staMac));
    ESP_LOGW(TAG, "STA MAC for the drone's ESPNOW_CTRL_PEER_MAC: %02x:%02x:%02x:%02x:%02x:%02x",
             staMac[0], staMac[1], staMac[2], staMac[3], staMac[4], staMac[5]);

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(onReceive));
    loadConfig();

    // From here on the serial port carries binary frames only
    esp_log_level_set("*", ESP_LOG_NONE);
    outQueue = xQueueCreate(16, sizeof(outFrame_t));
    serialInit();

    xTaskCreate(hostRxTask, "hostRx", 4096, NULL, 6, NULL);
    xTaskCreate(hostTxTask, "hostTx", 3072, NULL, 5, NULL);
}
