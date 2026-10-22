#include <stdint.h>
#include <stdbool.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_NOW_ETH_ALEN 6
typedef enum { WIFI_IF_STA, WIFI_IF_AP } wifi_interface_t;
typedef struct { uint8_t peer_addr[6]; uint8_t lmk[16]; uint8_t channel; wifi_interface_t ifidx; bool encrypt; void *priv; } esp_now_peer_info_t;
typedef void (*esp_now_recv_cb_t)(const uint8_t *mac_addr, const uint8_t *data, int data_len);
esp_err_t esp_now_init(void); esp_err_t esp_now_deinit(void);
esp_err_t esp_now_register_recv_cb(esp_now_recv_cb_t cb);
esp_err_t esp_now_add_peer(const esp_now_peer_info_t *p); esp_err_t esp_now_del_peer(const uint8_t *a);
bool esp_now_is_peer_exist(const uint8_t *a);
esp_err_t esp_now_send(const uint8_t *a, const uint8_t *d, size_t l); esp_err_t esp_now_set_pmk(const uint8_t *k);
