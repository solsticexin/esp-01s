// 原理说明：通过封装 ESP8266WiFi 库的方法，实现非阻塞超时机制的网络连接与状态查询。
#include "wifi_manager.h"

#include <ESP8266WiFi.h>

namespace wifi_manager {

void connectToNetwork(const char* ssid, const char* password, uint32_t timeoutMs) {
  WiFi.mode(WIFI_STA);
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  WiFi.begin(ssid, password);

  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
    if (timeoutMs > 0 && (millis() - start) > timeoutMs) {
      // 超时后立即跳出，让上层逻辑决定下一步动作。
      break;
    }
  }
}

bool isConnected() {
  return WiFi.status() == WL_CONNECTED;
}

IPAddress localIP() {
  return WiFi.localIP();
}

}  // namespace wifi_manager
