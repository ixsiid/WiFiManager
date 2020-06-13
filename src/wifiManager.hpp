#ifndef __UTILITY_WIFI_H
#define __UTILITY_WIFI_H

#include <lwip/inet.h>

class WiFi {
    private:
	WiFi();
	static bool initialized;
	static ip4_addr_t ip;
	static ip4_addr_t gateway;
	static ip4_addr_t subnetmask;
	static bool connected;

    public:
	static bool Connect(const char* ssid, const char* password);
	static bool Disconnect(bool release = false);
	static ip4_addr_t *getIp();
};

#endif // __UTILITY_WIFI_H
