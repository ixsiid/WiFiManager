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

DPP mode is reference from: https://dev.classmethod.jp/articles/try-wi-fi-easy-connect-with-esp32/


```sdkconfig
CONFIG_WPA_DPP_SUPPORT=y
```

```cpp:main.c
#include <wifiManager.hpp>

void app_main() {
	ESP_LOGI("Start");

	WiFi::wait_connection();
	ESP_LOGI("IP: %s", inet_ntoa(*WiFi::getIp()));

	ESP_LOGI("Finish");
}
```

```console
I (1380) WiFi Manager: DPP:C:81/6;M:xx:xx:xx:xx:xx:xx;K:MDkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDIgADJoeKKbJhp+PP9oktUc1Jbsk4K6WOPD7cuUV5XHn1Qtg=;;
                       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				   Read your smartphone by QR code
				   
                       ^^^^^^^^10^^^^^^^^20^^^^^^^^30^^^^^^^^40^^^^^^^^50^^^^^^^^60^^^^^^^^70^^^^^^^^80^^^^^^^^90^^^^^^^100^^^^^^^110^^115
```
