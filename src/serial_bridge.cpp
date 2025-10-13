// 原理说明：本文件实现串口行缓冲与 JSON 打包发送，以最小内存代价可靠转发 NDJSON 通信内容。
#include "serial_bridge.h"

#include <HardwareSerial.h>

namespace serial_bridge {
namespace {

HardwareSerial* port = nullptr;
MessageHandler message_handler = nullptr;
String rx_buffer;

void dispatchBuffer() {
  if (rx_buffer.length() == 0) {
    return;
  }
  if (message_handler != nullptr) {
    message_handler(rx_buffer);
  }
  rx_buffer = "";
}

}  // namespace

void begin(HardwareSerial& serial_port, unsigned long baud_rate) {
  port = &serial_port;
  port->begin(baud_rate);
  rx_buffer.reserve(256);
}

void setMessageHandler(MessageHandler handler) {
  message_handler = handler;
}

void loop() {
  if (port == nullptr) {
    return;
  }

  while (port->available() > 0) {
    const char ch = static_cast<char>(port->read());
    if (ch == '\n') {
      dispatchBuffer();
    } else if (ch == '\r') {
      continue;
    } else {
      if (rx_buffer.length() < 512) {
        rx_buffer += ch;
      } else {
        // 缓冲区溢出时丢弃当前行，避免占用过多内存。
        rx_buffer = "";
      }
    }
  }
}

bool sendJson(const JsonDocument& doc) {
  if (port == nullptr) {
    return false;
  }
  const size_t written = serializeJson(doc, *port);
  if (written == 0) {
    return false;
  }
  port->write('\n');
  port->flush();
  return true;
}

bool sendRawLine(const String& line) {
  if (port == nullptr) {
    return false;
  }
  if (line.length() == 0) {
    return false;
  }
  port->print(line);
  port->write('\n');
  port->flush();
  return true;
}

bool sendStatusMessage(const IPAddress& ip) {
  StaticJsonDocument<96> doc;
  doc["type"] = "status";
  doc["ip"] = ip.toString();
  return sendJson(doc);
}

}  // namespace serial_bridge
