// 原理说明：封装 ESP8266WiFi 接口以启动热点模式并对外提供运行状态。
#include "wifi_manager.h"

#include <ESP8266WiFi.h>
#include <cstring>

namespace wifi_manager {

namespace {
bool apRunning = false;
}  // namespace

void startAccessPoint(const char* ssid, const char* password) {
  WiFi.mode(WIFI_AP);
  WiFi.softAPdisconnect(true);
  apRunning = false;

  if (password != nullptr && password[0] != '\0' && strlen(password) >= 8) {
    apRunning = WiFi.softAP(ssid, password);
  } else {
    // Password shorter than 8 characters disables WPA2 on ESP8266, fall back to open AP.
    apRunning = WiFi.softAP(ssid);
  }
}

bool isConnected() {
  return apRunning;
}

IPAddress localIP() {
  return WiFi.softAPIP();
}

}  // namespace wifi_manager
