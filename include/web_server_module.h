// 原理说明：Web 服务模块负责托管静态页面、提供 REST 接口并在网页与串口间转发 NDJSON 消息，实现可视化控制。
#pragma once

#include <Arduino.h>

namespace web_server_module {

void start(uint16_t port);
void loop();
bool isRunning();
void handleSerialLine(const String& line);

}  // namespace web_server_module
