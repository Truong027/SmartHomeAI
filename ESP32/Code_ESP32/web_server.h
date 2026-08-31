#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Update.h>
#include <ArduinoJson.h>
#include "web_ui.h"

extern float indoorTemp, indoorHum;
extern float owmTemp, owmWindSpd;
extern int owmHumidity;
extern String aiAdvice, aiDesc;
extern volatile bool relay1, relay2;

extern Preferences preferences;
extern bool settingLogCsv;
extern bool settingOta;
extern bool settingAudio;
extern bool settingPopup;


extern void applyRelays(bool r1, bool r2); 
extern void beepDouble(); 
extern int curHour;
extern float histOwmT[24];
extern float histOwmH[24];


// Thêm các biến quản lý chuột
extern float cursorX, cursorY;
extern bool isClicked;
extern bool isBackPressed;
extern bool fullRedraw;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

#include <vector>

extern SemaphoreHandle_t stringMutex;
extern std::vector<String> wsLogQueue;

inline void wsLog(String msg) {
  Serial.println("[WS LOG] " + msg);
  if (stringMutex) xSemaphoreTake(stringMutex, portMAX_DELAY);
  wsLogQueue.push_back(msg);
  if (stringMutex) xSemaphoreGive(stringMutex);
}

inline void processWsLogs() {
  if (wsLogQueue.empty()) return;
  std::vector<String> copyQueue;
  if (stringMutex) xSemaphoreTake(stringMutex, portMAX_DELAY);
  copyQueue = wsLogQueue;
  wsLogQueue.clear();
  if (stringMutex) xSemaphoreGive(stringMutex);
  
  for (const String &m : copyQueue) {
    StaticJsonDocument<256> doc;
    doc["type"] = "log";
    doc["msg"] = m;
    String json;
    serializeJson(doc, json);
    ws.textAll(json);
  }
}


inline void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("WS Client connected: %u\n", client->id());
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("WS Client disconnected: %u\n", client->id());
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      data[len] = 0;
      String msg = (char*)data;
      
      StaticJsonDocument<200> doc;
      DeserializationError err = deserializeJson(doc, msg);
      if (!err) {
        String eventType = doc["type"] | "";
        if (eventType == "move") {
          float dx = doc["dx"] | 0.0f;
          float dy = doc["dy"] | 0.0f;
          cursorX += dx;
          cursorY += dy;
          // Giới hạn tọa độ chuột trong màn hình 160x128
          if (cursorX < 0) cursorX = 0;
          if (cursorX > 159) cursorX = 159;
          if (cursorY < 0) cursorY = 0;
          if (cursorY > 127) cursorY = 127;
        } else if (eventType == "click") {
          isClicked = true;
        } else if (eventType == "back") {
          isBackPressed = true;
        } else if (eventType == "relay") {
          int r = doc["relay"] | 1;
          if (r == 1) {
            applyRelays(!relay1, relay2);
          } else if (r == 2) {
            applyRelays(relay1, !relay2);
          }
          beepDouble();
        }
      }
    }
  }
}

inline void setupWebServer() {
  if(!LittleFS.begin(true)){
    Serial.println("An Error has occurred while mounting LittleFS");
  } else {
    Serial.println("LittleFS mounted successfully");
  }
  
  ws.onEvent(onEvent);
  server.addHandler(&ws);

  // Phục vụ giao diện Trackpad trực tiếp từ code C++ (không cần LittleFS)
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", (const uint8_t*)index_html, sizeof(index_html) - 1);
    response->addHeader("Content-Type", "text/html; charset=utf-8");
    request->send(response);
  });
  
  // Giữ API cũ cho tương thích (nếu cần test qua URL)
  server.on("/api/data", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(1024);
    doc["inT"] = indoorTemp;
    doc["inH"] = indoorHum;
    doc["outT"] = owmTemp;
    doc["outH"] = owmHumidity;
    doc["ai"] = aiAdvice.length() > 0 ? aiAdvice : "Waiting for AI analysis...";
    doc["r1"] = relay1;
    doc["r2"] = relay2;
    doc["ramFree"] = ESP.getFreeHeap();
    doc["ramTotal"] = ESP.getHeapSize();
    doc["curHour"] = curHour;
    
    JsonArray arrT = doc.createNestedArray("histT");
    JsonArray arrH = doc.createNestedArray("histH");
    JsonArray arrOwmT = doc.createNestedArray("histOwmT");
    JsonArray arrOwmH = doc.createNestedArray("histOwmH");
    int count = histFull ? HIST_MAX : histIdx;
    int start = histFull ? histIdx : 0;
    for(int i = 0; i < count; i++) {
        int idx = (start + i) % HIST_MAX;
        arrT.add(histT[idx]);
        arrH.add(histH[idx]);
        arrOwmT.add(histOwmT[idx]);
        arrOwmH.add(histOwmH[idx]);
    }
    
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // ─── API OTA Web Server (Nạp code không dây cho ESP32) ───
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", "<h1>ESP32 OTA</h1><form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>");
  });

  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
    bool shouldReboot = !Update.hasError();
    AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", shouldReboot ? "Update Success! Rebooting..." : "Update Failed");
    response->addHeader("Connection", "close");
    request->send(response);
    if(shouldReboot) {
      delay(1000);
      ESP.restart();
    }
  }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
    if(!index){
      Serial.printf("OTA Update Start: %s\n", filename.c_str());
      if(!Update.begin(UPDATE_SIZE_UNKNOWN)){
        Update.printError(Serial);
      }
    }
    if(!Update.hasError()){
      if(Update.write(data, len) != len){
        Update.printError(Serial);
      }
    }
    if(final){
      if(Update.end(true)){
        Serial.printf("OTA Update Success: %u bytes\n", index+len);
      } else {
        Update.printError(Serial);
      }
    }
  });

  // API upload hình nền
  static File bgUploadFile;
  server.on("/upload_bg", HTTP_POST, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "OK");
  }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
    if (index == 0) {
      bgUploadFile = LittleFS.open("/bg.bin", "w");
    }
    if (bgUploadFile) {
      bgUploadFile.write(data, len);
    }
    if (index + len == total) {
      if (bgUploadFile) {
        bgUploadFile.close();
      }
      fullRedraw = true; // Yêu cầu vẽ lại toàn bộ màn hình
    }
  });

  
  // Lấy cài đặt
  
  // File Manager API
  server.on("/api/fs", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(2048);
    JsonArray files = doc.createNestedArray("files");
    File root = LittleFS.open("/");
    File file = root.openNextFile();
    while(file){
      JsonObject fObj = files.createNestedObject();
      String fname = String(file.name());
      if (!fname.startsWith("/")) fname = "/" + fname;
      fObj["name"] = fname;
      fObj["size"] = file.size();
      file = root.openNextFile();
    }
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  server.on("/api/fs/delete", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
    if (index == 0) {
      data[len] = 0;
      StaticJsonDocument<200> doc;
      if (!deserializeJson(doc, (char*)data)) {
        String filename = doc["filename"] | "";
        if (filename.length() > 0) {
          if (!filename.startsWith("/")) filename = "/" + filename;
          LittleFS.remove(filename);
        }
      }
      request->send(200, "text/plain", "OK");
    }
  });

  server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(512);
    doc["logCsv"] = settingLogCsv;
    doc["ota"] = settingOta;
    doc["audio"] = settingAudio;
    doc["popup"] = settingPopup;
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });
  
  // Lưu cài đặt
  server.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
    if (index == 0) {
      data[len] = 0;
      DynamicJsonDocument doc(512);
      if (!deserializeJson(doc, (char*)data)) {
        if (doc.containsKey("logCsv")) settingLogCsv = doc["logCsv"];
        if (doc.containsKey("ota")) settingOta = doc["ota"];
        if (doc.containsKey("audio")) settingAudio = doc["audio"];
        if (doc.containsKey("popup")) settingPopup = doc["popup"];
        
        preferences.putBool("logCsv", settingLogCsv);
        preferences.putBool("ota", settingOta);
        preferences.putBool("audio", settingAudio);
        preferences.putBool("popup", settingPopup);
      }
      request->send(200, "text/plain", "OK");
    }
  });

  // Tải file CSV
  server.on("/download_csv", HTTP_GET, [](AsyncWebServerRequest *request){
    if(LittleFS.exists("/data_log.csv")){
      request->send(LittleFS, "/data_log.csv", "text/csv", true); // true = force download
    } else {
      request->send(404, "text/plain", "File not found");
    }
  });


  
  // OTA Upload
  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
    bool shouldReboot = !Update.hasError();
    AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", shouldReboot ? "OK" : "FAIL");
    response->addHeader("Connection", "close");
    request->send(response);
    if(shouldReboot) {
      delay(500);
      ESP.restart();
    }
  }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
    if(!settingOta) return; // Nếu tắt OTA thì bỏ qua
    if(!index){
      Serial.printf("Update Start: %s\n", filename.c_str());
      if(!Update.begin(UPDATE_SIZE_UNKNOWN)){
        Update.printError(Serial);
      }
    }
    if(!Update.hasError()){
      if(Update.write(data, len) != len){
        Update.printError(Serial);
      }
    }
    if(final){
      if(Update.end(true)){
        Serial.printf("Update Success: %uB\n", index+len);
      } else {
        Update.printError(Serial);
      }
    }
  });

  server.on("/ota", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = "<html><head><title>Cập nhật Firmware (OTA)</title>"
                  "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                  "<style>body { font-family: sans-serif; padding: 20px; background: #121212; color: #fff; text-align: center; } form { margin-top: 20px; } input[type=file] { margin-bottom: 20px; padding: 10px; border: 1px solid #333; } input[type=submit] { padding: 10px 20px; background: #00e5ff; color: #000; font-weight: bold; border: none; border-radius: 5px; font-size: 16px; }</style>"
                  "</head><body>"
                  "<h2>Nạp code OTA ESP32</h2>"
                  "<form method='POST' action='/update' enctype='multipart/form-data'>"
                  "<input type='file' name='update' accept='.bin'>"
                  "<br><input type='submit' value='Cập nhật Code'>"
                  "</form></body></html>";
    request->send(200, "text/html", html);
  });

  server.serveStatic("/", LittleFS, "/");
  server.begin();
  Serial.println("Async HTTP server & WebSocket started");
}

#endif
