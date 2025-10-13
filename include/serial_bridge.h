// 原理说明：串口桥接模块负责缓存并转发 STM32 与 ESP-01S 之间的 NDJSON 行消息，统一管理收发流程，降低主循环负担。
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <IPAddress.h>

namespace serial_bridge {

using MessageHandler = void (*)(const String& json_line);

void begin(HardwareSerial& serial_port, unsigned long baud_rate);
void loop();
void setMessageHandler(MessageHandler handler);
bool sendJson(const JsonDocument& doc);
bool sendRawLine(const String& line);
bool sendStatusMessage(const IPAddress& ip);

}  // namespace serial_bridge
