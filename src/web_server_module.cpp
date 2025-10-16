// 原理说明：Web 服务模块通过 ESP8266WebServer 提供静态资源与 REST 接口，并维护消息缓冲，实现网页与 STM32 间的 NDJSON 中转。
#include "web_server_module.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WebServer.h>
#include <FS.h>
#include <LittleFS.h>
#include <math.h>

#include <vector>

#include "serial_bridge.h"
#include "wifi_manager.h"

namespace web_server_module {
namespace {

struct SensorSnapshot {
  bool valid = false;
  float temp = 0.0f;
  float humi = 0.0f;
  int soil = 0;
  float lux = 0.0f;
  uint8_t water = 0;
  uint8_t light = 0;
  uint8_t fan = 0;
  uint8_t buzzer = 0;
  unsigned long updated_at = 0;
};

struct AckSnapshot {
  bool valid = false;
  String target;
  String action;
  String result;
  unsigned long updated_at = 0;
};

struct NumericThreshold {
  bool enabled = false;
  float value = 0.0f;
};

struct ThresholdConfig {
  NumericThreshold temp;
  NumericThreshold humi;
  NumericThreshold soil;
  NumericThreshold lux;
};

struct AlarmState {
  unsigned long lastTriggeredAt = 0;
  String reason;
  uint32_t count = 0;
};

struct MessageEntry {
  uint32_t id = 0;
  String payload;
};

ESP8266WebServer* server = nullptr;
bool littleFsMounted = false;
SensorSnapshot latest_sensor;
AckSnapshot last_ack;
ThresholdConfig threshold_config;
AlarmState alarm_state;
std::vector<MessageEntry> message_log;
uint32_t last_message_id = 0;
constexpr size_t kMaxMessages = 32;
constexpr unsigned long kAlarmCooldownMs = 15000;
constexpr uint16_t kAlarmPulseMs = 3000;
unsigned long lastAlarmCommandMs = 0;
String last_reported_ip("0.0.0.0");

String buildFallbackPage() {
  String html;
  html.reserve(512);
  html += F("<!DOCTYPE html><html lang=\"zh\"><head><meta charset=\"UTF-8\">");
  html += F("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
  html += F("<title>ESP-01S 控制台</title>");
  html += F("<style>body{font-family:Arial,sans-serif;margin:2rem;background:#f4f4f4;}");
  html += F(".card{background:#fff;padding:1.5rem;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1);max-width:420px;}");
  html += F("h1{font-size:1.5rem;margin-bottom:1rem;}p{margin:0.25rem 0;font-size:0.95rem;}");
  html += F("</style></head><body><div class=\"card\"><h1>ESP-01S 控制台</h1>");
  html += F("<p><strong>热点状态:</strong> ");
  html += wifi_manager::isConnected() ? F("已启用") : F("未启用");
  html += F("</p><p><strong>IP 地址:</strong> ");
  html += wifi_manager::localIP().toString();
  html += F("</p><p><strong>运行时间:</strong> ");
  html += String(millis() / 1000);
  html += F(" 秒</p><p>LittleFS 未挂载，网页资源不可用，已回退到内置状态页。</p></div></body></html>");
  return html;
}

void addMessage(const String& line) {
  MessageEntry entry;
  entry.id = ++last_message_id;
  entry.payload = line;
  message_log.push_back(entry);
  if (message_log.size() > kMaxMessages) {
    message_log.erase(message_log.begin());
  }
}

bool anyThresholdEnabled() {
  return threshold_config.temp.enabled || threshold_config.humi.enabled ||
         threshold_config.soil.enabled || threshold_config.lux.enabled;
}

void appendExceedReason(String& reason, const __FlashStringHelper* label, float value, float limit, uint8_t decimals) {
  if (reason.length() > 0) {
    reason += F("；");
  }
  reason += label;
  reason += ' ';
  reason += String(value, decimals);
  reason += F(" > 阈值 ");
  reason += String(limit, decimals);
}

void fillThresholdJson(JsonObject object) {
  auto assignThreshold = [&](const char* key, const NumericThreshold& threshold) {
    if (threshold.enabled) {
      object[key] = threshold.value;
    } else {
      object[key] = nullptr;
    }
  };

  assignThreshold("temp", threshold_config.temp);
  assignThreshold("humi", threshold_config.humi);
  assignThreshold("soil", threshold_config.soil);
  assignThreshold("lux", threshold_config.lux);
}

void fillAlarmJson(JsonObject object) {
  object["count"] = alarm_state.count;
  object["cooldownMs"] = kAlarmCooldownMs;
  object["pulseMs"] = kAlarmPulseMs;
  if (alarm_state.lastTriggeredAt != 0) {
    object["reason"] = alarm_state.reason;
    object["ageMs"] = millis() - alarm_state.lastTriggeredAt;
  } else {
    object["reason"] = nullptr;
    object["ageMs"] = nullptr;
  }
}

bool isNumericVariant(const JsonVariantConst& value) {
  return value.is<int>() || value.is<long>() || value.is<unsigned int>() || value.is<unsigned long>() ||
         value.is<float>() || value.is<double>();
}

bool updateThresholdValue(const char* key,
                          NumericThreshold& target,
                          const JsonVariantConst& value,
                          float minValue,
                          float maxValue,
                          String& error) {
  if (value.isNull()) {
    target.enabled = false;
    return true;
  }

  if (!isNumericVariant(value)) {
    error = String(key) + F(" 必须为数值或 null");
    return false;
  }

  const float numeric = value.as<float>();
  if (isnan(numeric) || numeric < minValue || numeric > maxValue) {
    error = String(key) + F(" 超出范围");
    return false;
  }

  target.enabled = true;
  target.value = numeric;
  return true;
}

void checkAndTriggerAlarm(const JsonDocument& doc) {
  if (!anyThresholdEnabled()) {
    return;
  }

  bool triggered = false;
  String reason;

  if (threshold_config.temp.enabled) {
    JsonVariantConst tempVar = doc["temp"];
    if (!tempVar.isNull()) {
      const float value = tempVar.as<float>();
      if (!isnan(value) && value > threshold_config.temp.value) {
        appendExceedReason(reason, F("温度"), value, threshold_config.temp.value, 1);
        triggered = true;
      }
    }
  }

  if (threshold_config.humi.enabled) {
    JsonVariantConst humiVar = doc["humi"];
    if (!humiVar.isNull()) {
      const float value = humiVar.as<float>();
      if (!isnan(value) && value > threshold_config.humi.value) {
        appendExceedReason(reason, F("湿度"), value, threshold_config.humi.value, 1);
        triggered = true;
      }
    }
  }

  if (threshold_config.soil.enabled) {
    JsonVariantConst soilVar = doc["soil"];
    if (!soilVar.isNull()) {
      const float value = soilVar.as<float>();
      if (!isnan(value) && value > threshold_config.soil.value) {
        appendExceedReason(reason, F("土壤"), value, threshold_config.soil.value, 0);
        triggered = true;
      }
    }
  }

  if (threshold_config.lux.enabled) {
    JsonVariantConst luxVar = doc["lux"];
    if (!luxVar.isNull()) {
      const float value = luxVar.as<float>();
      if (!isnan(value) && value > threshold_config.lux.value) {
        appendExceedReason(reason, F("光照"), value, threshold_config.lux.value, 1);
        triggered = true;
      }
    }
  }

  if (!triggered) {
    return;
  }

  const unsigned long now = millis();
  alarm_state.reason = reason;
  alarm_state.lastTriggeredAt = now;

  if (now - lastAlarmCommandMs < kAlarmCooldownMs) {
    return;
  }

  StaticJsonDocument<128> cmdDoc;
  cmdDoc["type"] = "cmd";
  cmdDoc["target"] = "buzzer";
  cmdDoc["action"] = "pulse";
  cmdDoc["time"] = kAlarmPulseMs;

  String cmdLine;
  serializeJson(cmdDoc, cmdLine);
  if (!serial_bridge::sendJson(cmdDoc)) {
    Serial.println(F("自动报警命令发送失败"));
    return;
  }

  addMessage(cmdLine);
  const uint32_t commandMessageId = last_message_id;

  StaticJsonDocument<192> logDoc;
  logDoc["type"] = "alarm";
  logDoc["reason"] = reason;
  logDoc["triggeredAt"] = now;
  logDoc["relatedMessageId"] = commandMessageId;
  String logLine;
  serializeJson(logDoc, logLine);
  addMessage(logLine);

  lastAlarmCommandMs = now;
  alarm_state.count += 1;
}

void handleThresholdGet() {
  if (!server) {
    return;
  }

  StaticJsonDocument<256> doc;
  doc["ok"] = true;
  fillThresholdJson(doc.createNestedObject("thresholds"));
  fillAlarmJson(doc.createNestedObject("alarm"));

  String response;
  serializeJson(doc, response);
  server->sendHeader(F("Cache-Control"), F("no-store"));
  server->send(200, "application/json", response);
}

void handleThresholdPost() {
  if (!server) {
    return;
  }

  if (!server->hasArg("plain")) {
    server->send(400, "application/json", F("{\"error\":\"缺少 JSON 负载\"}"));
    return;
  }

  const String body = server->arg("plain");
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "application/json", F("{\"error\":\"JSON 解析失败\"}"));
    return;
  }

  String error;
  bool touched = false;

  if (doc.containsKey("temp")) {
    if (!updateThresholdValue("temp", threshold_config.temp, doc["temp"], -40.0f, 125.0f, error)) {
      StaticJsonDocument<96> resp;
      resp["error"] = error;
      String serialized;
      serializeJson(resp, serialized);
      server->send(422, "application/json", serialized);
      return;
    }
    touched = true;
  }

  if (doc.containsKey("humi")) {
    if (!updateThresholdValue("humi", threshold_config.humi, doc["humi"], 0.0f, 100.0f, error)) {
      StaticJsonDocument<96> resp;
      resp["error"] = error;
      String serialized;
      serializeJson(resp, serialized);
      server->send(422, "application/json", serialized);
      return;
    }
    touched = true;
  }

  if (doc.containsKey("soil")) {
    if (!updateThresholdValue("soil", threshold_config.soil, doc["soil"], 0.0f, 100.0f, error)) {
      StaticJsonDocument<96> resp;
      resp["error"] = error;
      String serialized;
      serializeJson(resp, serialized);
      server->send(422, "application/json", serialized);
      return;
    }
    touched = true;
  }

  if (doc.containsKey("lux")) {
    if (!updateThresholdValue("lux", threshold_config.lux, doc["lux"], 0.0f, 200000.0f, error)) {
      StaticJsonDocument<96> resp;
      resp["error"] = error;
      String serialized;
      serializeJson(resp, serialized);
      server->send(422, "application/json", serialized);
      return;
    }
    touched = true;
  }

  if (!touched) {
    server->send(422, "application/json", F("{\"error\":\"缺少阈值字段\"}"));
    return;
  }

  StaticJsonDocument<256> resp;
  resp["ok"] = true;
  fillThresholdJson(resp.createNestedObject("thresholds"));
  fillAlarmJson(resp.createNestedObject("alarm"));
  String serialized;
  serializeJson(resp, serialized);
  server->send(200, "application/json", serialized);
}

void handleFallbackRoot() {
  if (!server) {
    return;
  }
  server->send(200, "text/html", buildFallbackPage());
}

void handleIndexHtml() {
  if (!server) {
    return;
  }

  if (!littleFsMounted) {
    handleFallbackRoot();
    return;
  }

  File file = LittleFS.open("/index.html", "r");
  if (!file) {
    server->send(500, "text/plain", "index.html not found");
    return;
  }

  server->streamFile(file, F("text/html"));
  file.close();
}

void handleMessagesRequest() {
  if (!server) {
    return;
  }

  uint32_t after = 0;
  if (server->hasArg("after")) {
    after = static_cast<uint32_t>(server->arg("after").toInt());
  }

  String body;
  for (const auto& message : message_log) {
    if (message.id > after) {
      body += message.payload;
      body += '\n';
    }
  }

  server->sendHeader(F("Cache-Control"), F("no-store"));
  server->sendHeader(F("X-Last-Message-Id"), String(last_message_id));
  server->send(200, "application/x-ndjson", body);
}

void handleStateRequest() {
  if (!server) {
    return;
  }

  StaticJsonDocument<384> doc;
  JsonObject wifi = doc.createNestedObject("wifi");
  wifi["connected"] = wifi_manager::isConnected();
  wifi["ip"] = wifi_manager::localIP().toString();

  doc["stm32ReportedIp"] = last_reported_ip;
  doc["uptimeSeconds"] = millis() / 1000;

  if (latest_sensor.valid) {
    JsonObject data = doc.createNestedObject("latestData");
    data["temp"] = latest_sensor.temp;
    data["humi"] = latest_sensor.humi;
    data["soil"] = latest_sensor.soil;
    data["lux"] = latest_sensor.lux;
    data["water"] = latest_sensor.water;
    data["light"] = latest_sensor.light;
    data["fan"] = latest_sensor.fan;
    data["buzzer"] = latest_sensor.buzzer;
    data["ageMs"] = millis() - latest_sensor.updated_at;
  }

  if (last_ack.valid) {
    JsonObject ack = doc.createNestedObject("latestAck");
    ack["target"] = last_ack.target;
    ack["action"] = last_ack.action;
    ack["result"] = last_ack.result;
    ack["ageMs"] = millis() - last_ack.updated_at;
  }

  fillThresholdJson(doc.createNestedObject("thresholds"));
  fillAlarmJson(doc.createNestedObject("alarm"));

  String response;
  serializeJson(doc, response);
  server->sendHeader(F("Cache-Control"), F("no-store"));
  server->send(200, "application/json", response);
}

bool validateCommand(JsonDocument& doc, String& error) {
  const char* target = doc["target"];
  const char* action = doc["action"];

  if (target == nullptr || action == nullptr) {
    error = F("缺少 target 或 action 字段");
    return false;
  }

  if (strcmp(target, "water") != 0 && strcmp(target, "light") != 0 && strcmp(target, "fan") != 0 &&
      strcmp(target, "buzzer") != 0) {
    error = F("target 非法");
    return false;
  }

  if (strcmp(action, "on") != 0 && strcmp(action, "off") != 0 && strcmp(action, "pulse") != 0) {
    error = F("action 非法");
    return false;
  }

  if (strcmp(action, "pulse") == 0) {
    if (!doc.containsKey("time")) {
      error = F("pulse 指令缺少 time");
      return false;
    }
    const int pulse_ms = doc["time"];
    if (pulse_ms <= 0 || pulse_ms > 10000) {
      error = F("time 超出范围");
      return false;
    }
  }

  return true;
}

void handleCommandRequest() {
  if (!server) {
    return;
  }

  if (!server->hasArg("plain")) {
    server->send(400, "application/json", F("{\"error\":\"缺少 JSON 负载\"}"));
    return;
  }

  const String body = server->arg("plain");
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "application/json", F("{\"error\":\"JSON 解析失败\"}"));
    return;
  }

  doc["type"] = "cmd";

  String error;
  if (!validateCommand(doc, error)) {
    StaticJsonDocument<96> resp;
    resp["error"] = error;
    String serialized;
    serializeJson(resp, serialized);
    server->send(422, "application/json", serialized);
    return;
  }

  if (!serial_bridge::sendJson(doc)) {
    server->send(500, "application/json", F("{\"error\":\"串口发送失败\"}"));
    return;
  }

  String serialized;
  serializeJson(doc, serialized);
  addMessage(serialized);

  StaticJsonDocument<96> resp;
  resp["result"] = "sent";
  resp["queuedId"] = last_message_id;
  String response;
  serializeJson(resp, response);
  server->send(200, "application/json", response);
}

void handleNotFound() {
  if (!server) {
    return;
  }

  if (!littleFsMounted && server->uri() == "/") {
    handleFallbackRoot();
    return;
  }

  server->send(404, "text/plain", "Not found");
}

void updateSensorSnapshot(const JsonDocument& doc) {
  latest_sensor.valid = true;
  latest_sensor.temp = doc["temp"] | latest_sensor.temp;
  latest_sensor.humi = doc["humi"] | latest_sensor.humi;
  latest_sensor.soil = doc["soil"] | latest_sensor.soil;
  latest_sensor.lux = doc["lux"] | latest_sensor.lux;
  latest_sensor.water = doc["water"] | latest_sensor.water;
  latest_sensor.light = doc["light"] | latest_sensor.light;
  latest_sensor.fan = doc["fan"] | latest_sensor.fan;
  latest_sensor.buzzer = doc["buzzer"] | latest_sensor.buzzer;
  latest_sensor.updated_at = millis();
}

void updateAckSnapshot(const JsonDocument& doc) {
  last_ack.valid = true;
  last_ack.target = doc["target"] | "";
  last_ack.action = doc["action"] | "";
  last_ack.result = doc["result"] | "";
  last_ack.updated_at = millis();
}

}  // namespace

void start(uint16_t port) {
  if (server != nullptr) {
    server->stop();
    delete server;
    server = nullptr;
  }

  if (littleFsMounted) {
    LittleFS.end();
    littleFsMounted = false;
  }

  littleFsMounted = LittleFS.begin();
  if (!littleFsMounted) {
    Serial.println(F("LittleFS 挂载失败，将使用回退页面。"));
  }

  server = new ESP8266WebServer(port);

  if (littleFsMounted) {
    server->on("/", handleIndexHtml);
    server->serveStatic("/index.css", LittleFS, "/index.css");
    server->serveStatic("/index.js", LittleFS, "/index.js");
  } else {
    server->on("/", handleFallbackRoot);
  }

  server->on("/api/messages", HTTP_GET, handleMessagesRequest);
  server->on("/api/state", HTTP_GET, handleStateRequest);
  server->on("/api/cmd", HTTP_POST, handleCommandRequest);
  server->on("/api/thresholds", HTTP_GET, handleThresholdGet);
  server->on("/api/thresholds", HTTP_POST, handleThresholdPost);
  server->onNotFound(handleNotFound);
  server->begin();
}

void loop() {
  if (server != nullptr) {
    server->handleClient();
  }
}

bool isRunning() {
  return server != nullptr;
}

void handleSerialLine(const String& line) {
  addMessage(line);

  StaticJsonDocument<256> doc;
  const DeserializationError err = deserializeJson(doc, line);
  if (err) {
    Serial.print(F("解析串口 JSON 失败: "));
    Serial.println(err.c_str());
    return;
  }

  const char* type = doc["type"];
  if (type == nullptr) {
    Serial.println(F("串口消息缺少 type 字段"));
    return;
  }

  if (strcmp(type, "data") == 0) {
    updateSensorSnapshot(doc);
    checkAndTriggerAlarm(doc);
  } else if (strcmp(type, "ack") == 0) {
    updateAckSnapshot(doc);
  } else if (strcmp(type, "status") == 0) {
    last_reported_ip = doc["ip"] | last_reported_ip;
  }
}

}  // namespace web_server_module
