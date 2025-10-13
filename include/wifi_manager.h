// 原理说明：Wi-Fi 管理模块封装连接与状态查询函数，减轻业务逻辑对底层库的耦合。
#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>

namespace wifi_manager {

void connectToNetwork(const char* ssid, const char* password, uint32_t timeoutMs = 15000);
bool isConnected();
IPAddress localIP();

}  // namespace wifi_manager
