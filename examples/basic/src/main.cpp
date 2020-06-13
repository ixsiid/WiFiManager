#include <wifiManager.hpp>

#include "wifi_credential.h"
/*
#define SSID "your ssid"
#define PASSWORD "your password"
*/

#define TAG "WiFi Manager sample"
#include "log.h"
extern "C" {
void app_main();
}

void app_main() {
	_i("Start");

	WiFi::Connect(SSID, PASSWORD);
	_i("IP: %s", inet_ntoa(*WiFi::getIp()));

	_i("Finish");
}
