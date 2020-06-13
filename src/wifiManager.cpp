#include "wifiManager.hpp"

#include <esp_event.h>
#include <esp_wifi.h>
#include <lwip/inet.h>
#include <nvs_flash.h>
#include <stdbool.h>
#include <string.h>

#define TAG "WiFi Manager"
#include "log.h"

bool WiFi::initialized = false;

ip4_addr_t WiFi::ip;
ip4_addr_t WiFi::gateway;
ip4_addr_t WiFi::subnetmask;
bool WiFi::connected = false;

WiFi::WiFi() {
	esp_err_t err;
	initialized = false;

	_v("set callback");
	err = esp_event_loop_init([](void *ctx, system_event_t *event) {
		_v("WiFi Event loop %d", event->event_id);
		if (event->event_id == SYSTEM_EVENT_STA_GOT_IP) {
			ip		 = event->event_info.got_ip.ip_info.ip;
			gateway	 = event->event_info.got_ip.ip_info.gw;
			subnetmask = event->event_info.got_ip.ip_info.netmask;
			connected	 = true;
		}
		return ESP_OK;
	},
						 NULL);
	if (err) {
		_v("Event loop initialize error %d", err);
		return;
	}

	_v("NVS initialize");
	err = nvs_flash_init();
	if (err) {
		_v("NVS initialize error %d", err);
		return;
	}

	wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
	err						 = esp_wifi_init(&wifi_config);
	if (err) {
		_v("WiFi initialize error %d", err);
		return;
	}

	_v("Set wifi mode to STA");
	err = esp_wifi_set_mode(WIFI_MODE_STA);
	if (err) {
		_v("WiFi set mode error %d", err);
		return;
	}

	// esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
	// esp_wifi_set_country({.cc = "JP", .schan=1, .nchan=14})

	initialized = true;
}

bool WiFi::Connect(const char *ssid, const char *password) {
	esp_err_t err;
	if (!initialized) {
		_v("WiFi initializing");
		WiFi();
		if (!initialized) return false;
	}

	_v("Connecting WiFi");
	wifi_config_t config;
	memset(&config, 0, sizeof(config));
	strcpy((char *)config.sta.ssid, ssid);
	strcpy((char *)config.sta.password, password);
	// config.sta.scan_method = WIFI_FAST_SCAN;
	// config.sta.bssid_set = false; // APのMACアドレスをチェックしない
	// config.sta.channel = 0; // APのチャンネルが不明
	_v("Set config");
	err = esp_wifi_set_config(WIFI_IF_STA, &config);
	if (err) {
		_v("WiFi set config error %d", err);
		return false;
	}

	_v("tcp/ip adapter initializing");
	tcpip_adapter_init();

	_v("start wifi");
	err = esp_wifi_start();
	if (err) {
		_v("WiFi start error %d", err);
		return false;
	}

	_v("connect wifi");
	err = esp_wifi_connect();
	if (err) {
		_v("WiFi connect error %d", err);
		return false;
	}

	while (!connected) vTaskDelay(10 / portTICK_PERIOD_MS);

	return true;
}

bool WiFi::Disconnect(bool release) {
	esp_err_t err;

	err = esp_wifi_disconnect();
	if (err) {
		_v("WiFi disconnect error %d", err);
		return false;
	}

	err = esp_wifi_stop();
	if (err) {
		_v("WiFi stop error %d", err);
		return false;
	}

	err = esp_wifi_deinit();
	if (err) {
		_v("WiFi de-initialize error %d", err);
		return false;
	}

	return true;
}

ip4_addr_t *WiFi::getIp() {
	if (!connected) return nullptr;
	return &ip;
}
