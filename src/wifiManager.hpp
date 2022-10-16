#ifndef __UTILITY_WIFI_H
#define __UTILITY_WIFI_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <esp_wifi.h>
#include <esp_event.h>
#include <lwip/err.h>
#include <lwip/sys.h>

class WiFi {
    private:
	WiFi();
	static bool initialized;
	static esp_ip4_addr_t ip;
	static bool connected;

	static int s_retry_num;


	static EventGroupHandle_t s_wifi_event_group;
	static esp_event_handler_instance_t instance_any_id;
	static esp_event_handler_instance_t instance_got_ip;

	static void event_handler(void* arg, esp_event_base_t event_base,
						 int32_t event_id, void* event_data);

    public:
	static esp_err_t Connect(const char* ssid, const char* password);
	static bool Disconnect(bool release = false);
	static esp_ip4_addr_t* getIp();
	static const char * get_address();
};

#endif  // __UTILITY_WIFI_H
