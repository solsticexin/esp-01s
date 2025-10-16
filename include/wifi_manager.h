// 原理说明：Wi-Fi 管理模块封装热点模式的启停与状态查询，减轻业务逻辑对底层库的耦合。
#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>

namespace wifi_manager {

void startAccessPoint(const char* ssid, const char* password = nullptr);
bool isConnected();
IPAddress localIP();

}  // namespace wifi_manager
