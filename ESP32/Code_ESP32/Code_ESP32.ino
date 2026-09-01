#define FIREBASE_NODE "/ESP32_AI_Hub"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>

#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>

#include "config.h"
#include "ui_lvgl.h"
#include "hw_sensors.h"
#include "hw_audio.h"
#include "hw_led.h"
#include "ai_agent.h"

#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <ir_Daikin.h>
#include "hw_eeprom.h"

IRDaikinESP ac_daikin(IR_SEND_PIN);
IRDaikin160 ac_daikin_160(IR_SEND_PIN);
IRrecv irrecv(IR_RECV_PIN, 1024, 50, true);
IRsend irsend(IR_SEND_PIN);
decode_results ir_results;

LearnedIR learned_ir[MAX_IR_SLOTS];

int selected_ir_idx = 0;
bool is_ir_learning_mode = false;

// Trạng thái Điều hòa Daikin
bool daikin_power = false;
uint8_t daikin_temp = 25;
uint8_t daikin_fan = 10; // kDaikinFanAuto (0xA)
String userName = "Trường";

// Biến Âm lịch thiên văn Việt Nam
int lunarDay_global = 3;
int lunarMonth_global = 7;
int lunarYear_global = 2026;
String canChiDay_global = "Tân Dậu";
String canChiYear_global = "Bính Ngọ";
String dateLunar = "03/07 AL";

// Hàm phát sóng hồng ngoại điều hòa Daikin (Phát đồng thời cả chuẩn 280-bit và 160-bit để tương thích 100% tất cả các đời máy)
void sendDaikinCommand(bool power, uint8_t temp, uint8_t fan = 10, uint8_t mode = kDaikinCool) {
  if (temp < 16) temp = 16;
  if (temp > 32) temp = 32;

  daikin_power = power;
  daikin_temp = temp;
  daikin_fan = fan;

  // 1. Chuẩn Daikin 280-bit (ARC433, ARC452, ARC466, FTKC, FTKM, FTF, ATF - Phổ biến nhất Việt Nam)
  if (power) {
    ac_daikin.on();
    ac_daikin.setMode(mode);
    ac_daikin.setTemp(temp);
    ac_daikin.setFan(fan);
  } else {
    ac_daikin.off();
  }
  ac_daikin.send();

  delay(60);

  // 2. Chuẩn Daikin 160-bit (ARC423 - Dành cho dòng Daikin khác / đời cũ)
  if (power) {
    ac_daikin_160.on();
    ac_daikin_160.setMode(mode);
    ac_daikin_160.setTemp(temp);
    ac_daikin_160.setFan(fan);
  } else {
    ac_daikin_160.off();
  }
  ac_daikin_160.send();

  // Reset buffer nhận IR sau khi phát
  irrecv.resume();

  Serial.printf("❄️ [IR AC SENDER] Đã phát sóng IR -> Power: %s | Temp: %d°C | Fan: %d\n",
                power ? "ON" : "OFF", temp, fan);
}

int ledBrightness = 50;

// Khởi tạo các đối tượng cảm biến (Được khai báo extern trong hw_sensors.h)
RTC_DS3231 rtc;
Adafruit_AHTX0 aht;
Adafruit_BMP280 bmp;

bool aht_ready = false;
bool bmp_ready = false;
bool rtc_ready = false;

// Khởi tạo đối tượng LED WS2812B
Adafruit_NeoPixel pixels(NUM_LEDS, WS2812B_PIN, NEO_GRB + NEO_KHZ800);
int currentLedMode = 0;

// Đối tượng Firebase
FirebaseData fbdo;
FirebaseData stream;
FirebaseAuth auth;
FirebaseConfig config;
bool firebase_ready = false;

// Biến toàn cục quản lý trạng thái âm thanh & phát nhạc
bool isMusicMode = false;
bool wasAudioRunning = false;
unsigned long songConnectTime = 0;

extern String pendingSongUrl;
extern String pendingSongTitle;
extern String foundSongDisplay;
extern String searchMusicUrl(String songTitle);
extern volatile bool pendingReturnToMain;
extern volatile bool pendingReturnToRemote;

void setupButtons() {
  updateBootScreen(5);
  pinMode(TOUCH1_PIN, INPUT_PULLUP);
  pinMode(TOUCH2_PIN, INPUT_PULLUP);
  pinMode(TOUCH3_PIN, INPUT_PULLUP);
}

// Biến chặn nhiễu Firebase khi bấm nút vật lý
unsigned long last_local_relay_change = 0;

void handleTouch() { // Xử lý nút cứng với thuật toán chống rung chuẩn
  unsigned long now = millis();
  
  // Đọc tín hiệu thô từ chân cứng (LOW là nhấn do dùng INPUT_PULLUP)
  bool read1 = (digitalRead(TOUCH1_PIN) == LOW);
  bool read2 = (digitalRead(TOUCH2_PIN) == LOW);
  bool read3 = (digitalRead(TOUCH3_PIN) == LOW);
  
  // --- Nút 1 (Relay 1 / Next Slot) ---
  static bool last_read1 = false, state1 = false;
  static unsigned long time1 = 0;
  static unsigned long press_start1 = 0;
  if (read1 != last_read1) time1 = now;
  if ((now - time1) > 50) { // Chống rung 50ms
    if (read1 != state1) {
      state1 = read1;
      if (state1) {
        press_start1 = now;
      } else {
        if (press_start1 > 0 && (now - press_start1 > 50)) {
          if (is_ir_learning_mode) {
            selected_ir_idx = (selected_ir_idx + 1) % 14;
            updateIrScreen(selected_ir_idx, typeToString(learned_ir[selected_ir_idx].type), String((uint32_t)(learned_ir[selected_ir_idx].value & 0xFFFFFFFF), HEX));
            uiUpdatePending = true;
          } else {
            relay1 = !relay1;
            digitalWrite(RELAY1_PIN, relay1 ? LOW : HIGH);
            last_local_relay_change = millis();
            if (firebase_ready) {
              Firebase.RTDB.setBoolAsync(&fbdo, FIREBASE_NODE "/relay1", relay1);
            }
            saveSettingsToEEPROM(relay1, relay2, ledBrightness);
            Serial.printf("👉 Relay 1: %s\n", relay1 ? "ON" : "OFF");
            uiUpdatePending = true;
          }
          press_start1 = 0;
        }
      }
    }
  }
  last_read1 = read1;

  // --- Nút 2 (Relay 2 / Send IR) ---
  static bool last_read2 = false, state2 = false;
  static unsigned long time2 = 0;
  static unsigned long press_start2 = 0;
  if (read2 != last_read2) time2 = now;
  if ((now - time2) > 50) { // Chống rung 50ms
    if (read2 != state2) {
      state2 = read2;
      if (state2) {
        press_start2 = now;
      } else {
        if (press_start2 > 0 && (now - press_start2 > 50)) {
          if (is_ir_learning_mode) {
            if (selected_ir_idx >= 10 && selected_ir_idx <= 13) {
              // Điều khiển Daikin (Slot 10: Nguồn, Slot 11: Tăng nhiệt, Slot 12: Giảm nhiệt, Slot 13: Quạt)
              if (selected_ir_idx == 10) {
                daikin_power = !daikin_power;
              } else if (selected_ir_idx == 11) {
                if (daikin_temp < 32) daikin_temp++;
                else daikin_temp = 18;
              } else if (selected_ir_idx == 12) {
                if (daikin_temp > 18) daikin_temp--;
                else daikin_temp = 32;
              } else if (selected_ir_idx == 13) {
                if (daikin_fan == 10) daikin_fan = 1; // Auto -> 1
                else if (daikin_fan >= 1 && daikin_fan < 5) daikin_fan++; // 1->2->3->4->5
                else daikin_fan = 10; // 5 -> Auto
              }
              
              sendDaikinCommand(daikin_power, daikin_temp, daikin_fan);
              updateIrScreen(selected_ir_idx, "", ""); // Cập nhật lại UI hiển thị thông số mới
            } else if (learned_ir[selected_ir_idx].type != decode_type_t::UNKNOWN) {
              irsend.send(learned_ir[selected_ir_idx].type, learned_ir[selected_ir_idx].value, learned_ir[selected_ir_idx].bits);
              irrecv.resume();
              Serial.println("📡 Đã phát lệnh IR slot " + String(selected_ir_idx + 1));
            } else {
              Serial.println("⚠️ Slot này chưa có lệnh IR nào.");
            }
          } else {
            relay2 = !relay2;
            digitalWrite(RELAY2_PIN, relay2 ? LOW : HIGH);
            last_local_relay_change = millis();
            if (firebase_ready) {
              Firebase.RTDB.setBoolAsync(&fbdo, FIREBASE_NODE "/relay2", relay2);
            }
            saveSettingsToEEPROM(relay1, relay2, ledBrightness);
            Serial.printf("👉 Relay 2: %s\n", relay2 ? "ON" : "OFF");
            uiUpdatePending = true;
          }
          press_start2 = 0;
        }
      }
    }
  }
  last_read2 = read2;

  // --- Nút 3 (Gọi AI / Ngắt trả lời trở về màn hình chính) ---
  static bool last_read3 = false, state3 = false;
  static unsigned long time3 = 0;
  static unsigned long press_start3 = 0;
  
  if (read3 != last_read3) time3 = now;
  if ((now - time3) > 50) { // Chống rung 50ms
    if (read3 != state3) {
      state3 = read3;
      if (state3) { 
        press_start3 = now;
      } else {
        // Vừa thả tay ra
        if (press_start3 > 0) {
          unsigned long duration = now - press_start3;
          if (duration < 1000) { // Nhấn thả nhanh (dưới 1000ms)
            extern String pendingSongUrl;
            extern String pendingSongTitle;
            extern bool audio_just_finished;
            extern bool isWaitingFollowupCommand;
            extern bool isAiBusy;

            // Kiểm tra xem AI có đang hoạt động (đang phát âm thanh, đang nói TTS, đang phát nhạc, đang thu âm, hoặc đang ở màn hình AI/Music/IR)
            bool isAiActive = audio.isRunning() || isRecording || isMusicMode || is_ir_learning_mode ||
                              (lv_screen_active() == screen_ai) || (lv_screen_active() == screen_music) || (lv_screen_active() == screen_ir);

            if (isAiActive) {
              // 👉 TRƯỜNG HỢP 1: NGẮT TRẢ LỜI & TRỞ VỀ MÀN HÌNH CHÍNH (KHÔNG XÓA DỮ LIỆU/CÀI ĐẶT/BỘ NHỚ)
              Serial.println("🛑 [Nút 3] Nhấn ngắt AI / Dừng âm thanh -> Trở về màn hình chính!");

              // Dừng âm thanh/nhạc lập tức
              if (audio.isRunning()) {
                audio.stopSong();
                delay(30);
              }

              // Hủy ghi âm nếu đang thu
              if (isRecording) {
                cancelRecording();
              }

              // Xóa sạch hàng đợi lịch trình phát (không ảnh hưởng bộ nhớ lâu dài Preferences/Firebase)
              pendingSongUrl = "";
              pendingSongTitle = "";
              audio_just_finished = false;
              isWaitingFollowupCommand = false;
              isMusicMode = false;
              wasAudioRunning = false;
              isAiBusy = false;
              pendingReturnToMain = false;
              pendingReturnToRemote = false;
              if (is_ir_learning_mode) {
                is_ir_learning_mode = false;
                irrecv.disableIRIn(); // Tắt ngắt thu hồng ngoại khi thoát
              }

              // Dừng màn hình nhạc nếu có
              stopMusicScreen();

              // Reset trạng thái AI & LED về IDLE
              pendingAiFaceState = -1; // Tránh main loop load lại screen_ai
              setAIFaceState(AI_STATE_IDLE);
              setLedMode(0);

              // Chuyển ngay về màn hình chính Dashboard
              showMainScreen();
              uiUpdatePending = true;

            } else {
              // 👉 TRƯỜNG HỢP 2: ĐANG Ở MÀN HÌNH CHÍNH -> GỌI AI LẮNG NGHE LỆNH
              Serial.println("🟢 [Nút 3] Gọi AI từ màn hình chính -> Bắt đầu ghi âm thủ công...");

              pendingSongUrl = "";
              pendingSongTitle = "";
              audio_just_finished = false;
              isMusicMode = false;
              wasAudioRunning = false;
              isAiBusy = false;
              pendingReturnToMain = false;
              pendingReturnToRemote = false;

              // Chuyển sang màn hình AI
              if (screen_ai) {
                lv_screen_load(screen_ai);
              }
              setAIFaceState(AI_STATE_LISTENING);
              setLedMode(2); // LED Listening Cyan
              extern void setAIChatDialogue(String userText, String aiText);
              setAIChatDialogue("Dang nghe...", "...");
              startRecording(true); // Ghi âm thủ công từ nút
              uiUpdatePending = true;
            }
          }
          press_start3 = 0;
        }
      }
    }
  }
  last_read3 = read3;

  // Xử lý giữ nút 3 trên 3 giây (Vào / Thoát chế độ Học Lệnh IR)
  if (state3 && press_start3 > 0) {
    unsigned long duration = now - press_start3;
    if (duration > 3000) { // Giữ 3 giây
      is_ir_learning_mode = !is_ir_learning_mode;
      if (is_ir_learning_mode) {
        if (audio.isRunning()) audio.stopSong();
        if (isRecording) cancelRecording();
        irrecv.enableIRIn(); // Chỉ bật ngắt phần cứng khi thực sự vào chế độ học lệnh
        showIrScreen();
        updateIrScreen(selected_ir_idx, typeToString(learned_ir[selected_ir_idx].type), String((uint32_t)(learned_ir[selected_ir_idx].value & 0xFFFFFFFF), HEX));
        Serial.println("🟢 BẬT Chế độ học lệnh IR (Đã kích hoạt ngắt thu IR).");
      } else {
        irrecv.disableIRIn(); // Tắt ngắt phần cứng để giải phóng 100% CPU cho LED & Màn hình
        showMainScreen();
        Serial.println("🔴 TẮT Chế độ học lệnh IR (Đã tắt ngắt thu IR) -> Về màn hình chính.");
      }
      uiUpdatePending = true;
      press_start3 = 0; // Đánh dấu đã xử lý
    }
  }
}
// Biến quản lý trạng thái Web App
String pendingWebCommand = "";
bool hasWebCommand = false;

uint8_t audioVolume = 21;

// Biến quản lý thời gian
unsigned long lastWeatherUpdate = 0;
const unsigned long WEATHER_UPDATE_INTERVAL = 300000; // 5 phút

bool audio_just_finished = false;

void notifyAudioFinished() {
  audio_just_finished = true;
}

void audio_info(const char *info) {
  Serial.printf("[AudioInfo] %s\n", info);
}

void audio_eof_mp3(const char *info) {
  Serial.printf("[Audio EOF MP3] %s\n", info);
  audio_just_finished = true;
}

void audio_eof_speech(const char *info) {
  Serial.printf("[Audio EOF Speech] %s\n", info);
  audio_just_finished = true;
}

void audio_eof_stream(const char *info) {
  Serial.printf("[Audio EOF Stream] %s\n", info);
  audio_just_finished = true;
}

// Hàm loại bỏ dấu Tiếng Việt
String removeVietnameseAccents(String text) {
  const char* vn_chars[] = {
    "á","à","ả","ã","ạ","ă","ắ","ằ","ẳ","ẵ","ặ","â","ấ","ầ","ẩ","ẫ","ậ",
    "é","è","ẻ","ẽ","ẹ","ê","ế","ề","ể","ễ","ệ",
    "í","ì","ỉ","ĩ","ị",
    "ó","ò","ỏ","õ","ọ","ô","ố","ồ","ổ","ỗ","ộ","ơ","ớ","ờ","ở","ỡ","ợ",
    "ú","ù","ủ","ũ","ụ","ư","ứ","ừ","ử","ữ","ự",
    "ý","ỳ","ỷ","ỹ","ỵ",
    "đ",
    "Á","À","Ả","Ã","Ạ","Ă","Ắ","Ằ","Ẳ","Ẵ","Ặ","Â","Ấ","Ầ","Ẩ","Ẫ","Ậ",
    "É","È","Ẻ","Ẽ","Ẹ","Ê","Ế","Ề","Ể","Ễ","Ệ",
    "Í","Ì","Ỉ","Ĩ","Ị",
    "Ó","Ò","Ỏ","Õ","Ọ","Ô","Ố","Ồ","Ổ","Ỗ","Ộ","Ơ","Ớ","Ờ","Ở","Ỡ","Ợ",
    "Ú","Ù","Ủ","Ũ","Ụ","Ư","Ứ","Ừ","Ử","Ữ","Ự",
    "Ý","Ỳ","Ỷ","Ỹ","Ỵ",
    "Đ"
  };
  const char* en_chars[] = {
    "a","a","a","a","a","a","a","a","a","a","a","a","a","a","a","a","a",
    "e","e","e","e","e","e","e","e","e","e","e",
    "i","i","i","i","i",
    "o","o","o","o","o","o","o","o","o","o","o","o","o","o","o","o","o",
    "u","u","u","u","u","u","u","u","u","u","u",
    "y","y","y","y","y",
    "d",
    "A","A","A","A","A","A","A","A","A","A","A","A","A","A","A","A","A",
    "E","E","E","E","E","E","E","E","E","E","E",
    "I","I","I","I","I",
    "O","O","O","O","O","O","O","O","O","O","O","O","O","O","O","O","O",
    "U","U","U","U","U","U","U","U","U","U","U",
    "Y","Y","Y","Y","Y",
    "D"
  };
  for (int i = 0; i < sizeof(vn_chars)/sizeof(vn_chars[0]); i++) {
    text.replace(vn_chars[i], en_chars[i]);
  }
  return text;
}

void fetchWeatherInternal() {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(8000);
    // URL được thiết lập từ config.h, thêm lang=vi để lấy tiếng Việt
    String url = String(weatherApiUrl) + "?lat=" + String(weatherLat, 6) + 
                 "&lon=" + String(weatherLon, 6) + 
                 "&units=metric&lang=vi&appid=" + String(weatherApiKey);
                 
    Serial.println("🌐 Đang tải thời tiết từ OpenWeatherMap (Background Core 0)...");
    http.begin(client, url);
    int httpCode = http.GET();
    
    if (httpCode == 200) {
      String payload = http.getString();
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, payload);
      
      if (!error) {
        owmTemp = doc["main"]["temp"].as<float>();
        owmHum = doc["main"]["humidity"].as<float>();
        owmWind = doc["wind"]["speed"].as<float>();
        const char* desc = doc["weather"][0]["description"];
        owmDesc = removeVietnameseAccents(String(desc));
        ai_prediction_short = owmDesc;
        Serial.printf("✅ Thời tiết OUTDOOR: %.1f C, %s\n", owmTemp, owmDesc.c_str());
        uiUpdatePending = true;
      }
    } else {
      Serial.printf("❌ Lỗi gọi API OWM: %d\n", httpCode);
    }
    http.end();
    client.stop();
  }
}

void weatherTask(void *pvParameters) {
  fetchWeatherInternal();
  vTaskDelete(NULL);
}

void fetchWeather() {
  static TaskHandle_t wTaskHandle = NULL;
  if (wTaskHandle != NULL) {
    eTaskState state = eTaskGetState(wTaskHandle);
    if (state != eDeleted && state != eInvalid) return;
  }
  xTaskCreatePinnedToCore(weatherTask, "weatherTask", 16384, NULL, 1, &wTaskHandle, 0);
}

// ---------------- FIREBASE FUNCTIONS ----------------
void streamCallback(FirebaseStream data) {
  // Nếu vừa bấm nút vật lý trong vòng 2 giây, bỏ qua stream dội lại từ Firebase
  if (millis() - last_local_relay_change < 2000) {
    return;
  }
  
  String path = data.dataPath();
  
  // Xử lý Relay
  if (path.indexOf("/relay1") != -1 && data.dataType() == "boolean") {
    relay1 = data.boolData();
    digitalWrite(RELAY1_PIN, relay1 ? LOW : HIGH);
    uiUpdatePending = true;
    saveSettingsToEEPROM(relay1, relay2, ledBrightness);
    Serial.printf("🔥 Firebase -> Relay 1: %s\n", relay1 ? "ON" : "OFF");
  } else if (path.indexOf("/relay2") != -1 && data.dataType() == "boolean") {
    relay2 = data.boolData();
    digitalWrite(RELAY2_PIN, relay2 ? LOW : HIGH);
    uiUpdatePending = true;
    saveSettingsToEEPROM(relay1, relay2, ledBrightness);
    Serial.printf("🔥 Firebase -> Relay 2: %s\n", relay2 ? "ON" : "OFF");
  }
  
  // Xử lý Cài đặt
  if (path.indexOf("/settings/ledBrightness") != -1) {
    ledBrightness = data.intData();
    pixels.setBrightness(ledBrightness);
    pixels.show();
    saveSettingsToEEPROM(relay1, relay2, ledBrightness);
    Serial.printf("🔥 Firebase -> Độ sáng LED: %d\n", ledBrightness);
  } else if (path.indexOf("/settings/audioVolume") != -1) {
    audioVolume = data.intData();
    audio.setVolume(audioVolume);
    Serial.printf("🔥 Firebase -> Âm lượng loa: %d\n", audioVolume);
  } else if (path.indexOf("/settings/acPower") != -1 && data.dataType() == "boolean") {
    daikin_power = data.boolData();
    sendDaikinCommand(daikin_power, daikin_temp, daikin_fan);
    Serial.printf("🔥 Firebase -> Điều hòa Power: %s\n", daikin_power ? "ON" : "OFF");
  } else if (path.indexOf("/settings/acTemp") != -1) {
    daikin_temp = data.intData();
    sendDaikinCommand(daikin_power, daikin_temp, daikin_fan);
    Serial.printf("🔥 Firebase -> Điều hòa Nhiệt độ: %d\n", daikin_temp);
  } else if (path.indexOf("/settings/acFan") != -1) {
    daikin_fan = data.intData();
    sendDaikinCommand(daikin_power, daikin_temp, daikin_fan);
    Serial.printf("🔥 Firebase -> Điều hòa Quạt: %d\n", daikin_fan);
  }
  
  // Xử lý Lệnh Giọng Nói từ Web (Hỗ trợ cả chuỗi String và JSON object để tránh lỗi Cache Web)
  if (path.indexOf("/ai/webCommand") != -1) {
    if (data.dataType() == "string") {
      pendingWebCommand = data.stringData();
    } else if (data.dataType() == "json") {
      FirebaseJson& json = data.jsonObject();
      FirebaseJsonData jsonData;
      json.get(jsonData, "text");
      if (jsonData.success) {
        pendingWebCommand = jsonData.stringValue;
      }
    }
    
    if (pendingWebCommand.length() > 0) {
      hasWebCommand = true;
      Serial.println("🔥 Firebase -> Lệnh giọng nói từ Web: " + pendingWebCommand);
    }
  }
}

void streamTimeoutCallback(bool timeout) {
  if (timeout) {
    Serial.println("🔥 Stream timeout, resuming...");
  }
}

void setupFirebase() {
  updateBootScreen(50); // Khoi tao Firebase
  config.api_key = FIREBASE_API_KEY;
  auth.user.email = FIREBASE_USER_EMAIL;
  auth.user.password = FIREBASE_USER_PASSWORD;
  config.database_url = FIREBASE_URL;

  fbdo.setResponseSize(1024);
  config.token_status_callback = tokenStatusCallback;
  
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  firebase_ready = true;
  
  // Thiết lập luồng lắng nghe trạng thái toàn hệ thống
  if (!Firebase.RTDB.beginStream(&stream, FIREBASE_NODE)) {
    Serial.printf("❌ Lỗi stream Firebase: %s\n", stream.errorReason().c_str());
  }
  Firebase.RTDB.setStreamCallback(&stream, streamCallback, streamTimeoutCallback);
  updateBootScreen(60); // Firebase khoi tao xong
}
// ---------------------------------------------------

// Tác vụ điều khiển LED Ring 12 bóng độc lập trên Core 0 (Đạt 55 FPS mượt mà không bị gián đoạn)
void ledTask(void *pvParameters) {
  while (true) {
    handleLedAnimation();
    vTaskDelay(pdMS_TO_TICKS(18));
  }
}

#include <esp_task_wdt.h>

void setup() {
  // Cấu hình Task Watchdog 30s an toàn, không theo dõi Idle Task để các tác vụ AI & Audio chạy mượt mà
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  esp_task_wdt_config_t twdt_config = {
      .timeout_ms = 30000,
      .idle_core_mask = 0,
      .trigger_panic = false
  };
  esp_task_wdt_reconfigure(&twdt_config);
#else
  esp_task_wdt_init(30, false);
#endif

  // Khởi tạo đèn nền màn hình TFT ngay từ mili-giây đầu tiên để giữ điện áp ổn định, chống nháy đèn
  initTftBacklight();

  Serial.begin(115200);
  delay(1000);
  Serial.println("Khoi dong he thong ESP32 (Giao dien & Cam bien)...");

  // Khởi tạo các chân Relay (Tắt Relay: với relay kích mức THẤP thì HIGH là TẮT)
  // ESP32 cần kéo mức cao trước khi pinMode để tránh nhảy rơ-le 1 nhịp
  digitalWrite(RELAY1_PIN, HIGH);
  digitalWrite(RELAY2_PIN, HIGH);
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  digitalWrite(RELAY1_PIN, HIGH);
  digitalWrite(RELAY2_PIN, HIGH);
  relay1 = false;
  relay2 = false;

  // Khởi tạo giao diện đồ họa LVGL trước (Sẽ tự động hiển thị Màn hình Boot)
  setupLVGL();
  
  // Khởi tạo nút bấm cứng
  setupButtons();

  // Khởi tạo Hồng Ngoại (Daikin & IR Send)
  ac_daikin.begin();
  ac_daikin_160.begin();
  irsend.begin();
  // Không bật irrecv.enableIRIn() ở chế độ thường để tránh ngắt phần cứng 20,000 lần/giây gây giật LED & màn hình
  Serial.println("📡 Đã khởi tạo bộ Phát Hồng Ngoại & Daikin.");

  // Khởi tạo Audio (Mic I2S và Loa MAX98357A)
  setupAudio();
  setupAiTask(); // Khởi tạo Worker Task xử lý AI bất đồng bộ trên Core 0
  
  updateBootScreen(10); // Khoi tao Cam bien I2C
  // Khởi tạo các cảm biến I2C (RTC, AHT20, BMP280)
  setupSensors();
  
  // Load mã IR từ EEPROM sau khi I2C đã được khởi tạo
  loadIRCodesFromAT24();

  // Khôi phục trạng thái Relay / Đèn từ EEPROM
  if (loadSettingsFromEEPROM(relay1, relay2, ledBrightness)) {
    digitalWrite(RELAY1_PIN, relay1 ? LOW : HIGH);
    digitalWrite(RELAY2_PIN, relay2 ? LOW : HIGH);
  } else {
    // Nếu EEPROM trống, tắt hết mặc định
    relay1 = false;
    relay2 = false;
    ledBrightness = 50;
    digitalWrite(RELAY1_PIN, HIGH);
    digitalWrite(RELAY2_PIN, HIGH);
  }

  // Khởi tạo LED WS2812B
  setupLED();
  pixels.setBrightness(ledBrightness);
  pixels.show();
  setLedMode(0); // IDLE mode

  xTaskCreatePinnedToCore(
    ledTask,
    "ledTask",
    4096,
    NULL,
    2,
    NULL,
    0
  );
  
  updateBootScreen(20); // Ket noi WiFi
  // Kết nối WiFi qua WiFiManager
  Serial.println("Đang kết nối WiFi...");
  WiFiManager wm;
  wm.setConnectTimeout(15); // Đợi 15 giây, nếu không có tự phát AP
  wm.autoConnect("ESP32_AI_Hub");
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("✅ Đã kết nối WiFi!");
    updateBootScreen(30); // Da ket noi WiFi
    
    // Đồng bộ thời gian thực từ Internet về ESP32
    updateBootScreen(40); // Dong bo Gio Quoc Te (NTP)
    configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org", "time.nist.gov", "time.windows.com");
    
    // Kiểm tra xem RTC đã có giờ chuẩn chưa (Năm >= 2024)
    bool need_wait = true;
    if (rtc_ready) {
      DateTime rtcNow = rtc.now();
      if (rtcNow.year() >= 2024) {
        need_wait = false; // Bỏ qua chờ NTP để boot cho nhanh
        Serial.println("✅ RTC đã có giờ chuẩn, bỏ qua chờ NTP để tăng tốc khởi động.");
      }
    }

    if (need_wait) {
      Serial.println("⏳ Đang đồng bộ giờ từ Internet (RTC chưa có giờ)...");
      struct tm timeinfo;
      bool time_synced = false;
      
      // Đợi tối đa 5 giây (10 vòng * 500ms) để lấy giờ
      for (int i = 0; i < 10; i++) {
        if (getLocalTime(&timeinfo, 500)) {
          if (timeinfo.tm_year > 120) { // Năm > 2020
            time_synced = true;
            break;
          }
        }
        Serial.print(".");
      }
  
      if (time_synced) {
        if (rtc_ready) {
          rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec));
          Serial.println("\n✅ Đã đồng bộ RTC DS3231 thành công!");
        }
      } else {
        Serial.println("\n❌ Không thể lấy giờ từ NTP, sẽ dùng giờ cũ.");
      }
    }
    
    // Lấy thời tiết lần đầu
    updateBootScreen(70); // Tai du lieu thoi tiet
    fetchWeather();
    
    // Khởi tạo Firebase
    setupFirebase();
    
    updateBootScreen(100); // HOAN TAT KHOI DONG!
    delay(1000); // Chờ 1 giây để người dùng đọc thông báo
  } else {
    Serial.println("❌ Kết nối WiFi thất bại!");
    updateBootScreen(0); // Loi ket noi WiFi!
    delay(2000);
  }
  
  // Chuyển sang màn hình chính
  showMainScreen();

  // Kích hoạt Micro & Auto-VAD sau khi hệ thống đã khởi động hoàn tất 100%
  system_ready = true;
  Serial.println("🚀 [System Ready] Đã khởi động xong toàn bộ, Micro & XiaoZhi VAD sẵn sàng lắng nghe!");
}

void loop() {
  processAudioLoop();
  unsigned long now = millis();

  // Đảm bảo ép Firebase về OFF một lần duy nhất sau khi đã khởi động thành công
  static bool firebase_init_sync = false;
  if (firebase_ready && Firebase.ready() && !firebase_init_sync) {
    Firebase.RTDB.setBoolAsync(&fbdo, FIREBASE_NODE "/relay1", false);
    Firebase.RTDB.setBoolAsync(&fbdo, FIREBASE_NODE "/relay2", false);
    firebase_init_sync = true;
    Serial.println("🔥 Đã đồng bộ tắt toàn bộ Relay lên Firebase.");
  }

  // Quản lý kết nối WiFi
  if (WiFi.status() != WL_CONNECTED) {
  }

  // 1. Quét nút cảm ứng liên tục
  handleTouch();
  
  // 1.2 Học lệnh Hồng Ngoại (Chỉ xử lý khi đang ở chế độ học lệnh)
  if (is_ir_learning_mode && irrecv.decode(&ir_results)) {
    Serial.println("\n📡 [IR Nhận Lệnh]");
    Serial.print("Giao thức: ");
    Serial.println(typeToString(ir_results.decode_type));
    Serial.print("Mã Hex: 0x");
    Serial.println(resultToHexidecimal(&ir_results));
    
    if (ir_results.decode_type != decode_type_t::UNKNOWN) {
      if (selected_ir_idx < MAX_IR_SLOTS) {
        learned_ir[selected_ir_idx].type = ir_results.decode_type;
        learned_ir[selected_ir_idx].value = ir_results.value;
        learned_ir[selected_ir_idx].bits = ir_results.bits;
        
        saveIRCodeToAT24(selected_ir_idx);
        
        updateIrScreen(selected_ir_idx, typeToString(learned_ir[selected_ir_idx].type), String((uint32_t)(learned_ir[selected_ir_idx].value & 0xFFFFFFFF), HEX));
        uiUpdatePending = true;
      } else {
        Serial.println("⚠️ Slot này là điều hòa Daikin, không thể học lệnh RAW!");
      }
    }
    
    irrecv.resume(); // Sẵn sàng nhận mã tiếp theo
  }

  // Bơm dữ liệu âm thanh mượt mà
  processAudioLoop();
  if (audio.isRunning()) processAudioLoop();
  
  // 2. Cập nhật dữ liệu Cảm biến & RTC
  updateSensors();
  
  // 3. Datalogging vào EEPROM AT24C32 định kỳ (mỗi 60 giây)
  static unsigned long lastOfflineLog = 0;
  if (now - lastOfflineLog >= 60000) { 
    lastOfflineLog = now;
    uint32_t currentUnix = rtc_ready ? rtc.now().unixtime() : (uint32_t)(1770000000 + (now / 1000));
    logSensorDataOffline(currentUnix, indoorTemp, indoorHum, indoorPres);
  }

  // 4. Tự động làm mới màn hình mỗi 1 giây (để cập nhật giờ và số liệu sensor)
  static unsigned long lastUiRefresh = 0;
  if (now - lastUiRefresh >= 1000) {
    lastUiRefresh = now;
    uiUpdatePending = true;
  }

  // Chuyển về trạng thái IDLE khi AI nói xong
  static unsigned long idleStartTime = 0;
  static unsigned long ttsAttemptTime = 0;
  extern bool audio_just_finished;
  
  // 0. Xử lý yêu cầu ngắt âm thanh từ Core 0 / Background Task an toàn trên Core 1
  if (hasPendingAudioStop) {
    hasPendingAudioStop = false;
    if (audio.isRunning()) {
      audio.stopSong();
      delay(40);
    }
  }

  // 0. Xử lý kết nối phát TTS an toàn 100% trong Main Loop (Chống xung đột đa luồng)
  if (hasPendingTts) {
    hasPendingTts = false;
    if (audio.isRunning()) {
      audio.stopSong();
      delay(40);
    }
    ttsAttemptTime = millis();
    wasAudioRunning = false;
    if (pendingTtsSpeech.length() > 0) {
      String speechText = pendingTtsSpeech;
      pendingTtsSpeech = "";
      pendingTtsUrl = "";
      Serial.println("🗣️ [TTS Engine] Bắt đầu phát Speech (m_f_tts = true): " + speechText);
      audio.connecttospeech(speechText.c_str(), "vi");
    } else if (pendingTtsUrl.length() > 0) {
      String urlToPlay = pendingTtsUrl;
      pendingTtsUrl = "";
      pendingTtsSpeech = "";
      Serial.println("🔗 [TTS Engine] Bắt đầu phát URL Stream: " + urlToPlay);
      audio.connecttohost(urlToPlay.c_str());
    }
  }

  // 0.1. Xử lý tự động kích hoạt stream nhạc ngay khi Core 0 Background Worker tìm thấy link
  if (pendingSongUrl.length() > 0 && !audio.isRunning() && !hasPendingTts) {
    String urlToPlay = pendingSongUrl;
    pendingSongUrl = "";
    String titleToShow = foundSongDisplay.length() > 0 ? foundSongDisplay : (pendingSongTitle.length() > 0 ? pendingSongTitle : "Đang phát nhạc");
    pendingSongTitle = "";
    Serial.println("🎵 [Music Trigger] Bắt đầu phát nhạc MP3 Stream: " + urlToPlay);
    showMusicScreen(titleToShow);
    isMusicMode = true;
    songConnectTime = millis();
    wasAudioRunning = false;
    ttsAttemptTime = 0;
    audio.connecttohost(urlToPlay.c_str());
  }
  
  bool currentAudioRunning = audio.isRunning();
  static unsigned long notRunningStartTime = 0;
  
  if (currentAudioRunning) {
    wasAudioRunning = true;
    notRunningStartTime = 0;
    idleStartTime = 0;
    ttsAttemptTime = 0;
  } else {
    // Nếu audio vừa kết thúc (TTS hoặc nhạc xong), đợi 350ms xác nhận đã kết thúc thực sự
    if (wasAudioRunning) {
      if (notRunningStartTime == 0) notRunningStartTime = millis();
      if (millis() - notRunningStartTime >= 350) {
        audio_just_finished = true;
        if (pendingSongTitle.length() == 0 && pendingSongUrl.length() == 0) {
          isMusicMode = false;
        }
        notRunningStartTime = 0;
      }
    }
  }
  
  // Kiểm tra điều kiện ngắt: Audio đã phát xong hoàn toàn HOẶC Timeout kết nối ban đầu (15s khi không phát nhạc)
  if (audio_just_finished || (!isMusicMode && ttsAttemptTime > 0 && millis() - ttsAttemptTime > 15000)) {
    audio_just_finished = false;
    wasAudioRunning = false;
    ttsAttemptTime = 0;
    notRunningStartTime = 0;
    
    if (pendingSongTitle.length() > 0) {
      String titleToSearch = pendingSongTitle;
      pendingSongTitle = "";
      Serial.println("🔎 AI đang tìm kiếm bài hát trên Core 0 Background: " + titleToSearch);
      extern void setLedMode(int mode);
      setLedMode(1); // Thinking / Loading LED mode
      triggerMusicSearchTask(titleToSearch);
    } else if (pendingSongUrl.length() > 0) {
      String urlToPlay = pendingSongUrl;
      pendingSongUrl = "";
      Serial.println("🎵 Bắt đầu phát nhạc MP3 Stream: " + urlToPlay);
      showMusicScreen(urlToPlay);
      isMusicMode = true;
      songConnectTime = millis();
      wasAudioRunning = false;
      ttsAttemptTime = 0; // Xóa timeout TTS để không bị ngắt nhạc giữa chừng
      audio.connecttohost(urlToPlay.c_str());
    } else {
      isMusicMode = false;
      audio.stopSong(); // Giải phóng hoàn toàn socket và heap của Audio stream
      stopMusicScreen();
      extern bool isStreamingAiText;
      isStreamingAiText = false; // Ngắt hoạt họa chữ chạy của câu nói cũ
      extern bool isRecording;
      extern bool isWaitingFollowupCommand;
      if (isRecording) {
        // Đang trong tiến trình ghi âm cướp lời hoặc thủ công, giữ nguyên trạng thái thu âm!
        Serial.println("🎙️ Âm thanh cũ đã dừng, tiếp tục thu âm...");
      } else if (isWaitingFollowupCommand) {
        isWaitingFollowupCommand = false;
        extern bool isAiBusy;
        isAiBusy = false;
        setAIFaceState(AI_STATE_LISTENING);
        setLedMode(2); // LED Mode 2: Cyan Listening
        startRecording(true); // Mở mic nhận câu lệnh trực tiếp (không bắt buộc lặp lại từ khóa Wake-Word)
        Serial.println("🎙️ [XiaoZhi Followup] Đã chào xong, tự động mở mic thu âm câu lệnh tiếp theo...");
      } else if (pendingReturnToMain) {
        pendingReturnToMain = false;
        pendingReturnToRemote = false;
        extern bool isAiBusy;
        isAiBusy = false;
        idleStartTime = millis();
        showMainScreen();
        setAIFaceState(AI_STATE_IDLE);
        setLedMode(0);
        uiUpdatePending = true;
        Serial.println("📱 [Screen Switch] TTS đã nói xong -> Đã tự động chuyển về Màn hình chính Dashboard thành công!");
      } else if (pendingReturnToRemote) {
        pendingReturnToRemote = false;
        pendingReturnToMain = false;
        extern bool isAiBusy;
        isAiBusy = false;
        idleStartTime = millis();
        showIrScreen();
        updateIrScreen(selected_ir_idx, typeToString(learned_ir[selected_ir_idx].type), String((uint32_t)(learned_ir[selected_ir_idx].value & 0xFFFFFFFF), HEX));
        setLedMode(0);
        uiUpdatePending = true;
        Serial.println("📱 [Screen Switch] TTS đã nói xong -> Đã tự động chuyển sang Màn hình Remote Học Lệnh thành công!");
      } else {
        stopMusicScreen();
        requestScreen(SCREEN_AI);
        
        // 👉 TỰ ĐỘNG KÍCH HOẠT HỘI THOẠI ĐA TẦNG LIÊN TỤC (CONTINUOUS MULTI-TURN DIALOGUE)
        extern bool isAiBusy;
        isAiBusy = false;
        setAIFaceState(AI_STATE_LISTENING);
        setLedMode(2); // LED Mode 2: Cyan Listening (Thở nhẹ báo hiệu đang chờ câu hỏi tiếp theo)
        extern void setAIChatDialogue(String userText, String aiText);
        setAIChatDialogue("Dang nghe tiep...", "...");
        uiUpdatePending = true;
        
        startRecording(true); // Mở mic thu âm câu hỏi tiếp nối trong 4.5s (không cần nói lại "Hi Nori")
        Serial.println("🎙️ [Continuous Dialogue] Đã nói xong -> Tự động mở Mic lắng nghe câu hỏi tiếp theo trong 4.5s...");
      }
    }
  }
  
  // Không tự quay về màn hình chính nữa.
  // Sau khi TTS nói xong → ở lại màn hình AI (IDLE/sẵn sàng) chờ nhấn nút ghi âm tiếp.
  // Chỉ thoát AI khi người dùng giữ nút 1 giây.

  // Chạy vòng lặp âm thanh (Ghi âm & Phát I2S)
  processAudioLoop();

  // 3. Tự động lấy thời tiết mỗi 5 phút từ OpenWeatherMap
  if (now - lastWeatherUpdate >= WEATHER_UPDATE_INTERVAL) {
    lastWeatherUpdate = now;
    fetchWeather();
  }

  // 4. Đẩy dữ liệu cảm biến lên Firebase mỗi 10 giây (updateNodeAsync là hàm non-blocking async tức thời)
  static unsigned long lastFirebasePush = 0;
  if (firebase_ready && Firebase.ready() && (now - lastFirebasePush >= 10000)) {
    lastFirebasePush = now;
    FirebaseJson json;
    json.set("temperature", indoorTemp);
    json.set("humidity", indoorHum);
    json.set("indoorTemp", indoorTemp);
    json.set("indoorHum", indoorHum);
    json.set("pressure", indoorPres);
    json.set("outdoor_temp", owmTemp);
    json.set("outdoor_desc", owmDesc);
    json.set("outTemp", owmTemp);
    json.set("outHum", owmHum);
    json.set("outWindSpd", owmWind);
    json.set("outDesc", owmDesc);
    json.set("aiAdvice", ai_prediction_short);
    json.set("time", hhmmText);
    json.set("dateSolar", dateSolar);
    json.set("dateLunar", dateLunar);
    json.set("relay1", relay1);
    json.set("relay2", relay2);
    json.set("settings/acPower", daikin_power);
    json.set("settings/acTemp", (int)daikin_temp);
    json.set("settings/acFan", (int)daikin_fan);
    json.set("settings/ledBrightness", ledBrightness);
    json.set("settings/audioVolume", (int)audioVolume);
    time_t nowTime;
    time(&nowTime);
    json.set("last_seen", (int)nowTime);
    Firebase.RTDB.updateNodeAsync(&fbdo, FIREBASE_NODE, &json);
  }

  // 5. Cập nhật dữ liệu lên giao diện an toàn 100% trên luồng Core 1
  if (pendingAiFaceState != -1) {
    int st = pendingAiFaceState;
    pendingAiFaceState = -1;
    applyAIFaceState(st);
  }
  if (uiUpdatePending) {
    uiUpdatePending = false;
    updateLVGL_UI();
  }
  
  // 6. Xử lý lệnh giọng nói từ Web App
  if (hasWebCommand) {
    hasWebCommand = false;
    Serial.println("🌐 Đang xử lý lệnh giọng nói từ Web: " + pendingWebCommand);
    
    // Nếu đang phát âm thanh thì dừng lại
    if (audio.isRunning()) {
      audio.stopSong();
    }
    
    if (screen_ai) {
      lv_screen_load(screen_ai);
    }
    setAIFaceState(AI_STATE_THINKING);
    extern void setLedMode(int mode);
    setLedMode(1);
    triggerAiTextProcess(pendingWebCommand);
  }

  // 7. Xử lý các tác vụ của LVGL Engine mượt mà chuẩn xác
  static unsigned long last_tick = 0;
  unsigned long now_ms = millis();
  if (last_tick == 0) last_tick = now_ms;
  unsigned long tick = now_ms - last_tick;
  last_tick = now_ms;
  if (tick > 0) lv_tick_inc(tick); 
  
  extern void handlePendingScreenSwitch();
  handlePendingScreenSwitch();
  lv_timer_handler();
  processAudioLoop();
  yield();
}
