/*
  ESP8266_Firebase.ino
  ESP8266: Trạm điều khiển sân độc lập qua Firebase

  Cài đặt thư viện:
  - Firebase ESP Client (bởi Mobizt)
  - Chân kết nối:
    - Nút 1: D1 (GPIO5)
    - Nút 2: D2 (GPIO4)
    - Relay 1: D6 (GPIO12)
    - Relay 2: D7 (GPIO13)
*/

#include <ESP8266WiFi.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include <DNSServer.h>

// ─── CẤU HÌNH FIREBASE ────────────────────────────────────────
#define FIREBASE_API_KEY "AIzaSyDND5fdH_tduPrnFHPsAo2Ggxzu1zJk18o"
#define FIREBASE_URL "https://esp32app-30335-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define FIREBASE_USER_EMAIL "admin@esp32.local"
#define FIREBASE_USER_PASSWORD "123456"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool firebaseReady = false;

// ─── CẤU HÌNH CHÂN ────────────────────────────────────────
#define BUTTON1_PIN 5  // D1
#define BUTTON2_PIN 4  // D2
#define RELAY1_PIN 12  // D6
#define RELAY2_PIN 13  // D7

// ─── WEBSERVER & OTA ────────────────────────────────────────
ESP8266WebServer otaServer(80);
ESP8266HTTPUpdateServer httpUpdater;
DNSServer dnsServer;

bool isConfigMode = false;

// ─── Biến trạng thái Relay ────────────────────────────────────────
bool currentRelay1 = false;
bool currentRelay2 = false;
bool syncPending1 = false;
bool syncPending2 = false;

// ─── Biến debounce Nút bấm ────────────────────────────────────────
bool lastBtn1State = HIGH;
bool lastBtn2State = HIGH;
unsigned long lastDebounce1 = 0;
unsigned long lastDebounce2 = 0;
unsigned long pressStart1 = 0;
unsigned long pressStart2 = 0;
bool btn1Held = false;
bool btn2Held = false;
const unsigned long DEBOUNCE_MS = 50;
const unsigned long HOLD_RESET_MS = 10000;

// ─── Timer Firebase ────────────────────────────────────────
unsigned long lastFirebaseSync = 0;
unsigned long lastIpSync = 0;
const unsigned long FIREBASE_INTERVAL = 2000;
const unsigned long IP_INTERVAL = 60000;

void saveWiFiToEEPROM(String ssid, String pass) {
  for (int i = 0; i < 96; ++i) EEPROM.write(i, 0);
  for (int i = 0; i < ssid.length(); ++i) EEPROM.write(i, ssid[i]);
  for (int i = 0; i < pass.length(); ++i) EEPROM.write(32 + i, pass[i]);
  EEPROM.commit();
}

void clearWiFiEEPROM() {
  for (int i = 0; i < 96; ++i) EEPROM.write(i, 0);
  EEPROM.commit();
}

void loadWiFiFromEEPROM(String &ssid, String &pass) {
  ssid = "";
  pass = "";
  for (int i = 0; i < 32; ++i) {
    char c = char(EEPROM.read(i));
    if (c == 0) break;
    ssid += c;
  }
  for (int i = 32; i < 96; ++i) {
    char c = char(EEPROM.read(i));
    if (c == 0) break;
    pass += c;
  }
}

void setupWebServerConfig() {
  otaServer.onNotFound([]() {
    // Nếu request /wifisave (từ App)
    if (otaServer.uri() == "/wifisave" && otaServer.method() == HTTP_POST) {
      String s = otaServer.arg("s");
      String p = otaServer.arg("p");
      
      if (s.length() > 0) {
        Serial.println("Thử kết nối Wi-Fi mới: " + s);
        WiFi.begin(s.c_str(), p.c_str());
        
        int tries = 0;
        while (WiFi.status() != WL_CONNECTED && tries < 30) { // 15 giây
          delay(500);
          Serial.print(".");
          tries++;
        }
        Serial.println();
        
        if (WiFi.status() == WL_CONNECTED) {
          saveWiFiToEEPROM(s, p);
          String ipStr = WiFi.localIP().toString();
          String json = "{\"status\":\"success\",\"ip\":\"" + ipStr + "\"}";
          
          // Trả về JSON để App nhận được kết quả thành công và IP
          otaServer.send(200, "application/json", json);
          delay(1000);
          ESP.restart(); // Khởi động lại để vào chế độ bình thường
        } else {
          WiFi.disconnect();
          WiFi.softAP("ESP8266_Setup"); // Bật lại AP
          String json = "{\"status\":\"error\",\"message\":\"Sai mật khẩu hoặc Wi-Fi quá yếu\"}";
          // Dùng 400 Bad Request để App dễ nhận diện lỗi
          otaServer.send(400, "application/json", json);
        }
      } else {
         otaServer.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Tên Wi-Fi trống\"}");
      }
      return;
    }

    // Nếu không phải /wifisave, trả về giao diện Captive Portal HTML
    String html = "<html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<title>Cấu hình Wi-Fi</title><style>";
    html += "body{font-family:Arial;text-align:center;background:#1a1a2e;color:#fff;margin:0;padding:20px;}";
    html += "h1{color:#00e5ff;}";
    html += "input{width:100%;max-width:300px;padding:12px;margin:10px 0;border-radius:8px;border:none;font-size:16px;}";
    html += "button{width:100%;max-width:300px;padding:15px;background:#00e5ff;color:#000;border:none;border-radius:8px;font-size:18px;font-weight:bold;cursor:pointer;}";
    html += "</style></head><body>";
    html += "<h1>ESP8266 Setup</h1>";
    html += "<p>Vui lòng nhập mạng Wi-Fi nhà bạn</p>";
    html += "<form action='/wifisave' method='POST'>";
    html += "<input type='text' name='s' placeholder='Tên Wi-Fi (SSID)' required><br>";
    html += "<input type='password' name='p' placeholder='Mật khẩu'><br>";
    html += "<button type='submit'>Lưu & Kết Nối</button>";
    html += "</form></body></html>";
    
    otaServer.send(200, "text/html", html);
  });
  
  otaServer.begin();
  dnsServer.start(53, "*", WiFi.softAPIP());
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(512);
  Serial.println("\n\n=== ESP8266 Smart Home ===");

  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  digitalWrite(RELAY1_PIN, HIGH);
  digitalWrite(RELAY2_PIN, HIGH);

  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);

  // ─── ĐỌC WIFI TỪ EEPROM ────────────────────────────────────────
  String ssid, pass;
  loadWiFiFromEEPROM(ssid, pass);
  
  if (ssid.length() > 0) {
    Serial.println("Đang thử kết nối Wi-Fi đã lưu: " + ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 20) { // Đợi 10 giây
      delay(500);
      Serial.print(".");
      tries++;
    }
    Serial.println();
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi: THẤT BẠI. Mở chế độ Cấu hình (AP Mode).");
    isConfigMode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP8266_Setup");
    setupWebServerConfig();
  } else {
    Serial.print("WiFi: OK! IP = ");
    Serial.println(WiFi.localIP());
    isConfigMode = false;

    // ─── KHỞI TẠO WEB OTA (/update) ────────────────────────────────────────
    httpUpdater.setup(&otaServer);
    otaServer.on("/", []() {
      String html = "<html><head><meta charset='utf-8'><title>ESP8266 OTA</title>";
      html += "<style>body{font-family:Arial;text-align:center;margin-top:50px;background:#1a1a2e;color:#fff}";
      html += "a{color:#00e5ff;font-size:24px;padding:15px 40px;border:2px solid #00e5ff;border-radius:10px;text-decoration:none}";
      html += "a:hover{background:#00e5ff;color:#000}</style></head>";
      html += "<body><h1>ESP8266 OTA</h1><p>IP: " + WiFi.localIP().toString() + "</p>";
      html += "<br><a href='/update'>Nạp Firmware Mới</a></body></html>";
      otaServer.send(200, "text/html", html);
    });
    otaServer.begin();

    // ─── KHỞI TẠO ARDUINO OTA (Nạp qua Arduino IDE) ────────────────────────────────────────
    ArduinoOTA.setHostname("ESP8266_NgoaiSan");
    ArduinoOTA.onStart([]() { Serial.println("Start OTA..."); });
    ArduinoOTA.onEnd([]() { Serial.println("\nEnd OTA"); });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
    });
    ArduinoOTA.begin();

    // ─── KHỞI TẠO FIREBASE ────────────────────────────────────────
    config.api_key = FIREBASE_API_KEY;
    config.database_url = FIREBASE_URL;
    auth.user.email = FIREBASE_USER_EMAIL;
    auth.user.password = FIREBASE_USER_PASSWORD;
    config.token_status_callback = tokenStatusCallback;

    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
  }

  Serial.println("=== Khởi động hoàn tất ===\n");
}

void handleButtons() {
  unsigned long now = millis();

  // ─── NÚT 1 ────────────────────────────────────────
  bool reading1 = digitalRead(BUTTON1_PIN);
  if (reading1 != lastBtn1State) {
    lastDebounce1 = now;
  }
  if ((now - lastDebounce1) > DEBOUNCE_MS) {
    if (reading1 == LOW) {
      if (pressStart1 == 0) {
        pressStart1 = now;
        // Bật/tắt ngay khi vừa nhấn nút
        currentRelay1 = !currentRelay1;
        digitalWrite(RELAY1_PIN, currentRelay1 ? LOW : HIGH);
        syncPending1 = true;
      }
      if (!btn1Held && (now - pressStart1 > HOLD_RESET_MS)) {
        btn1Held = true;
        Serial.println(">>> NÚT 1 GIỮ 10s -> RESET WIFI <<<");
        clearWiFiEEPROM();
        delay(500);
        ESP.restart();
      }
    } else {
      pressStart1 = 0;
      btn1Held = false;
    }
  }
  lastBtn1State = reading1;

  // ─── NÚT 2 ────────────────────────────────────────
  bool reading2 = digitalRead(BUTTON2_PIN);
  if (reading2 != lastBtn2State) {
    lastDebounce2 = now;
  }
  if ((now - lastDebounce2) > DEBOUNCE_MS) {
    if (reading2 == LOW) {
      if (pressStart2 == 0) {
        pressStart2 = now;
        // Bật/tắt ngay khi vừa nhấn nút
        currentRelay2 = !currentRelay2;
        digitalWrite(RELAY2_PIN, currentRelay2 ? LOW : HIGH);
        syncPending2 = true;
      }
      if (!btn2Held && (now - pressStart2 > HOLD_RESET_MS)) {
        btn2Held = true;
        Serial.println(">>> NÚT 2 GIỮ 10s -> RESET WIFI <<<");
        clearWiFiEEPROM();
        delay(500);
        ESP.restart();
      }
    } else {
      pressStart2 = 0;
      btn2Held = false;
    }
  }
  lastBtn2State = reading2;
}

void syncFirebase() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!Firebase.ready()) return;

  unsigned long now = millis();
  
  if (now - lastIpSync > IP_INTERVAL || lastIpSync == 0) {
    lastIpSync = now;
    Firebase.RTDB.setString(&fbdo, "/esp8266/ip", WiFi.localIP().toString());
    Firebase.RTDB.setInt(&fbdo, "/esp8266/last_seen", (int)(millis() / 1000));
  }

  if (now - lastFirebaseSync > FIREBASE_INTERVAL) {
    lastFirebaseSync = now;

    if (syncPending1) {
      Firebase.RTDB.setBool(&fbdo, "/esp8266/relay1", currentRelay1);
      syncPending1 = false;
    } else {
      if (Firebase.RTDB.getBool(&fbdo, "/esp8266/relay1")) {
        bool appState = fbdo.boolData();
        if (appState != currentRelay1) {
          currentRelay1 = appState;
          digitalWrite(RELAY1_PIN, currentRelay1 ? LOW : HIGH);
        }
      }
    }

    yield();

    if (syncPending2) {
      Firebase.RTDB.setBool(&fbdo, "/esp8266/relay2", currentRelay2);
      syncPending2 = false;
    } else {
      if (Firebase.RTDB.getBool(&fbdo, "/esp8266/relay2")) {
        bool appState = fbdo.boolData();
        if (appState != currentRelay2) {
          currentRelay2 = appState;
          digitalWrite(RELAY2_PIN, currentRelay2 ? LOW : HIGH);
        }
      }
    }
  }
}

void loop() {
  if (isConfigMode) {
    dnsServer.processNextRequest();
    otaServer.handleClient();
    handleButtons(); // Vẫn cho phép bấm nút cứng khi đang ở AP mode
    yield();
    return;
  }

  ArduinoOTA.handle();      // OTA qua Arduino IDE
  otaServer.handleClient(); // OTA qua Web

  handleButtons();
  syncFirebase();
  yield();
}
