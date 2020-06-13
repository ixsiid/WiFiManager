# WiFi Manager

WiFi Manager for ESP-IDF

It's a very simple to use.

```cpp:main.c
#include <wifiManager.hpp>

#include "wifi_credential.h"

void app_main() {
	ESP_LOGI("Start");

	WiFi::Connect(SSID, PASSWORD);
	ESP_LOGI("IP: %s", inet_ntoa(*WiFi::getIp()));

	ESP_LOGI("Finish");
}

```
