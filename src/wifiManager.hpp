#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <esp_wifi.h>
#include <esp_event.h>
#include <lwip/err.h>
#include <lwip/sys.h>

#ifdef CONFIG_WPA_DPP_SUPPORT
#include <esp_dpp.h>
#endif

class WiFi {
    private:
	enum class SetupMode {
		Normal,
		DPP,
	};

	WiFi();
	static bool initialized;
	static esp_ip4_addr_t ip;
	static bool connected;

	static SetupMode mode;
	static int s_retry_num;

	static wifi_config_t wifi_config;

	static EventGroupHandle_t s_wifi_event_group;
	static esp_event_handler_instance_t instance_any_id;
	static esp_event_handler_instance_t instance_got_ip;

	static void event_handler(void* arg, esp_event_base_t event_base,
						 int32_t event_id, void* event_data);

	static esp_err_t initialize(SetupMode mode, const char* ssid = nullptr, const char* password = nullptr);

    public:
	static esp_err_t Connect(const char* ssid, const char* password);
	static bool Disconnect(bool release = false);
	static esp_ip4_addr_t* getIp();
	static const char* get_address();

#ifdef CONFIG_WPA_DPP_SUPPORT
    public:
	typedef void (*pairing_text_callback_t)(const char* pairing_text);

    private:
	static pairing_text_callback_t callback;
	static void dpp_enrollee_event_cb(esp_supp_dpp_event_t event, void* data);

    public:
	static esp_err_t wait_connection(pairing_text_callback_t callback = nullptr);
#endif
};
