#ifndef CODE_ESP32_CLOUD_API_H
#define CODE_ESP32_CLOUD_API_H

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"

static inline bool postToCloud(float t, float h, int &r1, int &r2, String &suggestion) {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  HTTPClient https;
  client.setInsecure();

  if (!https.begin(client, geminiApiUrl)) {
    Serial.println("HTTPS begin failed");
    return false;
  }
  https.addHeader("Content-Type", "application/json");
  if (geminiApiKey && strlen(geminiApiKey) > 0) {
    https.addHeader("Authorization", String("Bearer ") + geminiApiKey);
  }

  StaticJsonDocument<256> doc;
  doc["temperature"] = t;
  doc["humidity"] = h;
  doc["timestamp"] = millis();
  doc["weatherKey"] = weatherKey;
  String payload;
  serializeJson(doc, payload);

  Serial.print("POST payload: ");
  Serial.println(payload);

  int httpCode = https.POST(payload);
  if (httpCode <= 0) {
    Serial.printf("HTTP POST failed, code: %d\n", httpCode);
    https.end();
    return false;
  }

  String resp = https.getString();
  https.end();

  Serial.print("Cloud response: ");
  Serial.println(resp);

  StaticJsonDocument<512> resDoc;
  DeserializationError err = deserializeJson(resDoc, resp);
  if (err) {
    Serial.print("JSON parse error: ");
    Serial.println(err.c_str());
    return false;
  }

  if (resDoc.containsKey("relay1")) r1 = resDoc["relay1"].as<int>();
  if (resDoc.containsKey("relay2")) r2 = resDoc["relay2"].as<int>();

  if (resDoc.containsKey("suggestion")) {
    suggestion = String((const char*)resDoc["suggestion"]);
  } else if (resDoc.containsKey("prediction")) {
    String tmp;
    serializeJson(resDoc["prediction"], tmp);
    suggestion = tmp;
  }

  return true;
}

#endif // CODE_ESP32_CLOUD_API_H
