// 原理说明：设备配置模块集中管理网络与硬件常量，便于维护和统一修改。
#pragma once

#include <Arduino.h>

namespace device_config {

extern const char WIFI_SSID[];
extern const char WIFI_PASSWORD[];
constexpr uint16_t WEB_SERVER_PORT = 80;
constexpr unsigned long STM32_SERIAL_BAUD = 115200;

}  // namespace device_config
