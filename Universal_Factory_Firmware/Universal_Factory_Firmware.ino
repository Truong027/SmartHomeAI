/*
  ======================================================================
  UNIVERSAL FACTORY FIRMWARE - SMART HOME IOT (ESP32 & ESP8266)
  ======================================================================
  
  Mô tả:
  Đây là bản CODE NHÀ MÁY DÙNG CHUNG (Universal Firmware) nạp 1 LẦN DUY NHẤT.
  Bạn KHÔNG CẦN sửa bất kỳ dòng code nào hay nạp lại code khi mua bo mạch mới.
  
  Tính năng chuẩn công nghiệp:
  1. Tự động đọc MAC Address phần cứng làm Node ID duy nhất (VD: esp32_A4CF12 / esp8266_B29A4C).
  2. Phát Wi-Fi AP cấu hình tên "SmartHome_Setup" (IP: 192.168.4.1).
  3. Lắng nghe App gửi Wi-Fi nhà + Node ID + Auth Token qua HTTP POST /wifisave.
  4. Lưu thông tin vào bộ nhớ Flash (EEPROM/Preferences) không bị mất khi cúp điện.
  5. Đồng bộ Realtime 2 chiều với Firebase qua node /<nodeId> (relay1, relay2, ip...).
  6. Giữ nút bấm vật lý 10 giây để Reset Wi-Fi về trạng thái xuất xưởng.
  7. Tích hợp OTA Web Server tại đường dẫn http://<IP>/update để nạp code từ xa qua Wi-Fi.
  
  Thư viện yêu cầu (Cài đặt trong Arduino IDE):
  - Firebase ESP Client (bởi Mobizt)
  - WiFiManager (bởi tzapu)
  ======================================================================
*/

#include <Arduino.h>

#if defined(ESP32)
  #include <WiFi.h>
  #include <WebServer.h>
  #include <Update.h>
  #include <Preferences.h>
  Preferences preferences;
  WebServer server(80);
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>
  #include <ESP8266HTTPUpdateServer.h>
  #include <EEPROM.h>
  ESP8266WebServer server(80);
  ESP8266HTTPUpdateServer httpUpdater;
#endif

#include <WiFiManager.h> 
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>

// ─── CẤU HÌNH FIREBASE HỆ THỐNG ─────────────────────────────────────
#define FIREBASE_API_KEY "AIzaSyDND5fdH_tduPrnFHPsAo2Ggxzu1zJk18o"
#define FIREBASE_URL "https://esp32app-30335-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define FIREBASE_USER_EMAIL "admin@esp32.local"
#define FIREBASE_USER_PASSWORD "123456"

// ─── CẤU HÌNH CHÂN NÚT BẤM VÀ RELAY ──────────────────────────────────
#if defined(ESP32)
  #define RELAY1_PIN 18
  #define RELAY2_PIN 19
  #define BUTTON1_PIN 0
  #define BUTTON2_PIN 4
#else
  #define RELAY1_PIN D6
  #define RELAY2_PIN D7
  #define BUTTON1_PIN D1
  #define BUTTON2_PIN D2
#endif

// ─── ĐỐI TƯỢNG FIREBASE ─────────────────────────────────────────────
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// ─── BIẾN HỆ THỐNG DÙNG CHUNG ──────────────────────────────────────
String activeNodeID = "";
String activeAuthToken = "";
String globalBasePath = ""; // Sử dụng biến toàn cục để tránh nối chuỗi
FirebaseJson globalSensorJson; // Dùng chung để tránh tràn RAM
FirebaseJson globalEnergyJson; // Dùng chung để tránh tràn RAM
bool currentRelay1 = false;
bool currentRelay2 = false;
bool syncPending1 = false;
bool syncPending2 = false;

unsigned long sendDataPrevMillis = 0;
unsigned long heartbeatPrevMillis = 0;
unsigned long sensorLogPrevMillis = 0;
unsigned long pressTime1 = 0;
bool isPressing1 = false;
bool processed1 = false;
unsigned long lastRelease1 = 0;

// ─── TỰ ĐỘNG TẠO NODE ID DỰA TRÊN MAC ADDRESS PHẦN CỨNG ──────────────
String getHardwareNodeID() {
#if defined(ESP32)
  uint64_t chipid = ESP.getEfuseMac();
  char idStr[32];
  snprintf(idStr, sizeof(idStr), "esp32_%04X%08X", (uint16_t)(chipid >> 32), (uint32_t)chipid);
  return String(idStr);
#else
  uint32_t chipid = ESP.getChipId();
  char idStr[32];
  snprintf(idStr, sizeof(idStr), "esp8266_%06X", chipid);
  return String(idStr);
#endif
}


// ─── ĐỌC / GHI CẤU HÌNH FLASH EEPROM ─────────────────────────────────
void loadSystemSettings() {
#if defined(ESP32)
  preferences.begin("smarthome", true);
  activeNodeID = preferences.getString("node_id", getHardwareNodeID());
  activeAuthToken = preferences.getString("auth_token", "");
  preferences.end();
#else
  EEPROM.begin(512);
  char nBuf[33] = {0};
  char tBuf[33] = {0};
  for (int i = 0; i < 32; i++) nBuf[i] = EEPROM.read(i);
  for (int i = 0; i < 32; i++) tBuf[i] = EEPROM.read(32 + i);
  activeNodeID = String(nBuf);
  activeAuthToken = String(tBuf);
  if (activeNodeID.length() < 3) activeNodeID = getHardwareNodeID();
#endif

  globalBasePath = "/" + activeNodeID;

  Serial.println("==========================================");
  Serial.print("Node ID Mạch: "); Serial.println(activeNodeID);
  Serial.print("Auth Token Mạch: "); Serial.println(activeAuthToken);
  Serial.println("==========================================");
}

void saveSystemSettings(String node, String token) {
#if defined(ESP32)
  preferences.begin("smarthome", false);
  preferences.putString("node_id", node);
  preferences.putString("auth_token", token);
  preferences.end();
#else
  EEPROM.begin(512);
  for (int i = 0; i < 32; i++) EEPROM.write(i, i < node.length() ? node[i] : 0);
  for (int i = 0; i < 32; i++) EEPROM.write(32 + i, i < token.length() ? token[i] : 0);
  EEPROM.commit();
#endif
  activeNodeID = node;
  activeAuthToken = token;
  globalBasePath = "/" + activeNodeID;
}

// ─── KHỞI TẠO SETUP ────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  digitalWrite(RELAY1_PIN, HIGH);
  digitalWrite(RELAY2_PIN, HIGH);

  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);

  loadSystemSettings();

  // 1. Cấu hình WiFiManager AutoConnect
  WiFiManager wm;
  wm.setConfigPortalTimeout(60); // Timeout 60s nếu không ai nhập Wi-Fi
  
  if (!wm.autoConnect("SmartHome_Setup")) {
    Serial.println("Lỗi kết nối WiFi. Đang chạy chế độ Offline!");
  } else {
    Serial.print("Đã kết nối WiFi nhà! IP: ");
    Serial.println(WiFi.localIP());
  }

  // 2. Cổng nhận Wi-Fi & Token từ App Flutter (/wifisave)
  server.on("/wifisave", HTTP_POST, []() {
    String s = server.hasArg("s") ? server.arg("s") : "";
    String p = server.hasArg("p") ? server.arg("p") : "";
    String node = server.hasArg("node") ? server.arg("node") : activeNodeID;
    String token = server.hasArg("token") ? server.arg("token") : activeAuthToken;

    if (node.length() > 0) {
      saveSystemSettings(node, token);
    }

    server.send(200, "text/plain", "OK");
    delay(1000);
    ESP.restart();
  });

  // 3. Cài đặt OTA Web Server tại /update và /ota
#if defined(ESP8266)
  httpUpdater.setup(&server);
#elif defined(ESP32)
  server.on("/ota", HTTP_GET, []() {
    String html = "<html><head><title>Nạp OTA ESP32</title>"
                  "<meta name='viewport' content='width=device-width, initial-scale=1'></head>"
                  "<body style='font-family:sans-serif;padding:20px;background:#121212;color:#fff;text-align:center;'>"
                  "<h2>Cập nhật Firmware ESP32 (OTA)</h2>"
                  "<form method='POST' action='/update' enctype='multipart/form-data'>"
                  "<input type='file' name='update' accept='.bin' style='margin-bottom:20px;padding:10px;'>"
                  "<br><input type='submit' value='Tải lên và Cập nhật' style='padding:10px 20px;background:#00e5ff;color:#000;font-weight:bold;border:none;border-radius:6px;'>"
                  "</form></body></html>";
    server.send(200, "text/html", html);
  });

  server.on("/update", HTTP_POST, []() {
    bool shouldReboot = !Update.hasError();
    server.send(200, "text/plain", shouldReboot ? "OK" : "FAIL");
    if (shouldReboot) {
      delay(500);
      ESP.restart();
    }
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("OTA Start: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        Serial.printf("OTA Thành công: %u bytes\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  });
#endif

  server.on("/", []() {
    String html = "<h2>Universal SmartHome ESP Server</h2><p>Node ID: " + activeNodeID + "</p><p>IP: " + WiFi.localIP().toString() + "</p><p>N&acirc;p code OTA qua <a href='/update'>/update</a> hoac <a href='/ota'>/ota</a></p>";
    server.send(200, "text/html", html);
  });

  server.begin();
  Serial.println("HTTP Web Server & OTA đã sẵn sàng!");

  // 4. Khởi tạo kết nối Firebase
  config.api_key = FIREBASE_API_KEY;
  config.database_url = FIREBASE_URL;
  auth.user.email = FIREBASE_USER_EMAIL;
  auth.user.password = FIREBASE_USER_PASSWORD;
  config.token_status_callback = tokenStatusCallback;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

// ─── VÒNG LẶP CHÍNH LOOP ─────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  // 1. Xử lý Nút bấm vật lý (Bấm nhả = Toggle, Giữ 10s = Reset xuất xưởng)
  if (digitalRead(BUTTON1_PIN) == LOW) {
    if (!isPressing1 && (now - lastRelease1 > 50)) {
      isPressing1 = true;
      pressTime1 = now;
      processed1 = false;
    }
    if (isPressing1 && (now - pressTime1 > 10000)) { // Giữ 10 giây reset xuất xưởng
      Serial.println("Giữ nút 1 10 giây -> Xóa bộ nhớ Wi-Fi reset xuất xưởng!");
      WiFiManager wm;
      wm.resetSettings();
      saveSystemSettings("", "");
      delay(500);
      ESP.restart();
    }
  } else {
    if (isPressing1) {
      isPressing1 = false;
      if (!processed1 && (now - pressTime1 < 10000)) {
        currentRelay1 = !currentRelay1;
        digitalWrite(RELAY1_PIN, currentRelay1 ? LOW : HIGH);
        Serial.printf("Nút 1 bấm -> Relay 1: %s\n", currentRelay1 ? "ON" : "OFF");
        syncPending1 = true;
      }
      lastRelease1 = now;
    }
  }

  // 2. Đồng bộ dữ liệu Realtime với Firebase
  if (WiFi.status() == WL_CONNECTED && Firebase.ready() && (now - sendDataPrevMillis > 1200)) {
    sendDataPrevMillis = now;

    // Ưu tiên gửi sự kiện nút bấm vật lý
    if (syncPending1) {
      Firebase.RTDB.setBool(&fbdo, globalBasePath + "/relay1", currentRelay1);
      syncPending1 = false;
    } else {
      // Đọc trạng thái điều khiển từ App
      if (Firebase.RTDB.getBool(&fbdo, globalBasePath + "/relay1")) {
        bool fbR1 = fbdo.boolData();
        if (fbR1 != currentRelay1) {
          currentRelay1 = fbR1;
          digitalWrite(RELAY1_PIN, currentRelay1 ? LOW : HIGH);
        }
      }
    }

    if (syncPending2) {
      Firebase.RTDB.setBool(&fbdo, globalBasePath + "/relay2", currentRelay2);
      syncPending2 = false;
    } else {
      if (Firebase.RTDB.getBool(&fbdo, globalBasePath + "/relay2")) {
        bool fbR2 = fbdo.boolData();
        if (fbR2 != currentRelay2) {
          currentRelay2 = fbR2;
          digitalWrite(RELAY2_PIN, currentRelay2 ? LOW : HIGH);
        }
      }
    }

    // Đẩy IP thực tế lên Firebase
    Firebase.RTDB.setString(&fbdo, globalBasePath + "/ip", WiFi.localIP().toString());
  }

  // ─── 3. HEARTBEAT: Ghi last_seen mỗi 30 giây ──────────────────────
  if (WiFi.status() == WL_CONNECTED && Firebase.ready() && (now - heartbeatPrevMillis > 30000)) {
    heartbeatPrevMillis = now;
    // Dùng Server Timestamp để App Flutter nhận diện chính xác tuyệt đối thời gian thực
    Firebase.RTDB.setTimestamp(&fbdo, globalBasePath + "/last_seen");
    Serial.println("♥ Heartbeat sent: " + activeNodeID);
  }

  // ─── 4. SENSOR & ENERGY LOG: Ghi lịch sử mỗi 5 phút ──────────────
  if (WiFi.status() == WL_CONNECTED && Firebase.ready() && (now - sensorLogPrevMillis > 300000)) {
    sensorLogPrevMillis = now;
    
    // Đọc nhiệt độ & độ ẩm hiện tại từ Firebase node (được cảm biến ghi)
    float temp = 0, hum = 0;
    if (Firebase.RTDB.getFloat(&fbdo, globalBasePath + "/indoorTemp")) temp = fbdo.floatData();
    if (Firebase.RTDB.getFloat(&fbdo, globalBasePath + "/indoorHum")) hum = fbdo.floatData();
    
    // Tính watts đang tiêu thụ
    int activeWatts = 0;
    if (currentRelay1) activeWatts += 100;  // Mặc định relay1 = 100W
    if (currentRelay2) activeWatts += 350;  // Mặc định relay2 = 350W
    
    // Ghi bản ghi sensor history (nếu có dữ liệu sensor)
    if (temp > 0 || hum > 0) {
      String logPath = globalBasePath + "/sensor_history/" + String((int)(now / 1000));
      globalSensorJson.clear();
      globalSensorJson.set("temp", temp);
      globalSensorJson.set("hum", hum);
      globalSensorJson.set("uptime_s", (int)(now / 1000));
      Firebase.RTDB.setJSON(&fbdo, logPath, &globalSensorJson);
      Serial.printf("📊 Sensor Log: temp=%.1f°C, hum=%.1f%%\n", temp, hum);
    }
    
    // Ghi bản ghi energy log
    String energyPath = globalBasePath + "/energy_log/" + String((int)(now / 1000));
    globalEnergyJson.clear();
    globalEnergyJson.set("r1", currentRelay1);
    globalEnergyJson.set("r2", currentRelay2);
    globalEnergyJson.set("watts", activeWatts);
    globalEnergyJson.set("uptime_s", (int)(now / 1000));
    Firebase.RTDB.setJSON(&fbdo, energyPath, &globalEnergyJson);
    Serial.printf("⚡ Energy Log: watts=%dW, r1=%d, r2=%d\n", activeWatts, currentRelay1, currentRelay2);
  }

  // 5. Xử lý yêu cầu HTTP & OTA
  server.handleClient();
}
