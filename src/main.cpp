// 原理说明：主程序负责初始化各模块、周期性维护 Wi-Fi 与串口通信状态，并驱动 Web 服务器与协议中转逻辑。
#include <Arduino.h>

#include "device_config.h"
#include "serial_bridge.h"
#include "web_server_module.h"
#include "wifi_manager.h"

namespace {

constexpr uint32_t kReconnectIntervalMs = 5000;
unsigned long lastReconnectAttempt = 0;
bool lastWifiConnected = false;
IPAddress lastIpReported(0, 0, 0, 0);

IPAddress currentStatusIp() {
  if (wifi_manager::isConnected()) {
    return wifi_manager::localIP();
  }
  return IPAddress(0, 0, 0, 0);
}

void reportNetworkStatusIfChanged() {
  const bool connected = wifi_manager::isConnected();
  const IPAddress current_ip = currentStatusIp();
  if (connected != lastWifiConnected || current_ip != lastIpReported) {
    serial_bridge::sendStatusMessage(current_ip);
    lastWifiConnected = connected;
    lastIpReported = current_ip;
  }
}

}  // namespace

void setup() {
  serial_bridge::begin(Serial, device_config::STM32_SERIAL_BAUD);
  Serial.println();
  Serial.println(F("智能盆栽通信终端启动中..."));

  serial_bridge::setMessageHandler(web_server_module::handleSerialLine);

  wifi_manager::connectToNetwork(device_config::WIFI_SSID, device_config::WIFI_PASSWORD);
  if (wifi_manager::isConnected()) {
    Serial.print(F("Wi-Fi 已连接，IP: "));
    Serial.println(wifi_manager::localIP());
  } else {
    Serial.println(F("Wi-Fi 连接失败或超时，将持续重试。"));
  }

  web_server_module::start(device_config::WEB_SERVER_PORT);
  Serial.println(F("Web 服务已启动。"));

  reportNetworkStatusIfChanged();
}

void loop() {
  serial_bridge::loop();
  web_server_module::loop();

  reportNetworkStatusIfChanged();

  if (!wifi_manager::isConnected()) {
    const unsigned long now = millis();
    if (now - lastReconnectAttempt > kReconnectIntervalMs) {
      wifi_manager::connectToNetwork(device_config::WIFI_SSID, device_config::WIFI_PASSWORD);
      lastReconnectAttempt = now;
    }
  }

  delay(10);
}
