#include "wifiManager.hpp"

#include <lwip/inet.h>
#include <stdbool.h>
#include <string.h>

#define TAG "WiFi Manager"
#include "log.h"

#undef CONFIG_ESP_WIFI_AUTH_OPEN
#undef CONFIG_ESP_WIFI_AUTH_WEP
#define CONFIG_ESP_WIFI_AUTH_WPA_PSK 1
#undef CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#undef CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK 1

#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

bool WiFi::initialized = false;
bool WiFi::connected = false;

esp_ip4_addr_t WiFi::ip;

/* FreeRTOS event group to signal when we are connected*/
EventGroupHandle_t WiFi::s_wifi_event_group;
esp_event_handler_instance_t WiFi::instance_any_id;
esp_event_handler_instance_t WiFi::instance_got_ip;

int WiFi::s_retry_num = 0;

void WiFi::event_handler(void *arg, esp_event_base_t event_base,
					int32_t event_id, void *event_data) {
	const int maximum_retry = 5;
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		esp_wifi_connect();
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
		ESP_LOGI(TAG, "SSID: %s, length: %d, BSSID: %2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:, reason: %d",
			event->ssid, event->ssid_len,
			event->bssid[0], event->bssid[1], event->bssid[2], 
			event->bssid[3], event->bssid[4], event->bssid[5], event->reason);
		if (s_retry_num < maximum_retry) {
			esp_wifi_connect();
			s_retry_num++;
			ESP_LOGI(TAG, "retry to connect to the AP");
		} else {
			xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
		}
		ESP_LOGI(TAG, "connect to the AP fail");
	} else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
		ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
		s_retry_num = 0;
		memcpy(&ip, &event->ip_info.ip, sizeof(esp_ip4_addr_t));
		connected = true;
		xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
	}
}

WiFi::WiFi() {
	initialized = false;

	s_wifi_event_group = xEventGroupCreate();

	ESP_ERROR_CHECK(esp_netif_init());

	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_create_default_wifi_sta();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
											  ESP_EVENT_ANY_ID,
											  &event_handler,
											  NULL,
											  &instance_any_id));
	ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
											  IP_EVENT_STA_GOT_IP,
											  &event_handler,
											  NULL,
											  &instance_got_ip));

	initialized = true;
}

esp_err_t WiFi::Connect(const char *ssid, const char *password) {
	if (!initialized) {
		_v("WiFi initializing");
		WiFi();
		if (!initialized) return false;
	}

	wifi_config_t wifi_config;
	memset(&wifi_config, 0, sizeof(wifi_config_t));
	wifi_config.sta.threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD;
	strncpy((char *)&wifi_config.sta.ssid, ssid, 31);
	strncpy((char *)&wifi_config.sta.password, password, 63);

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
	ESP_ERROR_CHECK(esp_wifi_start());

	ESP_LOGI(TAG, "wifi_init_sta finished.");

	/* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
	 * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
	EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
								    WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
								    pdFALSE,
								    pdFALSE,
								    portMAX_DELAY);

	/* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
	 * happened. */
	if (bits & WIFI_CONNECTED_BIT) {
		ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
			    ssid, password);
		return 0;
	} else if (bits & WIFI_FAIL_BIT) {
		ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
			    ssid, password);
		return 1;
	} else {
		ESP_LOGE(TAG, "UNEXPECTED EVENT");
		return 2;
	}

	return 3;
}

bool WiFi::Disconnect(bool release) {
	esp_err_t err;

	err = esp_wifi_disconnect();
	if (err) {
		_e("WiFi disconnect error %d", err);
		return false;
	}

	err = esp_wifi_stop();
	if (err) {
		_e("WiFi stop error %d", err);
		return false;
	}

	err = esp_wifi_deinit();
	if (err) {
		_e("WiFi de-initialize error %d", err);
		return false;
	}

	return true;
}

esp_ip4_addr_t *WiFi::getIp() {
	if (!connected) return nullptr;
	return &ip;
}

const char *WiFi::get_address() {
	if (!connected) return nullptr;
	return inet_ntoa(ip);
}
