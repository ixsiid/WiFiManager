#include "wifiManager.hpp"

#include <lwip/inet.h>
#include <stdbool.h>
#include <string.h>

#include <esp_log.h>

#define TAG "WiFi Manager"

#undef CONFIG_ESP_WIFI_AUTH_OPEN
#undef CONFIG_ESP_WIFI_AUTH_WEP
#define CONFIG_ESP_WIFI_AUTH_WPA_PSK
#undef CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#undef CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK

#if defined(CONFIG_ESP_WIFI_AUTH_OPEN)
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif defined(CONFIG_ESP_WIFI_AUTH_WEP)
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif defined(CONFIG_ESP_WIFI_AUTH_WPA_PSK)
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif defined(CONFIG_ESP_WIFI_AUTH_WPA2_PSK)
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif defined(CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK)
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif defined(CONFIG_ESP_WIFI_AUTH_WPA3_PSK)
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif defined(CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK)
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif defined(CONFIG_ESP_WIFI_AUTH_WAPI_PSK)
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_AUTH_FAIL_BIT BIT2

bool WiFi::initialized = false;
bool WiFi::connected = false;

esp_ip4_addr_t WiFi::ip;

/* FreeRTOS event group to signal when we are connected*/
EventGroupHandle_t WiFi::s_wifi_event_group;
esp_event_handler_instance_t WiFi::instance_any_id;
esp_event_handler_instance_t WiFi::instance_got_ip;

int WiFi::s_retry_num = 0;
WiFi::SetupMode WiFi::mode = SetupMode::Normal;
wifi_config_t WiFi::wifi_config = {};

void WiFi::event_handler(void *arg, esp_event_base_t event_base,
					int32_t event_id, void *event_data) {
	const int maximum_retry = 5;
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		switch(mode) {
			case WiFi::SetupMode::Normal:
				ESP_ERROR_CHECK(esp_wifi_connect());
				ESP_LOGI(TAG, "STA starting");
			break;
			case WiFi::SetupMode::DPP:
				ESP_ERROR_CHECK(esp_supp_dpp_start_listen());
				ESP_LOGI(TAG, "Started listening for DPP Authentication");
			break;
		}
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
};

#ifdef CONFIG_WPA_DPP_SUPPORT
WiFi::pairing_text_callback_t WiFi::callback = nullptr;

void WiFi::dpp_enrollee_event_cb(esp_supp_dpp_event_t event, void *data) {
	switch (event) {
		case ESP_SUPP_DPP_URI_READY:
			if (data != NULL) {
				// esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();

				ESP_LOGI(TAG, "Scan below QR Code to configure the enrollee:\n");
				//esp_qrcode_generate(&cfg, (const char *)data);

				ESP_LOG_BUFFER_HEXDUMP(TAG, data, 32, esp_log_level_t::ESP_LOG_ERROR);
				const char * qr_text = static_cast<const char *>(data);
				ESP_LOGI(TAG, "%s", qr_text);
				if (callback) callback(qr_text);
			}
			break;
		case ESP_SUPP_DPP_CFG_RECVD:
			memcpy(&wifi_config, data, sizeof(wifi_config));
			esp_wifi_set_config(static_cast<wifi_interface_t>(ESP_IF_WIFI_STA), &wifi_config);
			ESP_LOGI(TAG, "DPP Authentication successful, connecting to AP : %s",
				    wifi_config.sta.ssid);
			s_retry_num = 0;
			esp_wifi_connect();
			break;
		case ESP_SUPP_DPP_FAIL:
			if (s_retry_num < 5) {
				ESP_LOGI(TAG, "DPP Auth failed (Reason: %s), retry...", esp_err_to_name((int)data));
				ESP_ERROR_CHECK(esp_supp_dpp_start_listen());
				s_retry_num++;
			} else {
				xEventGroupSetBits(s_wifi_event_group, WIFI_AUTH_FAIL_BIT);
			}
			break;
		default:
			break;
	}
};

esp_err_t WiFi::wait_connection(pairing_text_callback_t callback) {
	if (initialized) {
		ESP_LOGE(TAG, "WiFi is Initialized");
		return 12;
	}

	WiFi::callback = callback;

	return initialize(SetupMode::DPP);
}
#endif


esp_err_t WiFi::initialize(SetupMode mode, const char *ssid, const char *password) {
	initialized = false;
	WiFi::mode = mode;

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
	
	switch(mode) {
		case SetupMode::Normal:
			memset(&wifi_config, 0, sizeof(wifi_config_t));
			wifi_config.sta.threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD;
			strncpy((char *)&wifi_config.sta.ssid, ssid, 31);
			strncpy((char *)&wifi_config.sta.password, password, 63);
			break;
#ifdef CONFIG_WPA_DPP_SUPPORT

#ifdef CONFIG_ESP_DPP_LISTEN_CHANNEL
#define EXAMPLE_DPP_LISTEN_CHANNEL_LIST CONFIG_ESP_DPP_LISTEN_CHANNEL_LIST
#else
#define EXAMPLE_DPP_LISTEN_CHANNEL_LIST "6"
#endif

#ifdef CONFIG_ESP_DPP_BOOTSTRAPPING_KEY
#define EXAMPLE_DPP_BOOTSTRAPPING_KEY CONFIG_ESP_DPP_BOOTSTRAPPING_KEY
#else
#define EXAMPLE_DPP_BOOTSTRAPPING_KEY 0
#endif

#ifdef CONFIG_ESP_DPP_DEVICE_INFO
#define EXAMPLE_DPP_DEVICE_INFO CONFIG_ESP_DPP_DEVICE_INFO
#else
#define EXAMPLE_DPP_DEVICE_INFO 0
#endif
		case SetupMode::DPP:
			ESP_ERROR_CHECK(esp_supp_dpp_init(dpp_enrollee_event_cb));
			/* Currently only supported method is QR Code */
			ESP_ERROR_CHECK(esp_supp_dpp_bootstrap_gen(EXAMPLE_DPP_LISTEN_CHANNEL_LIST, DPP_BOOTSTRAP_QR_CODE,
									   EXAMPLE_DPP_BOOTSTRAPPING_KEY, EXAMPLE_DPP_DEVICE_INFO));
			break;
#endif
		default:
			ESP_LOGE(TAG, "Not implements mode: %d", static_cast<int>(mode));
			return 5;
	}


	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	if (mode == SetupMode::Normal) ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
	ESP_ERROR_CHECK(esp_wifi_start());

	ESP_LOGI(TAG, "wifi_init_sta finished.");

	/* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
	 * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
	EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
								    WIFI_CONNECTED_BIT | WIFI_FAIL_BIT | WIFI_AUTH_FAIL_BIT,
								    pdFALSE,
								    pdFALSE,
								    portMAX_DELAY);

	/* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
	 * happened. */
	if (bits & WIFI_CONNECTED_BIT) {
		ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
			    wifi_config.sta.ssid, wifi_config.sta.password);
		return 0;
	}
	if (bits & WIFI_FAIL_BIT) {
		ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
			    wifi_config.sta.ssid, wifi_config.sta.password);
		return 1;
	}
	if (bits & WIFI_AUTH_FAIL_BIT) {
		ESP_LOGI(TAG, "DPP Authentication failed after %d retries", s_retry_num);
		return 2;
	}

	ESP_LOGE(TAG, "UNEXPECTED EVENT");
	return 3;
};

esp_err_t WiFi::Connect(const char *ssid, const char *password) {
	if (initialized) {
		ESP_LOGE(TAG, "WiFi is Initialized");
		return 12;
	}

	return initialize(SetupMode::Normal, ssid, password);
}

bool WiFi::Disconnect(bool release) {
	esp_err_t err;

	err = esp_wifi_disconnect();
	if (err) {
		ESP_LOGE(TAG, "WiFi disconnect error %d", err);
		return false;
	}

	err = esp_wifi_stop();
	if (err) {
		ESP_LOGE(TAG, "WiFi stop error %d", err);
		return false;
	}

	err = esp_wifi_deinit();
	if (err) {
		ESP_LOGE(TAG, "WiFi de-initialize error %d", err);
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
